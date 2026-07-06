#include "acs37610_cmd.h"
#include "manchester.h"
#include <Arduino.h>
#include <esp_rom_sys.h>

static int  s_pwr_gpio  = -1;
static bool s_pwr_on    = false;
static bool s_port_open = false;

static constexpr uint8_t  kAccessAddr   = 0x31;
static constexpr uint32_t kAccessData   = 0x2C413736UL;
// Post-AUTH settle: spec says 90-120 µs (v4.1 §2.4); 500 µs is the
// hardware-validated value used throughout Phase 3 bring-up.
static constexpr uint32_t kSettleUs     = 500;
// EEPROM write time t_w is 25-35 ms (v4.1 §2.5.2). The device pulses PROG LOW
// (~1 Tbit) when internal programming completes — observed at ~33 ms (scope,
// 2026-07-04). 40 ms keeps the verify read clear of that completion pulse even
// for a device at the slow end of t_w.
static constexpr uint32_t kTwMs         = 40;
static constexpr uint32_t kRxTimeoutMs  = 200;
static constexpr uint32_t kData26Mask   = 0x03FFFFFFUL;
static constexpr uint8_t  kWriteLockAddr = 0x09;          // EE_CUST0
static constexpr uint32_t kWriteLockBit  = 1UL << 25;     // WRITE_LOCK[25]

void acs_init(int pwr_en_gpio) {
    s_pwr_gpio  = pwr_en_gpio;
    s_pwr_on    = false;
    s_port_open = false;
}

void acs_power(bool on) {
    digitalWrite(s_pwr_gpio, on ? LOW : HIGH);   // PWR_EN is active LOW
    s_pwr_on = on;
    if (!on) s_port_open = false;   // device port closes on power loss
}

bool acs_power_state() { return s_pwr_on; }
bool acs_port_open()   { return s_port_open; }

AcsError acs_auth() {
    if (!s_pwr_on) return AcsError::PwrOff;
    const uint64_t frame = acs_build_write_frame(kAccessAddr, kAccessData);
    manchester_tx_send(frame, 44, /*start_mark=*/false, /*end_mark=*/false);
    esp_rom_delay_us(kSettleUs);
    s_port_open = true;
    return AcsError::None;
}

AcsError acs_read(uint8_t addr, AcsReadResult *out) {
    if (!s_pwr_on)     return AcsError::PwrOff;
    if (!s_port_open)  return AcsError::Port;
    if (addr > 0x3Fu)  return AcsError::Arg;

    const uint16_t frame = acs_build_read_frame(addr);
    // arm_rx: RMT armed inside TX right as PROG is released — the device
    // starts responding ~25 µs later (hardware-measured).
    manchester_tx_send(frame, 12, /*start_mark=*/false, /*end_mark=*/false,
                       /*arm_rx=*/true);

    uint64_t response = 0;
    const uint8_t bits = manchester_rx_receive(&response, kRxTimeoutMs);
    if (bits == 0) return AcsError::Timeout;

    uint32_t data = 0;
    const AcsError err = acs_parse_response(response, bits, &data);
    if (err != AcsError::None) return err;

    out->data = data;
    out->ecc  = acs_decode_ecc(addr, data);
    return AcsError::None;
}

AcsError acs_write_ram(uint8_t addr, uint32_t data26) {
    if (!s_pwr_on)              return AcsError::PwrOff;
    if (!s_port_open)           return AcsError::Port;
    if (addr > 0x3Fu)           return AcsError::Arg;
    if (data26 & ~kData26Mask)  return AcsError::Arg;

    const uint64_t frame = acs_build_write_frame(addr, data26);
    manchester_tx_send(frame, 44, /*start_mark=*/true, /*end_mark=*/true);
    return AcsError::None;
}

AcsError acs_write_eeprom(uint8_t addr, uint32_t data26, bool force) {
    if (!s_pwr_on)              return AcsError::PwrOff;
    if (!s_port_open)           return AcsError::Port;
    if (addr > 0x3Fu)           return AcsError::Arg;
    if (data26 & ~kData26Mask)  return AcsError::Arg;
    if (addr == kWriteLockAddr && (data26 & kWriteLockBit) && !force) {
        return AcsError::Locked;   // WRITE_LOCK is one-time-programmable
    }

    const uint64_t frame = acs_build_write_frame(addr, data26);
    manchester_tx_send(frame, 44, /*start_mark=*/true, /*end_mark=*/true);
    delay(kTwMs);   // t_w: EEPROM programming time before any next operation

    AcsReadResult rb = {};
    const AcsError err = acs_read(addr, &rb);
    if (err != AcsError::None)              return err;
    if ((rb.data & kData26Mask) != data26)  return AcsError::Verify;
    if (rb.ecc == AcsEcc::Fail)             return AcsError::Ecc;
    return AcsError::None;
}
