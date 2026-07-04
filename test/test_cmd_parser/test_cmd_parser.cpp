#include <unity.h>
#include <cstring>
#include "cmd_parser.h"

// ---------------------------------------------------------------------------
// Mock handlers emulating the acs37610_cmd state machine (GUI plan §4.5).
// ---------------------------------------------------------------------------

static bool     mock_pwr;
static bool     mock_port;
static uint32_t mock_read_data;
static AcsEcc   mock_read_ecc;
static AcsError mock_weep_result;
static uint8_t  last_addr;
static uint32_t last_data;
static bool     last_force;

static void     m_power(bool on)      { mock_pwr = on; if (!on) mock_port = false; }
static bool     m_power_state()       { return mock_pwr; }
static bool     m_port_open()         { return mock_port; }
static AcsError m_auth() {
    if (!mock_pwr) return AcsError::PwrOff;
    mock_port = true;
    return AcsError::None;
}
static AcsError m_read(uint8_t addr, AcsReadResult *out) {
    if (!mock_pwr)  return AcsError::PwrOff;
    if (!mock_port) return AcsError::Port;
    last_addr = addr;
    out->data = mock_read_data;
    out->ecc  = mock_read_ecc;
    return AcsError::None;
}
static AcsError m_wram(uint8_t addr, uint32_t data) {
    if (!mock_pwr)  return AcsError::PwrOff;
    if (!mock_port) return AcsError::Port;
    last_addr = addr; last_data = data;
    return AcsError::None;
}
static AcsError m_weep(uint8_t addr, uint32_t data, bool force) {
    if (!mock_pwr)  return AcsError::PwrOff;
    if (!mock_port) return AcsError::Port;
    last_addr = addr; last_data = data; last_force = force;
    if (addr == 0x09 && (data & (1ul << 25)) && !force) return AcsError::Locked;
    return mock_weep_result;
}

static const CmdHandlers kMock = {
    m_power, m_power_state, m_port_open, m_auth, m_read, m_wram, m_weep,
};

static char out[128];
static const char *run(const char *line) {
    cmd_parser_process(line, out, sizeof(out));
    return out;
}

void setUp() {
    mock_pwr = false; mock_port = false;
    mock_read_data = 0; mock_read_ecc = AcsEcc::NotApplicable;
    mock_weep_result = AcsError::None;
    last_addr = 0xFF; last_data = 0; last_force = false;
    cmd_parser_init(&kMock, "1.0.0");
}
void tearDown() {}

// --- Identity & status ---

void test_idn() {
    TEST_ASSERT_EQUAL_STRING("ID ACS37610-PRG 1.0.0", run("*IDN?"));
    TEST_ASSERT_EQUAL_STRING("ID ACS37610-PRG 1.0.0", run("PING"));
    TEST_ASSERT_EQUAL_STRING("ID ACS37610-PRG 1.0.0", run("  *idn?  "));
}

void test_status_reflects_state_and_last_error() {
    TEST_ASSERT_EQUAL_STRING("STATUS PWR=0 PORT=0 ERR=NONE", run("STATUS"));
    run("AUTH");   // fails: power off
    TEST_ASSERT_EQUAL_STRING("STATUS PWR=0 PORT=0 ERR=PWROFF", run("STATUS"));
    run("PWRON");
    run("AUTH");
    TEST_ASSERT_EQUAL_STRING("STATUS PWR=1 PORT=1 ERR=NONE", run("STATUS"));
}

// --- Power & auth sequencing ---

void test_power_on_off() {
    TEST_ASSERT_EQUAL_STRING("OK", run("PWRON"));
    TEST_ASSERT_TRUE(mock_pwr);
    TEST_ASSERT_EQUAL_STRING("OK", run("PWROFF"));
    TEST_ASSERT_FALSE(mock_pwr);
}

void test_auth_requires_power() {
    TEST_ASSERT_EQUAL_STRING("ERR PWROFF", run("AUTH"));
    run("PWRON");
    TEST_ASSERT_EQUAL_STRING("OK", run("AUTH"));
    TEST_ASSERT_TRUE(mock_port);
}

void test_pwroff_closes_port() {
    run("PWRON"); run("AUTH");
    run("PWROFF");
    TEST_ASSERT_EQUAL_STRING("ERR PWROFF", run("READ 20"));
    run("PWRON");   // power back, but port closed until AUTH
    TEST_ASSERT_EQUAL_STRING("ERR PORT", run("READ 20"));
}

