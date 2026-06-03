#pragma once
#include <cstdint>

// CRC-3 for ACS37610 Manchester protocol.
// Polynomial: g(x) = x^3 + x + 1 (0b1011), initial value 0b111, MSB first.
// SYNC bits are excluded from all calculations.

// Core engine — feeds bitCount bits from 'bits' (MSB first) into the CRC register.
uint8_t crc3_calc(uint64_t bits, int bitCount);

// R/W[1] + ADDR[6] + DATA[32] = 39 bits  (write commands)
uint8_t crc3_write(uint8_t rw, uint8_t addr, uint32_t data);

// R/W[1]=1 + ADDR[6] = 7 bits  (read request from controller)
uint8_t crc3_read_request(uint8_t addr);

// DATA[32] = 32 bits  (device read response)
uint8_t crc3_response(uint32_t data);
