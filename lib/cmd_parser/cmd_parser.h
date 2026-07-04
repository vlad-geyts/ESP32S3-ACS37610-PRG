#pragma once
#include <cstddef>
#include <cstdint>
#include "acs37610_frames.h"   // AcsError, AcsReadResult, AcsEcc

// ASCII line-command parser implementing the host protocol (GUI plan §3).
// Hardware-independent: the action handlers are injected at init so native
// unit tests can substitute mocks (GUI plan §4.5).
//
// Commands (case-insensitive, hex numbers with or without 0x):
//   *IDN? | PING            -> ID ACS37610-PRG <fw_ver>
//   STATUS                  -> STATUS PWR=<0|1> PORT=<0|1> ERR=<code>
//   PWRON / PWROFF          -> OK
//   AUTH                    -> OK
//   READ <addr>             -> DATA <addr> <hex8> ECC=<OK|FAIL|NA>
//   WRAM <addr> <data>      -> OK
//   WEEP <addr> <data> [FORCE] -> OK VERIFY=OK
// Every failure answers a single "ERR <code>" line (§3.3).

struct CmdHandlers {
    void     (*power)(bool on);
    bool     (*power_state)();
    bool     (*port_open)();
    AcsError (*auth)();
    AcsError (*read_reg)(uint8_t addr, AcsReadResult *out);
    AcsError (*write_ram)(uint8_t addr, uint32_t data26);
    AcsError (*write_eeprom)(uint8_t addr, uint32_t data26, bool force);
};

void cmd_parser_init(const CmdHandlers *handlers, const char *fw_version);

// Process one command line; writes exactly one response line (no '\n') into
// out. Safe to call with any input — malformed lines answer "ERR ARG".
void cmd_parser_process(const char *line, char *out, size_t out_cap);
