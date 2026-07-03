#include "manchester.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>  // rtc_gpio_pulldown_dis / rtc_gpio_pullup_dis
#include <driver/rmt.h>
#include <esp_rom_sys.h>    // esp_rom_delay_us
#include <freertos/ringbuf.h>

static constexpr gpio_num_t    kProgGpio   = GPIO_NUM_4;
static constexpr gpio_num_t    kStrobGpio  = GPIO_NUM_21;
// On ESP32-S3, RMT channels 0–3 are TX-only; RX requires channel 4–7.
static constexpr rmt_channel_t kRxChannel  = RMT_CHANNEL_4;
static constexpr uint32_t      kRmtClkMhz  = 10u;   // 80 MHz / clk_div=8
static constexpr uint32_t      kMarkUs     = 74u;   // start/end mark pulse width (µs)

// T/2 in microseconds, derived from bit_period_us in manchester_tx_init().
static uint32_t        s_half_us       = 0;

// RX state
static uint32_t        s_rx_half_ticks = 0;   // T/2 expressed in 10 MHz RMT ticks
static RingbufHandle_t s_rx_rb         = nullptr;
// Set true by manchester_tx_send(arm_rx=true); cleared after each collect.
// Prevents manchester_rx_receive() from re-arming (and losing captured data)
// when the caller already armed via TX.
static bool            s_rx_armed      = false;
// Raw copy of the last capture, kept for post-mortem via manchester_rx_last_raw().
static rmt_item32_t    s_last_items[96];
static size_t          s_last_count    = 0;

void manchester_tx_init(uint32_t bit_period_us) {
    s_half_us = bit_period_us / 2u;   // 33 / 2 = 16 µs → T ≈ 32 µs (31.25 kbps)

    // Open-drain output + input on PROG.
    // INPUT_OUTPUT_OD: gpio_set_level(1) → NMOS off → 3.3V via external 10K pull-up.
    //                  gpio_set_level(0) → NMOS on  → GND.
    // PULLDOWN_DISABLE: prevents internal ~45 K PD from dividing the idle voltage below 3.3V.
    // INPUT mode retained so the RMT RX can read the line back.
    gpio_config_t io = {};
    io.pin_bit_mask  = (1ULL << kProgGpio);
    io.mode          = GPIO_MODE_INPUT_OUTPUT_OD;
    io.pull_up_en    = GPIO_PULLUP_DISABLE;
    io.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io.intr_type     = GPIO_INTR_DISABLE;
    gpio_config(&io);
    // gpio_config() clears the digital IO_MUX pulldown (~45 KΩ), but GPIO4 is also
    // RTC_GPIO4. Its independent RTC-domain pulldown transistor (~45 KΩ) remains
    // active, and the two in parallel give ~22.5 KΩ to GND → idle at only 2.2 V.
    // rtc_gpio_pull{down,up}_dis() write RTCIO.pin[n] directly; no rtc_gpio_init needed.
    rtc_gpio_pulldown_dis(kProgGpio);
    rtc_gpio_pullup_dis(kProgGpio);

    gpio_set_level(kProgGpio, 1);    // release: NMOS off → 3.3 V via external pull-up

    gpio_set_level(kStrobGpio, 0);
}

void manchester_tx_send(uint64_t bits, uint8_t bit_count,
                        bool start_mark, bool end_mark, bool arm_rx) {
    gpio_set_level(kStrobGpio, 1);   // scope trigger: frame start

    if (start_mark) {
        gpio_set_level(kProgGpio, 0);
        esp_rom_delay_us(kMarkUs);   // 74 µs LOW mark before first bit
    }

    for (uint8_t i = 0; i < bit_count; ++i) {
        const uint8_t b = (bits >> (bit_count - 1u - i)) & 1u;
        // G.E. Thomas convention, MSB first:
        //   0 → LOW(T/2) then HIGH(T/2)   rising edge at mid-point
        //   1 → HIGH(T/2) then LOW(T/2)   falling edge at mid-point
        gpio_set_level(kProgGpio, b ? 1u : 0u);
        esp_rom_delay_us(s_half_us);
        gpio_set_level(kProgGpio, b ? 0u : 1u);
        esp_rom_delay_us(s_half_us);
    }

    if (end_mark) {
        gpio_set_level(kProgGpio, 0);
        esp_rom_delay_us(kMarkUs);   // 74 µs LOW mark after last bit
    }

    // Release line first, then arm RMT. PROG rises through the ~1.2 V digital
    // threshold within ~0.5 µs (10 kΩ pull-up, typical PCB cap). By the time
    // rmt_rx_start() executes the device hasn't had time to respond, so the RMT
    // starts from a clean idle-HIGH state and captures the device's very first edge.
    gpio_set_level(kProgGpio, 1);    // release: PROG returns to idle HIGH
    if (arm_rx && s_rx_rb) {
        rmt_rx_start(kRxChannel, true);  // arm while PROG is idle HIGH
        s_rx_armed = true;
    }
    gpio_set_level(kStrobGpio, 0);   // scope trigger: frame end
}

