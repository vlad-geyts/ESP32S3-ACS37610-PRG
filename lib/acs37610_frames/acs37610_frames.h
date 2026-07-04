#pragma once
#include <cstdint>

// Pure frame builders + response parsing for the ACS37610 serial protocol
// (dev plan v4.1 §2). No hardware dependencies — natively unit-testable,
// following the lib/crc3 pattern.

// Error codes mirror the host protocol ERR strings (GUI plan §3.3).
enum class AcsError : uint8_t {
    None = 0,
    Arg,      // malformed command / bad address or data
    Port,     // READ/WRITE attempted before AUTH opened the device port
    Timeout,  // no device response within RX window
    Crc,      // CRC-3 mismatch on device response
    Ecc,      // EEPROM read/verify reported ECC fault (DATA[27:26])
    Verify,   // EEPROM write read-back value != written value
    Locked,   // WRITE_LOCK guard tripped / write refused
    PwrOff,   // command requires DUT power but rail is off
};

enum class AcsEcc : uint8_t { Ok = 0, Fail, NotApplicable };

struct AcsReadResult {
    uint32_t data;  // full 32-bit response payload
    AcsEcc   ecc;   // decoded from DATA[27:26] on EEPROM reads, NA otherwise
};

// 44-bit write frame: SYNC(00) | R/W=0 | ADDR[6] | DATA[32] | CRC[3]
uint64_t acs_build_write_frame(uint8_t addr, uint32_t data);

// 12-bit read request: SYNC(00) | R/W=1 | ADDR[6] | CRC[3]
uint16_t acs_build_read_frame(uint8_t addr);

// Validate a captured device response: SYNC[2] | DATA[32] | CRC[3] = 37 bits,
// but leading sync bits may be swallowed by the device start mark, so 35-37
// bits are accepted. DATA and CRC anchor at the LSB end. Verifies CRC-3 over
// DATA[32] (hardware-pinned 2026-07-03). Returns None or Crc.
AcsError acs_parse_response(uint64_t frame, uint8_t bit_count, uint32_t *data);

// True if addr is an EEPROM register whose read response carries ECC status
// in DATA[27:26]: EE_CUST0 (0x09), EE_CUST1 (0x0A), EE_CUST2 (0x0B).
// FAULT_STATUS (0x20) and shadow registers are volatile — bits [27:26] there
// are data (e.g. TEMP_OUT), not ECC.
bool acs_addr_has_ecc(uint8_t addr);

// Decode the ECC status of a read: NA for non-EEPROM addresses; for EEPROM
// reads DATA[27:26] == 0b00 means no error. (Exact semantics of the nonzero
// codes — corrected vs uncorrectable — TBD against the Allegro datasheet;
// anything nonzero is reported as FAIL.)
AcsEcc acs_decode_ecc(uint8_t addr, uint32_t data);
