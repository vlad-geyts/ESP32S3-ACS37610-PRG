#include "manchester.h"
#include <driver/rmt.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>

// Hardware pin assignments
static constexpr gpio_num_t    kProgGpio  = GPIO_NUM_4;   // PROG line (open-drain)
static constexpr gpio_num_t    kStrobGpio = GPIO_NUM_21;  // scope trigger (push-pull)
static constexpr rmt_channel_t kRmtChan   = RMT_CHANNEL_0;

// RMT clock = 80 MHz APB / 8 = 10 MHz → 1 tick = 0.1 µs.
// T/2 for 33 µs bit period = 16.5 µs = 165 ticks (exact, no rounding error).
static constexpr uint8_t  kClkDiv     = 8;
static constexpr uint32_t kTicksPerUs = 10u;

static uint16_t s_half_ticks = 0;  // T/2 in RMT ticks

void manchester_tx_init(uint32_t bit_period_us) {
    s_half_ticks = static_cast<uint16_t>((bit_period_us * kTicksPerUs) / 2u);

    rmt_config_t cfg = {};
    cfg.rmt_mode                 = RMT_MODE_TX;
    cfg.channel                  = kRmtChan;
    cfg.gpio_num                 = kProgGpio;
    cfg.clk_div                  = kClkDiv;
    cfg.mem_block_num            = 1;
    cfg.tx_config.loop_en        = false;
    cfg.tx_config.carrier_en     = false;
    cfg.tx_config.idle_output_en = true;
    // Idle HIGH: PROG line released between frames — pull-up holds it high
    cfg.tx_config.idle_level     = RMT_IDLE_LEVEL_HIGH;

    ESP_ERROR_CHECK(rmt_config(&cfg));
    ESP_ERROR_CHECK(rmt_driver_install(kRmtChan, 0, 0));

    // Open-drain on PROG: RMT HIGH → line released (pull-up provides HIGH),
    //                      RMT LOW  → GPIO actively pulls the line low.
    // Write pad_driver bit directly — equivalent to gpio_od_enable() but without
    // the gpio_matrix_out(SIG_GPIO_OUT_IDX) side-effect that gpio_set_direction()
    // triggers, which would disconnect the RMT TX signal from this pin.
    GPIO.pin[kProgGpio].pad_driver = 1;

    // Disable internal pulldown: with OD mode and RMT idle HIGH (released),
    // an active internal PD fights the external 10K pull-up and drops idle
    // voltage to ~2V. gpio_pulldown_dis writes only IO_MUX — no GPIO matrix effect.
    gpio_pulldown_dis(kProgGpio);

    // ESP32-S3 legacy RMT: idle_output_en does not engage reliably on this
    // peripheral revision. Park the line HIGH by transmitting a 1-tick HIGH
    // symbol. The RMT holds its last output level after TX, so PROG stays HIGH.
    rmt_item32_t park = {};
    park.duration0 = 1;  park.level0 = 1;  // 0.1 µs HIGH
    park.duration1 = 0;  park.level1 = 0;  // end marker
    rmt_write_items(kRmtChan, &park, 1, true);

    gpio_set_level(kStrobGpio, 0);
}

void manchester_tx_send(uint64_t bits, uint8_t bit_count) {
    // One rmt_item32_t per Manchester bit (two half-period pulses per item).
    // G.E. Thomas convention, MSB first:
    //   bit 0 → LOW(T/2)  then HIGH(T/2)   rising edge at mid-point
    //   bit 1 → HIGH(T/2) then LOW(T/2)    falling edge at mid-point
    // +1 for the parking HIGH symbol appended after the Manchester frame.
    rmt_item32_t syms[45] = {};

    for (uint8_t i = 0; i < bit_count; ++i) {
        uint8_t bit     = (bits >> (bit_count - 1u - i)) & 1u;
        syms[i].duration0 = s_half_ticks;
        syms[i].level0    = bit ? 1u : 0u;
        syms[i].duration1 = s_half_ticks;
        syms[i].level1    = bit ? 0u : 1u;
    }
    // Parking symbol: 1-tick HIGH followed by end marker. The RMT holds the last
    // output level (HIGH) after TX, releasing the PROG line via the external pull-up.
    syms[bit_count].duration0 = 1;  syms[bit_count].level0 = 1;
    syms[bit_count].duration1 = 0;  syms[bit_count].level1 = 0;

    gpio_set_level(kStrobGpio, 1);                                  // scope trigger: frame start
    rmt_write_items(kRmtChan, syms, bit_count + 1u, true);          // +1 includes parking symbol
    gpio_set_level(kStrobGpio, 0);                                  // scope trigger: frame end
}
