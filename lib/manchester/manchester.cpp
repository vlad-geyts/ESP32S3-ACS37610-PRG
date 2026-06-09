#include "manchester.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>  // rtc_gpio_pulldown_dis / rtc_gpio_pullup_dis
#include <esp_rom_sys.h>    // esp_rom_delay_us

static constexpr gpio_num_t kProgGpio  = GPIO_NUM_4;
static constexpr gpio_num_t kStrobGpio = GPIO_NUM_21;

// T/2 in microseconds, derived from bit_period_us in manchester_tx_init().
static uint32_t s_half_us = 0;

void manchester_tx_init(uint32_t bit_period_us) {
    s_half_us = bit_period_us / 2u;   // 33 / 2 = 16 µs → T ≈ 32 µs (31.25 kbps)

    // Open-drain output + input on PROG.
    // INPUT_OUTPUT_OD: gpio_set_level(1) → NMOS off → 3.3V via external 10K pull-up.
    //                  gpio_set_level(0) → NMOS on  → GND.
    // PULLDOWN_DISABLE: prevents internal ~45 K PD from dividing the idle voltage below 3.3V.
    // INPUT mode retained so the line can be read back for RX (Phase 4).
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

void manchester_tx_send(uint64_t bits, uint8_t bit_count) {
    gpio_set_level(kStrobGpio, 1);   // scope trigger: frame start

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
    gpio_set_level(kProgGpio, 1);    // release line: PROG returns to idle HIGH

    gpio_set_level(kStrobGpio, 0);   // scope trigger: frame end
}
