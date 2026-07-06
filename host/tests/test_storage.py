"""Storage (snapshot save/load) tests — GUI plan §9."""
import json

import pytest

from acs_gui import storage
from acs_gui.storage import StorageError

# Real register contents from hardware validation sessions.
HW_VALUES = {
    0x09: 0x002095AE, 0x19: 0x002095AE,
    0x0A: 0x0003182E, 0x1A: 0x0003182E,
    0x0B: 0x00123456,
    0x20: 0x08080010,
}


def test_build_contains_all_registers_and_fields():
    doc = storage.build_snapshot(HW_VALUES, fw_version="ACS37610-PRG 1.0.0")
    assert doc["format"] == storage.FORMAT
    assert doc["version"] == storage.VERSION
    regs = doc["registers"]
    assert set(regs) == {"EE_CUST0", "EE_CUST1", "EE_CUST2", "FAULT_STATUS"}
    assert regs["EE_CUST0"]["raw"] == "0x002095AE"
    assert regs["EE_CUST0"]["shadow_raw"] == "0x002095AE"
    assert regs["EE_CUST0"]["fields"]["QVO"] == 74
    assert regs["FAULT_STATUS"]["fields"]["TEMP_OUT"] == 0x808
    assert "shadow_raw" not in regs["EE_CUST2"]


def test_round_trip():
    doc = storage.build_snapshot(HW_VALUES)
    assert storage.parse_snapshot(doc) == HW_VALUES


def test_file_round_trip(tmp_path):
    p = tmp_path / "snap.json"
    storage.save_snapshot(str(p), HW_VALUES, fw_version="1.0.0")
    assert storage.load_snapshot(str(p)) == HW_VALUES
    # file is valid, human-readable JSON
    doc = json.loads(p.read_text(encoding="utf-8"))
    assert doc["fw_version"] == "1.0.0"


def test_partial_snapshot_allowed():
    doc = storage.build_snapshot({0x09: 0x2095AE})
    vals = storage.parse_snapshot(doc)
    assert vals == {0x09: 0x2095AE}


def test_reject_wrong_format_and_version():
    doc = storage.build_snapshot(HW_VALUES)
    bad = dict(doc, format="something-else")
    with pytest.raises(StorageError, match="format"):
        storage.parse_snapshot(bad)
    bad = dict(doc, version=99)
    with pytest.raises(StorageError, match="version"):
        storage.parse_snapshot(bad)


def test_reject_unknown_register_and_addr_mismatch():
    doc = storage.build_snapshot(HW_VALUES)
    bad = json.loads(json.dumps(doc))
    bad["registers"]["BOGUS"] = {"addr": "0x30", "raw": "0x0"}
    with pytest.raises(StorageError, match="unknown register"):
        storage.parse_snapshot(bad)

    bad = json.loads(json.dumps(doc))
    bad["registers"]["EE_CUST0"]["addr"] = "0x0A"   # wrong address
    with pytest.raises(StorageError, match="does not match"):
        storage.parse_snapshot(bad)


def test_reject_bad_raw_values():
    doc = storage.build_snapshot(HW_VALUES)
    bad = json.loads(json.dumps(doc))
    bad["registers"]["EE_CUST0"]["raw"] = "not-hex"
    with pytest.raises(StorageError, match="hex"):
        storage.parse_snapshot(bad)

    bad = json.loads(json.dumps(doc))
    bad["registers"]["EE_CUST0"]["raw"] = "0x1FFFFFFFF"   # > 32 bit
    with pytest.raises(StorageError, match="32-bit"):
        storage.parse_snapshot(bad)


def test_reject_fields_disagreeing_with_raw():
    # The exact hardware-found trap: user edits a field value but not raw.
    doc = storage.build_snapshot(HW_VALUES)
    bad = json.loads(json.dumps(doc))
    bad["registers"]["EE_CUST2"]["fields"]["C_SPARE"] = 1   # raw still 0x123456
    with pytest.raises(StorageError, match="authoritative"):
        storage.parse_snapshot(bad)

    bad = json.loads(json.dumps(doc))
    bad["registers"]["EE_CUST0"]["fields"]["BOGUS"] = 1
    with pytest.raises(StorageError, match="unknown field"):
        storage.parse_snapshot(bad)


def test_fields_block_optional():
    # fields is informational — a snapshot without it loads fine
    doc = storage.build_snapshot(HW_VALUES)
    trimmed = json.loads(json.dumps(doc))
    for entry in trimmed["registers"].values():
        del entry["fields"]
    assert storage.parse_snapshot(trimmed) == HW_VALUES


def test_load_rejects_garbage_file(tmp_path):
    p = tmp_path / "bad.json"
    p.write_text("{ not json", encoding="utf-8")
    with pytest.raises(StorageError, match="JSON"):
        storage.load_snapshot(str(p))
    with pytest.raises(StorageError, match="cannot read"):
        storage.load_snapshot(str(tmp_path / "missing.json"))
