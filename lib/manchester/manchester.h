#pragma once
#include <cstdint>

// Manchester TX/RX driver for ACS37610 PROG line (GPIO4, open-drain, 3.3 V).
// G.E. Thomas convention, MSB first. External 10K pull-up defines idle HIGH.
//
// TX: bit-banged via gpio_set_level() + esp_rom_delay_us().
//   0 → LOW(T/2)  then HIGH(T/2)   (rising edge at mid-point)
//   1 → HIGH(T/2) then LOW(T/2)    (falling edge at mid-point)
//
// RX: RMT_CHANNEL_1 captures transitions; idle_threshold detects end-of-frame.
//
// GPIO21 (STROB_OUT) is toggled HIGH for the duration of each TX frame.

// Initialise Manchester TX. Call once from setup().
// bit_period_us: full bit period T (default 33 µs → T/2=16 µs → 31.25 kbps).
void manchester_tx_init(uint32_t bit_period_us = 33);

// Transmit bit_count bits from 'bits', MSB first. Blocks until done.
// bit_count: 1–44 (ACS37610 max frame = SYNC[2]+R/W[1]+ADDR[6]+DATA[32]+CRC[3]).
void manchester_tx_send(uint64_t bits, uint8_t bit_count);

// Initialise Manchester RX. Call once from setup(), after manchester_tx_init().
// Configures RMT_CHANNEL_1 on GPIO4 and re-applies INPUT_OUTPUT_OD so TX still works.
void manchester_rx_init(uint32_t bit_period_us = 33);

// Receive one Manchester frame. Call immediately after manchester_tx_send() returns.
// The ACS37610 responds within 74 µs; this function arms RMT RX, waits for the
// idle-threshold end-of-frame, decodes, and returns the bit count (0 = timeout/error).
uint8_t manchester_rx_receive(uint64_t *out_bits, uint32_t timeout_ms = 100);
