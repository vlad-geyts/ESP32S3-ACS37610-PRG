#pragma once
#include <cstdint>

// Manchester TX driver for ACS37610 PROG line (bit-banged, open-drain GPIO4).
// G.E. Thomas convention, MSB first. External 10K pull-up to 3.3V defines idle HIGH.
//
// Bit encoding (T/2 per half-period via esp_rom_delay_us + gpio_set_level):
//   0 → LOW(T/2)  then HIGH(T/2)   (rising edge at mid-point)
//   1 → HIGH(T/2) then LOW(T/2)    (falling edge at mid-point)
//
// GPIO21 (STROB_OUT) is toggled HIGH for the duration of each TX frame
// to provide an oscilloscope trigger.

// Initialise Manchester TX. Call once from setup().
// bit_period_us: full bit period T in microseconds (default 33 µs = 30 kbps).
// Integer T/2 = 16 µs → actual T = 32 µs (31.25 kbps), within ACS37610 tolerance.
void manchester_tx_init(uint32_t bit_period_us = 33);

// Transmit bit_count bits from 'bits', MSB first. Blocks until done.
// bit_count: 1–44 (ACS37610 max frame = SYNC[2]+R/W[1]+ADDR[6]+DATA[32]+CRC[3]).
void manchester_tx_send(uint64_t bits, uint8_t bit_count);
