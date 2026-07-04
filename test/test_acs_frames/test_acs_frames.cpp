#include <unity.h>
#include "acs37610_frames.h"

void setUp() {}
void tearDown() {}

// --- Frame builders (all vectors hardware-verified from live sessions) ---

void test_build_auth_frame() {
    // Access Code write: ADDR=0x31, DATA=0x2C413736 -> 0x1896209B9B5 (CRC=5)
    TEST_ASSERT_EQUAL_UINT64(0x1896209B9B5ull,
                             acs_build_write_frame(0x31, 0x2C413736));
}

void test_build_read_frames() {
    TEST_ASSERT_EQUAL_UINT16(0x301, acs_build_read_frame(0x20));  // CRC=1
    TEST_ASSERT_EQUAL_UINT16(0x24D, acs_build_read_frame(0x09));  // CRC=5
    TEST_ASSERT_EQUAL_UINT16(0x250, acs_build_read_frame(0x0A));  // CRC=0
}

// --- Response parsing (frames captured live on 2026-07-03) ---

void test_parse_response_ee_cust0() {
    // READ 0x09 -> 37 bits, frame 0x104AD75 -> DATA=0x002095AE, CRC=5
    uint32_t data = 0;
    TEST_ASSERT_EQUAL(AcsError::None, acs_parse_response(0x104AD75ull, 37, &data));
    TEST_ASSERT_EQUAL_UINT32(0x002095AE, data);
}

void test_parse_response_ee_cust1() {
    // READ 0x0A -> 37 bits, frame 0x18C173 -> DATA=0x0003182E, CRC=3
    uint32_t data = 0;
    TEST_ASSERT_EQUAL(AcsError::None, acs_parse_response(0x18C173ull, 37, &data));
    TEST_ASSERT_EQUAL_UINT32(0x0003182E, data);
}

void test_parse_response_fault_status() {
    // READ 0x20 -> 37 bits, frame 0x43C00082 -> DATA=0x08780010, CRC=2
    uint32_t data = 0;
    TEST_ASSERT_EQUAL(AcsError::None, acs_parse_response(0x43C00082ull, 37, &data));
    TEST_ASSERT_EQUAL_UINT32(0x08780010, data);
}

void test_parse_response_bad_crc() {
    uint32_t data = 0;
    // Flip one data bit of a valid frame -> CRC must fail
    TEST_ASSERT_EQUAL(AcsError::Crc, acs_parse_response(0x104AD75ull ^ (1ull << 10), 37, &data));
}

void test_parse_response_bad_bit_count() {
    uint32_t data = 0;
    TEST_ASSERT_EQUAL(AcsError::Crc, acs_parse_response(0x104AD75ull, 12, &data));
    TEST_ASSERT_EQUAL(AcsError::Crc, acs_parse_response(0x104AD75ull, 44, &data));
}

// --- ECC decode ---

void test_ecc_addresses() {
    TEST_ASSERT_TRUE(acs_addr_has_ecc(0x09));   // EE_CUST0
    TEST_ASSERT_TRUE(acs_addr_has_ecc(0x0A));   // EE_CUST1
    TEST_ASSERT_TRUE(acs_addr_has_ecc(0x0B));   // EE_CUST2
    TEST_ASSERT_FALSE(acs_addr_has_ecc(0x19));  // shadow — volatile
    TEST_ASSERT_FALSE(acs_addr_has_ecc(0x1A));  // shadow — volatile
    TEST_ASSERT_FALSE(acs_addr_has_ecc(0x20));  // FAULT_STATUS — [27:26] is TEMP_OUT
}

void test_ecc_decode() {
    TEST_ASSERT_EQUAL(AcsEcc::Ok,   acs_decode_ecc(0x09, 0x002095AE));          // bits[27:26]=00
    TEST_ASSERT_EQUAL(AcsEcc::Fail, acs_decode_ecc(0x09, 0x002095AE | (1ul << 26)));
    TEST_ASSERT_EQUAL(AcsEcc::Fail, acs_decode_ecc(0x09, 0x002095AE | (1ul << 27)));
    TEST_ASSERT_EQUAL(AcsEcc::NotApplicable, acs_decode_ecc(0x20, 0x08780010));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_build_auth_frame);
    RUN_TEST(test_build_read_frames);
    RUN_TEST(test_parse_response_ee_cust0);
    RUN_TEST(test_parse_response_ee_cust1);
    RUN_TEST(test_parse_response_fault_status);
    RUN_TEST(test_parse_response_bad_crc);
    RUN_TEST(test_parse_response_bad_bit_count);
    RUN_TEST(test_ecc_addresses);
    RUN_TEST(test_ecc_decode);
    return UNITY_END();
}
