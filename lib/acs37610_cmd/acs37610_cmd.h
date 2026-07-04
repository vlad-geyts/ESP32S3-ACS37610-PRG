#pragma once
#include <cstdint>
#include "acs37610_frames.h"

// Hardware transaction layer: drives the ACS37610 over the PROG line via the
// manchester TX/RX driver, tracks power/port state, enforces protocol timing
// (post-AUTH settle, EEPROM t_w, verify-after-write). Firmware-only — not
// built for native tests (those cover acs37610_frames and cmd_parser).

// Record the PWR_EN GPIO (active LOW). Call after gpioConfig(); the DUT rail
// is expected to be OFF at boot — the host enables it with PWRON.
void acs_init(int pwr_en_gpio);

// Drive the DUT 3.3 V rail. Turning it off closes the device serial port
// (the device loses its port-open state on power loss).
void acs_power(bool on);
bool acs_power_state();
bool acs_port_open();

// Send the Access Code (0x2C413736 -> 0x31) and wait the post-AUTH settle.
// Opens the device serial port. Requires power.
AcsError acs_auth();

// Read a register: 12-bit request, capture + decode the 37-bit response,
// verify CRC, decode ECC status for EEPROM addresses.
AcsError acs_read(uint8_t addr, AcsReadResult *out);

// Write a volatile shadow/RAM register. data26 is the 26-bit payload
// (DATA[31:26] transmitted as 0).
AcsError acs_write_ram(uint8_t addr, uint32_t data26);

// Write an EEPROM register: write, wait t_w (35 ms), read back, compare
// DATA[25:0], check ECC. WRITE_LOCK guard: refuses EE_CUST0 (0x09) data with
// bit[25] set unless force=true (GUI plan §4.3 — defense in depth).
AcsError acs_write_eeprom(uint8_t addr, uint32_t data26, bool force);
