#pragma once
#include <cstdint>

// Manchester TX/RX driver for ACS37610 PROG line.
// G.E. Thomas convention, MSB first, open-drain GPIO4.
//
// Bit encoding (one rmt_symbol_word_t per bit, T/2 + T/2):
//   0 → LOW(T/2)  then HIGH(T/2)   (rising edge at mid-point)
//   1 → HIGH(T/2) then LOW(T/2)    (falling edge at mid-point)
//
// GPIO21 (STROB_OUT) is toggled HIGH for the duration of each frame
// to provide an oscilloscope trigger.

// Initialise RMT TX channel. Call once from setup().
// bit_period_us: full bit period T in microseconds (default 33 = 30 kbps).
void manchester_tx_init(uint32_t bit_period_us = 33);

// Transmit bit_count bits from 'bits', MSB first. Blocks until done.
// bit_count: 1–44 (ACS37610 max frame = SYNC[2]+R/W[1]+ADDR[6]+DATA[32]+CRC[3]).
void manchester_tx_send(uint64_t bits, uint8_t bit_count);