// --- READ ---

void test_read_before_auth() {
    run("PWRON");
    TEST_ASSERT_EQUAL_STRING("ERR PORT", run("READ 20"));
}

void test_read_formats_response() {
    run("PWRON"); run("AUTH");
    mock_read_data = 0x08780010; mock_read_ecc = AcsEcc::NotApplicable;
    TEST_ASSERT_EQUAL_STRING("DATA 20 0x08780010 ECC=NA", run("READ 20"));
    TEST_ASSERT_EQUAL_UINT8(0x20, last_addr);

    mock_read_data = 0x002095AE; mock_read_ecc = AcsEcc::Ok;
    TEST_ASSERT_EQUAL_STRING("DATA 09 0x002095AE ECC=OK", run("READ 0x09"));

    mock_read_ecc = AcsEcc::Fail;
    TEST_ASSERT_EQUAL_STRING("DATA 09 0x002095AE ECC=FAIL", run("read 09"));
}

void test_read_arg_validation() {
    run("PWRON"); run("AUTH");
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("READ"));        // missing addr
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("READ 40"));     // addr > 0x3F
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("READ zz"));     // not hex
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("READ 20 09"));  // extra token
}

// --- WRAM ---

void test_wram() {
    run("PWRON"); run("AUTH");
    TEST_ASSERT_EQUAL_STRING("OK", run("WRAM 19 0x2095AE"));
    TEST_ASSERT_EQUAL_UINT8(0x19, last_addr);
    TEST_ASSERT_EQUAL_UINT32(0x2095AE, last_data);
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("WRAM 19 0x4000000"));  // > 26 bits
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("WRAM 19"));            // missing data
}

// --- WEEP ---

void test_weep_success() {
    run("PWRON"); run("AUTH");
    TEST_ASSERT_EQUAL_STRING("OK VERIFY=OK", run("WEEP 0B 0x123456"));
    TEST_ASSERT_EQUAL_UINT8(0x0B, last_addr);
    TEST_ASSERT_FALSE(last_force);
}

void test_weep_write_lock_guard() {
    run("PWRON"); run("AUTH");
    // WRITE_LOCK[25] set on EE_CUST0 (0x2095AE | 1<<25) -> refused without FORCE
    TEST_ASSERT_EQUAL_STRING("ERR LOCKED", run("WEEP 09 0x22095AE"));
    TEST_ASSERT_EQUAL_STRING("OK VERIFY=OK", run("WEEP 09 0x22095AE FORCE"));
    TEST_ASSERT_TRUE(last_force);
    // bit 25 clear -> no guard
    TEST_ASSERT_EQUAL_STRING("OK VERIFY=OK", run("WEEP 09 0x2095AE"));
    // garbage where FORCE should be
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("WEEP 09 0x22095AE NOW"));
}

void test_weep_verify_fail_maps_to_err() {
    run("PWRON"); run("AUTH");
    mock_weep_result = AcsError::Verify;
    TEST_ASSERT_EQUAL_STRING("ERR VERIFY", run("WEEP 0B 0x123456"));
    mock_weep_result = AcsError::Ecc;
    TEST_ASSERT_EQUAL_STRING("ERR ECC", run("WEEP 0B 0x123456"));
    TEST_ASSERT_EQUAL_STRING("STATUS PWR=1 PORT=1 ERR=ECC", run("STATUS"));
}

// --- Robustness ---

void test_unknown_and_empty() {
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("BOGUS"));
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run(""));
    TEST_ASSERT_EQUAL_STRING("ERR ARG", run("   "));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_idn);
    RUN_TEST(test_status_reflects_state_and_last_error);
    RUN_TEST(test_power_on_off);
    RUN_TEST(test_auth_requires_power);
    RUN_TEST(test_pwroff_closes_port);
    RUN_TEST(test_read_before_auth);
    RUN_TEST(test_read_formats_response);
    RUN_TEST(test_read_arg_validation);
    RUN_TEST(test_wram);
    RUN_TEST(test_weep_success);
    RUN_TEST(test_weep_verify_fail_maps_to_err);
    RUN_TEST(test_weep_write_lock_guard);
    RUN_TEST(test_unknown_and_empty);
    return UNITY_END();
}