// ---------------------------------------------------------------------------
// RX — RMT_CHANNEL_4 Manchester decoder
// ---------------------------------------------------------------------------

// Pair up half-periods into Manchester bits. Returns bit count, 0 if any pair
// is invalid (two equal halves) or the count is odd/empty.
static uint8_t pair_decode(const uint8_t *halves, uint8_t n, uint64_t *out) {
    if (n < 2u || (n & 1u) || n > 128u) return 0u;
    uint64_t bits = 0;
    const uint8_t bit_count = n / 2u;
    for (uint8_t i = 0; i < bit_count; ++i) {
        const uint8_t a = halves[2u * i];
        const uint8_t b = halves[2u * i + 1u];
        if      (a == 0u && b == 1u) bits = (bits << 1u);        // 0: L then H
        else if (a == 1u && b == 0u) bits = (bits << 1u) | 1u;   // 1: H then L
        else return 0u;  // invalid Manchester pair
    }
    *out = bits;
    return bit_count;
}

// Expand RMT items to a half-period stream and decode Manchester bit pairs.
// Returns bit count on success, 0 on error.
//
// Observed device response capture (hardware, T=33 µs):
//   [HIGH turnaround ~25 µs, may be absent] [LOW start-mark ~30-50 µs, merges
//   with the first data half-period] [Manchester data] [released to High-Z]
//
// Artefacts handled:
//  - Leading turnaround HIGH: skipped (response data always starts LOW).
//  - Leading LOW mark: its length varies, so whether it swallowed the first
//    data half-period ('0' first bit) is ambiguous by duration alone. Both
//    alignments are tried; Manchester pair validation rejects the wrong one
//    (any double half-period in the stream anchors the alignment uniquely).
//  - Trailing idle: the final HIGH either merges into the idle tail (recorded
//    as a long pulse) or is dropped entirely (RMT idle terminator, duration 0).
//    If the half count ends odd, one closing half is appended and validated.
//  - A long LOW at the end (device end mark, if present) likewise ends the frame.
static uint8_t decode_rmt(const rmt_item32_t *items, size_t count,
                           uint32_t half_ticks, uint64_t *out) {
    const uint32_t thresh   = half_ticks + half_ticks / 2u;  // 1.5 × T/2: single vs double
    const uint32_t artifact = 3u * half_ticks;               // 1.5 × T: longer than any valid pulse

    uint8_t halves[92];
    uint8_t n = 0;
    bool started        = false;  // set at the first LOW pulse
    bool ambiguous_lead = false;  // first LOW contained a mark; alignment unknown
    bool ended          = false;
    uint8_t tail_level  = 1u;     // level of the terminating artefact (idle = HIGH)

    auto push = [&](uint32_t dur, uint8_t lv) {
        if (ended || dur == 0u || n >= 88u) return;

        if (!started) {
            if (lv != 0u) return;        // skip bus-turnaround HIGH before the response
            started = true;
            if (dur >= thresh) {         // long leading LOW = device start mark
                ambiguous_lead = true;   // may hide one data half; resolved below
                return;
            }
            halves[n++] = 0u;            // plain first half-period, no mark
            return;
        }

        if (dur >= artifact) {           // idle tail or device end mark: frame over
            tail_level = lv;
            ended = true;
            return;
        }

        halves[n++] = lv;                        // single half-period...
        if (dur >= thresh && n < 88u) halves[n++] = lv;  // ...or double
    };

    for (size_t i = 0; i < count && !ended; ++i) {
        push(items[i].duration0, items[i].level0);
        push(items[i].duration1, items[i].level1);
    }

    // Try both lead alignments (merged data half vs mark only). For each, fix
    // trailing parity by appending one tail-level half (the half that merged
    // into / was dropped with the terminating artefact). pair_decode() rejects
    // structurally wrong candidates.
    uint8_t buf[92];
    const uint8_t lead_options = ambiguous_lead ? 2u : 1u;
    for (uint8_t lead = 0; lead < lead_options; ++lead) {
        uint8_t m = 0;
        if (ambiguous_lead && lead == 0u) buf[m++] = 0u;  // candidate: mark hid one L half
        for (uint8_t i = 0; i < n && m < 90u; ++i) buf[m++] = halves[i];
        if ((m & 1u) && m < 90u) buf[m++] = tail_level;   // close the final pair
        const uint8_t bits = pair_decode(buf, m, out);
        if (bits) return bits;
    }
    return 0u;
}

