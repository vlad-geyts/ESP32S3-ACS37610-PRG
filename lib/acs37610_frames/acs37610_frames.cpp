#include "acs37610_frames.h"
#include "crc3.h"

// SYNC[2]=00 occupies the two MSBs of every frame and is excluded from the
// CRC (dev plan v4.1 §2.3a); building it is therefore just field packing.

uint64_t acs_build_write_frame(uint8_t addr, uint32_t data) {
    const uint8_t crc = crc3_write(0, addr, data);
    return ((uint64_t)(addr & 0x3Fu) << 35) |
           ((uint64_t)data           <<  3) |
           (uint64_t)crc;
}

uint16_t acs_build_read_frame(uint8_t addr) {
    const uint8_t crc = crc3_read_request(addr);
    return (uint16_t)((1u << 9) | ((addr & 0x3Fu) << 3) | crc);
}

AcsError acs_parse_response(uint64_t frame, uint8_t bit_count, uint32_t *data) {
    if (bit_count < 35u || bit_count > 37u) return AcsError::Crc;
    const uint32_t payload = (uint32_t)((frame >> 3) & 0xFFFFFFFFull);
    const uint8_t  rx_crc  = (uint8_t)(frame & 0x7u);
    if (crc3_response(payload) != rx_crc) return AcsError::Crc;
    *data = payload;
    return AcsError::None;
}

bool acs_addr_has_ecc(uint8_t addr) {
    return addr == 0x09u || addr == 0x0Au || addr == 0x0Bu;
}

AcsEcc acs_decode_ecc(uint8_t addr, uint32_t data) {
    if (!acs_addr_has_ecc(addr)) return AcsEcc::NotApplicable;
    return (((data >> 26) & 0x3u) == 0u) ? AcsEcc::Ok : AcsEcc::Fail;
}
