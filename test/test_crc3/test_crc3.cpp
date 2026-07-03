#include <unity.h>
#include "crc3.h"

void setUp() {}
void tearDown() {}

// --- Read request vectors (R/W=1, ADDR=xx, 7 bits) ---

void test_read_fault_status() {
    // R/W=1, ADDR=0x20 (FAULT_STATUS) → 0b1100000 → CRC=1
    // Hardware-verified: device accepted frame and responded.
    TEST_ASSERT_EQUAL_UINT8(1, crc3_read_request(0x20));
}

void test_read_ee_cust0() {
    // R/W=1, ADDR=0x09 (EE_CUST0) → 0b1001001 → CRC=5
    TEST_ASSERT_EQUAL_UINT8(5, crc3_read_request(0x09));
}

void test_read_ee_cust1() {
    // R/W=1, ADDR=0x0A (EE_CUST1) → 0b1001010 → CRC=0
    TEST_ASSERT_EQUAL_UINT8(0, crc3_read_request(0x0A));
}

// --- Write command vector (R/W=0, ADDR, DATA, 39 bits) ---

void test_write_auth() {
    // AUTH: R/W=0, ADDR=0x31, DATA=0x2C413736 → CRC=5
    // Hardware-verified: device accepted frame and opened serial port.
    TEST_ASSERT_EQUAL_UINT8(5, crc3_write(0, 0x31, 0x2C413736));
}

// --- Device response vectors (DATA[32], 32 bits) ---
// Hardware-verified 2026-07-03: live device responses, CRC matched on every
// frame including changing TEMP_OUT data in FAULT_STATUS (0x20). Confirms the
// response CRC covers DATA[32] only (SYNC excluded).

void test_response_fault_status() {
    TEST_ASSERT_EQUAL_UINT8(2, crc3_response(0x08780010));  // 0x20, TEMP_OUT=0x878
    TEST_ASSERT_EQUAL_UINT8(7, crc3_response(0x08800000));  // 0x20, TEMP_OUT=0x880
    TEST_ASSERT_EQUAL_UINT8(0, crc3_response(0x08810000));  // 0x20, TEMP_OUT=0x881
    TEST_ASSERT_EQUAL_UINT8(5, crc3_response(0x08830000));  // 0x20, TEMP_OUT=0x883
}

void test_response_ee_cust() {
    TEST_ASSERT_EQUAL_UINT8(5, crc3_response(0x002095AE));  // EE_CUST0 (0x09)
    TEST_ASSERT_EQUAL_UINT8(3, crc3_response(0x0003182E));  // EE_CUST1 (0x0A)
}

// --- Structural invariants ---

void test_result_fits_3_bits() {
    // CRC must always be in range [0,7] regardless of input
    for (uint8_t addr = 0x00; addr <= 0x3F; addr++) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(7, crc3_read_request(addr));
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(7, crc3_write(0, addr, 0x00000000));
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(7, crc3_write(1, addr, 0xFFFFFFFF));
    }
}

void test_rw_bit_changes_crc() {
    // R/W=0 and R/W=1 with same ADDR must produce different results
    // (init=0b111 guarantees the first bit always perturbs the register)
    uint8_t crc_write = crc3_write(0, 0x09, 0x00000000);
    uint8_t crc_read  = crc3_read_request(0x09);
    TEST_ASSERT_NOT_EQUAL(crc_write, crc_read);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_read_fault_status);
    RUN_TEST(test_read_ee_cust0);
    RUN_TEST(test_read_ee_cust1);
    RUN_TEST(test_write_auth);
    RUN_TEST(test_response_fault_status);
    RUN_TEST(test_response_ee_cust);
    RUN_TEST(test_result_fits_3_bits);
    RUN_TEST(test_rw_bit_changes_crc);
    return UNITY_END();
}
