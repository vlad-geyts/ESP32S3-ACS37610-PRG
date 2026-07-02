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

// Expand RMT items to a half-period stream, stripping device start/end marks,
// then decode Manchester bit pairs. Returns bit count on success, 0 on error.
//
// The ACS37610 wraps its response with 74 µs LOW marks:
//   [LOW 74µs mark] [Manchester data] [LOW 74µs mark] [released to High-Z]
//
// The leading mark merges with the first Manchester half-period in the RMT
// capture (both are LOW and contiguous). The algorithm detects any LOW pulse
// that is longer than mark_thresh as a mark-containing pulse, strips the mark
// portion, and emits only the remaining half-period (if any). This correctly
// handles both the leading merge case and the trailing case where the mark may
// or may not merge with the last bit's half-period.
//
// Mark detection is ONLY enabled when mark_ticks > 2×half_ticks (i.e. T < 74 µs,
// production speed). At debug speed (T=100 µs, mark=74 µs < T), the mark
// overlaps the double-half-period range so detection is ambiguous and disabled.
static uint8_t decode_rmt(const rmt_item32_t *items, size_t count,
                           uint32_t half_ticks, uint64_t *out) {
    const uint32_t thresh      = half_ticks + half_ticks / 2u;    // 1.5 × T/2: single vs double half-period
    const uint32_t mark_ticks  = kMarkUs * kRmtClkMhz;            // 74 µs in RMT ticks (740 at 10 MHz)
    // Only apply mark detection when the mark is clearly longer than a full bit (T).
    // At T=33 µs: mark(740) > 2×half(160)=320 → enable.
    // At T=100 µs: mark(740) < 2×half(500)=1000 → disable to avoid stripping real data.
    const bool     marks_enabled = (mark_ticks > 2u * half_ticks);
    const uint32_t mark_thresh   = marks_enabled ? (mark_ticks - half_ticks / 2u) : UINT32_MAX;

    uint8_t halves[90];   // max 44 bits × 2 = 88 half-periods
    uint8_t n = 0;

    for (size_t i = 0; i < count && n < 88u; ++i) {
        auto push = [&](uint32_t dur, uint8_t lv) {
            if (dur == 0u || n >= 88u) return;

            if (lv == 0u && dur >= mark_thresh) {
                // This LOW pulse contains a 74 µs mark. Subtract the mark and check
                // if a half-period was merged in (happens when last bit = 1 or at start).
                const uint32_t remainder = (dur >= mark_ticks) ? (dur - mark_ticks) : 0u;
                if (remainder >= half_ticks / 2u && n < 88u) {
                    halves[n++] = 0u;   // one LOW half-period merged with mark
                }
                return;   // skip the mark itself
            }

            // Normal Manchester pulse: 1 half-period, or 2 if duration ≈ T.
            halves[n++] = lv;
            if (dur >= thresh && n < 88u) halves[n++] = lv;
        };

        push(items[i].duration0, items[i].level0);
        push(items[i].duration1, items[i].level1);
    }

    if (n < 2u || (n & 1u)) return 0u;  // must be even (2 half-periods per bit)

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
    // idle_threshold must be > T (max consecutive HIGH in Manchester = one full bit period)
    // so it doesn't fire mid-frame on a double-half-period HIGH boundary (e.g. 0→1 bit pair).
    // Using 3×T guarantees this at any speed: at T=33 µs → 990 ticks (99 µs);
    //                                          at T=100 µs → 3000 ticks (300 µs).
    // The device responds within a few µs of PROG going HIGH (well under 3×T).
    cfg.rx_config.idle_threshold = (uint16_t)(s_rx_half_ticks * 6u); // = 3×T

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
    if (!items) return 0u;

    const uint8_t n = decode_rmt(items, rx_size / sizeof(rmt_item32_t),
                                  s_rx_half_ticks, out_bits);
    vRingbufferReturnItem(s_rx_rb, items);
    return n;
}
