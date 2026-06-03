#include "crc3.h"

// Shift-register CRC-3: polynomial 0b1011, init 0b111, MSB first.
// A 4-bit working register is used so the overflow bit is handled implicitly —
// when bit 3 is set after shifting in a new bit, XOR with the full divisor
// clears bit 3 and applies the polynomial to bits [2:0].
uint8_t crc3_calc(uint64_t bits, int bitCount) {
    const uint8_t poly = 0b1011;
    uint8_t crc = 0b111;
    for (int i = bitCount - 1; i >= 0; --i) {
        uint8_t bit = (bits >> i) & 1u;
        crc = ((crc << 1) | bit) & 0xF;
        if (crc & 0x8) {
            crc ^= poly;
        }
    }
    return crc & 0x7;
}

// Write command: R/W[1] + ADDR[6] + DATA[32] = 39 bits
uint8_t crc3_write(uint8_t rw, uint8_t addr, uint32_t data) {
    uint64_t bits = ((uint64_t)(rw   & 0x01u) << 38) |
                    ((uint64_t)(addr & 0x3Fu) << 32) |
                    (uint64_t)data;
    return crc3_calc(bits, 39);
}

// Read request: R/W[1]=1 + ADDR[6] = 7 bits
uint8_t crc3_read_request(uint8_t addr) {
    uint64_t bits = (1ULL << 6) | (addr & 0x3Fu);
    return crc3_calc(bits, 7);
}

// Device read response: DATA[32] = 32 bits
uint8_t crc3_response(uint32_t data) {
    return crc3_calc((uint64_t)data, 32);
}
