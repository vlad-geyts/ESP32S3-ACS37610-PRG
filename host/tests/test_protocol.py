"""ProtocolClient tests against a scripted mock transport.

Reply strings are real firmware output captured during the 2026-07-04
hardware smoke test (G1 validation).
"""
import pytest

from acs_gui.protocol import (
    CrcError, DeviceTimeoutError, LockedError, PortNotOpenError, PowerOffError,
    ProtocolClient, ProtocolError, VerifyError,
)


class MockTransport:
    def __init__(self, script):
        self.script = dict(script)   # command line -> reply line
        self.sent = []

    def send(self, line, timeout=None):
        self.sent.append(line)
        return self.script[line]


def client(script):
    t = MockTransport(script)
    return ProtocolClient(t), t


# -- identity / status ------------------------------------------------------

def test_idn():
    c, _ = client({"*IDN?": "ID ACS37610-PRG 1.0.0"})
    assert c.idn() == "ACS37610-PRG 1.0.0"


def test_idn_rejects_garbage():
    c, _ = client({"*IDN?": "garbage"})
    with pytest.raises(ProtocolError):
        c.idn()


def test_status_parsing():
    c, _ = client({"STATUS": "STATUS PWR=1 PORT=1 ERR=NONE"})
    s = c.status()
    assert s.pwr and s.port_open and s.last_error == "NONE"

    c, _ = client({"STATUS": "STATUS PWR=0 PORT=0 ERR=PWROFF"})
    s = c.status()
    assert not s.pwr and not s.port_open and s.last_error == "PWROFF"


# -- power / auth -----------------------------------------------------------

def test_power_and_auth_sequence():
    c, t = client({"PWRON": "OK", "AUTH": "OK", "PWROFF": "OK"})
    c.power_on()
    c.auth()
    c.power_off()
    assert t.sent == ["PWRON", "AUTH", "PWROFF"]


def test_auth_before_power_raises():
    c, _ = client({"AUTH": "ERR PWROFF"})
    with pytest.raises(PowerOffError):
        c.auth()


# -- read ---------------------------------------------------------------------

def test_read_register():
    c, t = client({"READ 09": "DATA 09 0x002095AE ECC=OK"})
    r = c.read_register(0x09)
    assert r.addr == 0x09
    assert r.data == 0x002095AE
    assert r.data26 == 0x002095AE
    assert r.ecc == "OK"
    assert t.sent == ["READ 09"]


def test_read_fault_status_full_32bit():
    c, _ = client({"READ 20": "DATA 20 0x08780010 ECC=NA"})
    r = c.read_register(0x20)
    assert r.data == 0x08780010
    assert r.data26 == 0x00780010   # DATA[25:0] only
    assert r.ecc == "NA"


def test_read_before_auth_raises():
    c, _ = client({"READ 20": "ERR PORT"})
    with pytest.raises(PortNotOpenError):
        c.read_register(0x20)


def test_read_device_timeout_and_crc():
    c, _ = client({"READ 09": "ERR TIMEOUT"})
    with pytest.raises(DeviceTimeoutError):
        c.read_register(0x09)
    c, _ = client({"READ 09": "ERR CRC"})
    with pytest.raises(CrcError):
        c.read_register(0x09)


def test_read_echo_mismatch_raises():
    c, _ = client({"READ 09": "DATA 0A 0x002095AE ECC=OK"})
    with pytest.raises(ProtocolError, match="mismatch"):
        c.read_register(0x09)


def test_read_addr_validation_is_local():
    c, t = client({})
    with pytest.raises(ValueError):
        c.read_register(0x40)
    assert t.sent == []   # nothing hit the wire


# -- writes -------------------------------------------------------------------

def test_write_ram():
    c, t = client({"WRAM 19 0x02095AE": "OK"})
    c.write_ram(0x19, 0x2095AE)
    assert t.sent == ["WRAM 19 0x02095AE"]


def test_write_ram_data_range_is_local():
    c, t = client({})
    with pytest.raises(ValueError):
        c.write_ram(0x19, 0x4000000)   # > 26 bits
    assert t.sent == []


def test_write_eeprom_verified():
    c, t = client({"WEEP 0B 0x0123456": "OK VERIFY=OK"})
    assert c.write_eeprom(0x0B, 0x123456).verified
    assert t.sent == ["WEEP 0B 0x0123456"]


def test_write_eeprom_force_appends_token():
    c, t = client({"WEEP 09 0x22095AE FORCE": "OK VERIFY=OK"})
    c.write_eeprom(0x09, 0x22095AE, force=True)
    assert t.sent == ["WEEP 09 0x22095AE FORCE"]


def test_write_eeprom_locked_and_verify_errors():
    c, _ = client({"WEEP 09 0x22095AE": "ERR LOCKED"})
    with pytest.raises(LockedError):
        c.write_eeprom(0x09, 0x22095AE)
    c, _ = client({"WEEP 0B 0x0123456": "ERR VERIFY"})
    with pytest.raises(VerifyError):
        c.write_eeprom(0x0B, 0x123456)


def test_unknown_err_code_still_raises():
    c, _ = client({"PWRON": "ERR WHATEVER"})
    with pytest.raises(ProtocolError):
        c.power_on()