void manchester_rx_init(uint32_t bit_period_us) {
    // T/2 in RMT ticks: (bit_period_us / 2) µs × 10 ticks/µs (at 10 MHz)
    s_rx_half_ticks = (bit_period_us / 2u) * kRmtClkMhz;  // 16 × 10 = 160 ticks

    rmt_config_t cfg = {};
    cfg.rmt_mode      = RMT_MODE_RX;
    cfg.channel       = kRxChannel;
    cfg.gpio_num      = kProgGpio;   // GPIO4, same pin as TX
    cfg.clk_div       = 8u;          // 80 MHz / 8 = 10 MHz → 0.1 µs/tick
    cfg.mem_block_num = 2u;          // 128 items; 44-bit frame needs ≤ 44 items
    cfg.rx_config.filter_en           = true;
    cfg.rx_config.filter_ticks_thresh = 40u;   // 4 µs glitch filter
    // idle_threshold must exceed the longest valid intra-frame pulse or capture
    // terminates mid-frame. Worst case is a device 74 µs mark merged with an
    // adjacent LOW data half-period: mark + T/2. Add 3×T of margin on top:
    //   T=33 µs → 740 + 990  = 1730 ticks (173 µs)
    //   T=100 µs → 740 + 3000 = 3740 ticks (374 µs)
    // End-of-frame is still detected fine — after the response PROG idles HIGH
    // for the full 2 s command period.
    cfg.rx_config.idle_threshold =
        (uint16_t)(kMarkUs * kRmtClkMhz + s_rx_half_ticks * 6u);

    rmt_config(&cfg);
    rmt_driver_install(kRxChannel, 512u, 0);
    rmt_get_ringbuf_handle(kRxChannel, &s_rx_rb);

    // rmt_config() called gpio_set_direction(INPUT) which cleared the output-enable
    // bit, breaking the bit-bang TX. Re-apply INPUT_OUTPUT_OD + disable RTC pulls.
    // The gpio_matrix_in() connection for RMT RX is in a separate register and
    // survives gpio_config() so capture still works after this.
    gpio_config_t io = {};
    io.pin_bit_mask  = (1ULL << kProgGpio);
    io.mode          = GPIO_MODE_INPUT_OUTPUT_OD;
    io.pull_up_en    = GPIO_PULLUP_DISABLE;
    io.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io.intr_type     = GPIO_INTR_DISABLE;
    gpio_config(&io);
    rtc_gpio_pulldown_dis(kProgGpio);
    rtc_gpio_pullup_dis(kProgGpio);
}

uint8_t manchester_rx_receive(uint64_t *out_bits, uint32_t timeout_ms) {
    if (!s_rx_rb) return 0u;

    // If caller already armed via manchester_tx_send(arm_rx=true), skip re-arming —
    // the device may already be responding and re-arming would clear captured data.
    if (!s_rx_armed) {
        rmt_rx_start(kRxChannel, true);  // arm if not done by TX path
    }
    s_rx_armed = false;

    size_t rx_size = 0;
    auto *items = static_cast<rmt_item32_t *>(
        xRingbufferReceive(s_rx_rb, &rx_size, pdMS_TO_TICKS(timeout_ms)));

    rmt_rx_stop(kRxChannel);
    s_last_count = 0;
    if (!items) return 0u;

    const size_t item_count = rx_size / sizeof(rmt_item32_t);
    s_last_count = (item_count < 96u) ? item_count : 96u;
    for (size_t i = 0; i < s_last_count; ++i) s_last_items[i] = items[i];

    const uint8_t n = decode_rmt(items, item_count, s_rx_half_ticks, out_bits);
    vRingbufferReturnItem(s_rx_rb, items);
    return n;
}

size_t manchester_rx_last_raw(uint32_t *out, size_t max_out) {
    const size_t n = (s_last_count < max_out) ? s_last_count : max_out;
    for (size_t i = 0; i < n; ++i) out[i] = s_last_items[i].val;
    return n;
}
