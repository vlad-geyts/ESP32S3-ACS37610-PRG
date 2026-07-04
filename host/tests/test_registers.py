"""Register model / codec tests (GUI plan §6).

Decode vectors for EE_CUST0/1 and FAULT_STATUS come from real device reads
captured during hardware validation (2026-07-03/04).
"""
import pytest

from acs_gui import registers as R


# -- structure invariants -----------------------------------------------------

def test_field_maps_cover_expected_bits_without_overlap():
    for reg in R.REGISTERS:
        seen = 0
        for f in reg.fields:
            assert 0 <= f.lsb <= f.msb < reg.payload_bits, f"{reg.name}.{f.name}"
            assert seen & f.mask == 0, f"{reg.name}.{f.name} overlaps"
            seen |= f.mask
        assert seen == reg.covered_mask


def test_data_registers_cover_full_26_bits():
    # EE_CUST0/1/2 field maps must account for every payload bit
    for name in ("EE_CUST0", "EE_CUST1", "EE_CUST2"):
        assert R.BY_NAME[name].covered_mask == 0x3FFFFFF, name


def test_shadow_addresses_share_bitmap():
    assert R.BY_ADDR[0x19] is R.EE_CUST0
    assert R.BY_ADDR[0x1A] is R.EE_CUST1
    assert R.BY_ADDR[0x09] is R.EE_CUST0


def test_read_all_sequence():
    assert R.ALL_ADDRS == (0x09, 0x19, 0x0A, 0x1A, 0x0B, 0x20)


def test_fault_status_is_read_only_32bit():
    assert R.FAULT_STATUS.access == "RO"
    assert R.FAULT_STATUS.payload_bits == 32


# -- decode vectors from real hardware reads ---------------------------------

def test_decode_ee_cust0_hardware_value():
    f = R.decode(0x09, 0x002095AE)
    assert f["WRITE_LOCK"] == 0
    assert f["COM_LOCK"] == 0
    assert f["POL"] == 1
    assert f["QVO"] == 74
    assert f["SENS_FINE"] == 430


def test_decode_ee_cust1_hardware_value():
    f = R.decode(0x0A, 0x0003182E)
    assert f["OCF_HYST"] == 0
    assert f["OTF_THRESH"] == 12
    assert f["OCF_N_THRES"] == 48
    assert f["OCF_P_THRES"] == 46


def test_decode_fault_status_hardware_value():
    f = R.decode(0x20, 0x08780010)
    assert f["TEMP_OUT"] == 0x878   # 2168 — live die temperature
    assert f["UV_EV"] == 1          # bit 4
    assert f["OV_STAT"] == 0
    assert f["FP_EV"] == 0


# -- encode / round-trip ------------------------------------------------------

def test_encode_matches_hardware_value():
    raw = R.encode(0x09, {"POL": 1, "QVO": 74, "SENS_FINE": 430})
    assert raw == 0x002095AE


def test_encode_decode_round_trip_all_registers():
    vectors = (0x0000000, 0x3FFFFFF, 0x2095AE, 0x1AAAAAA, 0x1555555, 0xFFFFFFF)
    for reg in R.REGISTERS:
        for v in vectors:
            raw = v & reg.covered_mask
            assert R.encode(reg.addr, R.decode(reg.addr, raw)) == raw, reg.name


def test_encode_unknown_field_rejected():
    with pytest.raises(ValueError, match="no field"):
        R.encode(0x09, {"BOGUS": 1})


def test_encode_out_of_range_rejected():
    with pytest.raises(ValueError, match="outside"):
        R.encode(0x09, {"QVO": 512})     # 9-bit field
    with pytest.raises(ValueError, match="outside"):
        R.encode(0x09, {"POL": 2})       # 1-bit field


def test_field_lookup():
    f = R.EE_CUST0.field("QVO")
    assert (f.msb, f.lsb, f.width, f.max) == (17, 9, 9, 511)
    with pytest.raises(KeyError):
        R.EE_CUST0.field("NOPE")
