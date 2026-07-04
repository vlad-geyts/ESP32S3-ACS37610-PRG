#include "cmd_parser.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

static const CmdHandlers *s_h          = nullptr;
static const char        *s_fw_version = "0.0.0";
// Result of the last action command (PWRON/PWROFF/AUTH/READ/WRAM/WEEP),
// reported by STATUS. Query commands (STATUS, *IDN?) do not modify it.
static AcsError           s_last_err   = AcsError::None;

static const char *err_name(AcsError e) {
    switch (e) {
        case AcsError::None:    return "NONE";
        case AcsError::Arg:     return "ARG";
        case AcsError::Port:    return "PORT";
        case AcsError::Timeout: return "TIMEOUT";
        case AcsError::Crc:     return "CRC";
        case AcsError::Ecc:     return "ECC";
        case AcsError::Verify:  return "VERIFY";
        case AcsError::Locked:  return "LOCKED";
        case AcsError::PwrOff:  return "PWROFF";
    }
    return "NONE";
}

static const char *ecc_name(AcsEcc e) {
    switch (e) {
        case AcsEcc::Ok:            return "OK";
        case AcsEcc::Fail:          return "FAIL";
        case AcsEcc::NotApplicable: return "NA";
    }
    return "NA";
}

// Parse a hex token ("1A" or "0x1A"), bounded by max. Returns false on any
// non-hex garbage, overflow, or out-of-range value.
static bool parse_hex(const char *tok, uint32_t max, uint32_t *out) {
    if (!tok || !*tok) return false;
    char *end = nullptr;
    const unsigned long v = strtoul(tok, &end, 16);
    if (end == tok || *end != '\0') return false;
    if (v > max) return false;
    *out = (uint32_t)v;
    return true;
}

void cmd_parser_init(const CmdHandlers *handlers, const char *fw_version) {
    s_h          = handlers;
    s_fw_version = fw_version;
    s_last_err   = AcsError::None;
}

void cmd_parser_process(const char *line, char *out, size_t out_cap) {
    if (!s_h || !out || out_cap == 0) return;

    // Tokenize an uppercased local copy: keyword + up to 3 arguments.
    char buf[96];
    strncpy(buf, line ? line : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf; *p; ++p) *p = (char)toupper((unsigned char)*p);

    const char *tok[4] = {nullptr, nullptr, nullptr, nullptr};
    int ntok = 0;
    for (char *p = strtok(buf, " \t"); p && ntok < 4; p = strtok(nullptr, " \t")) {
        tok[ntok++] = p;
    }

    const char *cmd = tok[0];
    if (!cmd) { snprintf(out, out_cap, "ERR ARG"); return; }

    // --- Query commands (do not touch s_last_err) ---
    if (strcmp(cmd, "*IDN?") == 0 || strcmp(cmd, "PING") == 0) {
        snprintf(out, out_cap, "ID ACS37610-PRG %s", s_fw_version);
        return;
    }
    if (strcmp(cmd, "STATUS") == 0) {
        snprintf(out, out_cap, "STATUS PWR=%d PORT=%d ERR=%s",
                 s_h->power_state() ? 1 : 0, s_h->port_open() ? 1 : 0,
                 err_name(s_last_err));
        return;
    }

    // --- Action commands ---
    AcsError err = AcsError::Arg;

    if (strcmp(cmd, "PWRON") == 0 && ntok == 1) {
        s_h->power(true);
        err = AcsError::None;
        snprintf(out, out_cap, "OK");
    } else if (strcmp(cmd, "PWROFF") == 0 && ntok == 1) {
        s_h->power(false);
        err = AcsError::None;
        snprintf(out, out_cap, "OK");
    } else if (strcmp(cmd, "AUTH") == 0 && ntok == 1) {
        err = s_h->auth();
        if (err == AcsError::None) snprintf(out, out_cap, "OK");
    } else if (strcmp(cmd, "READ") == 0 && ntok == 2) {
        uint32_t addr = 0;
        if (parse_hex(tok[1], 0x3Fu, &addr)) {
            AcsReadResult r = {};
            err = s_h->read_reg((uint8_t)addr, &r);
            if (err == AcsError::None) {
                snprintf(out, out_cap, "DATA %02X 0x%08X ECC=%s",
                         (unsigned)addr, (unsigned)r.data, ecc_name(r.ecc));
            }
        }
    } else if (strcmp(cmd, "WRAM") == 0 && ntok == 3) {
        uint32_t addr = 0, data = 0;
        if (parse_hex(tok[1], 0x3Fu, &addr) &&
            parse_hex(tok[2], 0x03FFFFFFu, &data)) {
            err = s_h->write_ram((uint8_t)addr, data);
            if (err == AcsError::None) snprintf(out, out_cap, "OK");
        }
    } else if (strcmp(cmd, "WEEP") == 0 && (ntok == 3 || ntok == 4)) {
        uint32_t addr = 0, data = 0;
        const bool force    = (ntok == 4) && strcmp(tok[3], "FORCE") == 0;
        const bool tok3_bad = (ntok == 4) && !force;
        if (!tok3_bad && parse_hex(tok[1], 0x3Fu, &addr) &&
            parse_hex(tok[2], 0x03FFFFFFu, &data)) {
            err = s_h->write_eeprom((uint8_t)addr, data, force);
            if (err == AcsError::None) snprintf(out, out_cap, "OK VERIFY=OK");
        }
    }

    s_last_err = err;
    if (err != AcsError::None) {
        snprintf(out, out_cap, "ERR %s", err_name(err));
    }
}
