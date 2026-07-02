#include "crc3.h"

// Standard feedback shift-register CRC-3: polynomial x³+x+1, init 0b111, MSB first.
// For each input bit, feedback = MSB(crc) XOR input_bit. The register is shifted
// left and XORed with the lower polynomial bits (0b011) only when feedback is 1.
// This is the correct GF(2) long-division formulation; the previous version
// incorrectly ORed the input bit directly into the register (gave wrong results).
uint8_t crc3_calc(uint64_t bits, int bitCount) {
    const uint8_t poly = 0b011;   // lower bits of x³+x+1 (leading x³ term is implicit)
    uint8_t crc = 0b111;          // init = all ones
    for (int i = bitCount - 1; i >= 0; --i) {
        const uint8_t bit      = (bits >> i) & 1u;
        const uint8_t feedback = ((crc >> 2) & 1u) ^ bit;  // MSB(crc) XOR input
        crc = (crc << 1) & 0b111u;                          // shift out MSB
        if (feedback) {
            crc ^= poly;
        }
    }
    return crc;
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
