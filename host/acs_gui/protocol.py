"""Typed client for the ACS37610 programmer ASCII protocol (GUI plan §3).

Turns protocol lines into Python calls and typed results; every firmware
"ERR <code>" reply raises the matching exception. Knows nothing about widgets
or threads — it only needs a transport with send(line, timeout) -> str.
"""
from __future__ import annotations

import re
from dataclasses import dataclass

ADDR_MAX = 0x3F           # 6-bit register address
DATA26_MAX = 0x03FFFFFF   # 26-bit payload (plan §3.1)

# WEEP runs write + t_w (40 ms) + read-back inside the firmware; give the
# reply line generous headroom on top of the transport default.
WEEP_TIMEOUT_S = 3.0


# -- errors -----------------------------------------------------------------

class ProtocolError(Exception):
    """Malformed/unexpected reply, or firmware ERR (see subclasses)."""
    code = "PROTOCOL"


class DeviceArgError(ProtocolError):
    code = "ARG"


class PortNotOpenError(ProtocolError):
    """READ/WRITE before AUTH (device serial port not open)."""
    code = "PORT"


class DeviceTimeoutError(ProtocolError):
    """Device did not answer on the PROG line."""
    code = "TIMEOUT"


class CrcError(ProtocolError):
    code = "CRC"


class EccError(ProtocolError):
    code = "ECC"


class VerifyError(ProtocolError):
    """EEPROM read-back value differs from the written value."""
    code = "VERIFY"


class LockedError(ProtocolError):
    """WRITE_LOCK guard tripped / write refused."""
    code = "LOCKED"


class PowerOffError(ProtocolError):
    """Command requires DUT power but the rail is off."""
    code = "PWROFF"


_ERR_MAP: dict[str, type[ProtocolError]] = {
    cls.code: cls
    for cls in (DeviceArgError, PortNotOpenError, DeviceTimeoutError,
                CrcError, EccError, VerifyError, LockedError, PowerOffError)
}


# -- results ----------------------------------------------------------------

@dataclass(frozen=True)
class Status:
    pwr: bool
    port_open: bool
    last_error: str   # "NONE" or an ERR code


@dataclass(frozen=True)
class ReadResult:
    addr: int
    data: int         # full 32-bit response payload
    ecc: str          # "OK" | "FAIL" | "NA"

    @property
    def data26(self) -> int:
        """26-bit register payload DATA[25:0]."""
        return self.data & DATA26_MAX


@dataclass(frozen=True)
class WriteResult:
    verified: bool


# -- client -----------------------------------------------------------------

_STATUS_RE = re.compile(r"^STATUS PWR=([01]) PORT=([01]) ERR=(\w+)$")
_DATA_RE = re.compile(r"^DATA ([0-9A-Fa-f]{1,2}) 0x([0-9A-Fa-f]{8}) ECC=(OK|FAIL|NA)$")


def _check_addr(addr: int) -> None:
    if not 0 <= addr <= ADDR_MAX:
        raise ValueError(f"address 0x{addr:X} outside 0x00-0x{ADDR_MAX:X}")


def _check_data(data: int) -> None:
    if not 0 <= data <= DATA26_MAX:
        raise ValueError(f"data 0x{data:X} exceeds 26-bit payload")


class ProtocolClient:
    def __init__(self, transport, timeout: float = 2.0) -> None:
        self._t = transport
        self._timeout = timeout

    def _cmd(self, line: str, timeout: float | None = None) -> str:
        reply = self._t.send(line, timeout or self._timeout)
        if reply.startswith("ERR "):
            code = reply[4:].strip()
            raise _ERR_MAP.get(code, ProtocolError)(f"{line!r} -> {reply}")
        return reply

    def _cmd_ok(self, line: str, expect: str = "OK",
                timeout: float | None = None) -> None:
        reply = self._cmd(line, timeout)
        if reply != expect:
            raise ProtocolError(f"{line!r}: expected {expect!r}, got {reply!r}")

    # -- commands (plan §3.2) ---------------------------------------------

    def idn(self) -> str:
        """Identity string, e.g. 'ACS37610-PRG 1.0.0'."""
        reply = self._cmd("*IDN?")
        if not reply.startswith("ID "):
            raise ProtocolError(f"unexpected *IDN? reply: {reply!r}")
        return reply[3:]

    def status(self) -> Status:
        reply = self._cmd("STATUS")
        m = _STATUS_RE.match(reply)
        if not m:
            raise ProtocolError(f"unexpected STATUS reply: {reply!r}")
        return Status(pwr=m.group(1) == "1", port_open=m.group(2) == "1",
                      last_error=m.group(3))

    def power_on(self) -> None:
        self._cmd_ok("PWRON")

    def power_off(self) -> None:
        self._cmd_ok("PWROFF")

    def auth(self) -> None:
        """Send the Access Code — opens the device serial port (ENABLE DEVICE)."""
        self._cmd_ok("AUTH")

    def read_register(self, addr: int) -> ReadResult:
        _check_addr(addr)
        reply = self._cmd(f"READ {addr:02X}")
        m = _DATA_RE.match(reply)
        if not m:
            raise ProtocolError(f"unexpected READ reply: {reply!r}")
        echoed = int(m.group(1), 16)
        if echoed != addr:
            raise ProtocolError(
                f"READ address mismatch: sent 0x{addr:02X}, echo 0x{echoed:02X}")
        return ReadResult(addr=echoed, data=int(m.group(2), 16), ecc=m.group(3))

    def write_ram(self, addr: int, data: int) -> None:
        _check_addr(addr)
        _check_data(data)
        self._cmd_ok(f"WRAM {addr:02X} 0x{data:07X}")

    def write_eeprom(self, addr: int, data: int, force: bool = False) -> WriteResult:
        """EEPROM write; firmware enforces t_w and read-back verify.

        force=True appends FORCE — required to set WRITE_LOCK[25] on EE_CUST0
        (one-time-programmable; see plan §7.6 for the GUI-side guard).
        """
        _check_addr(addr)
        _check_data(data)
        line = f"WEEP {addr:02X} 0x{data:07X}" + (" FORCE" if force else "")
        self._cmd_ok(line, expect="OK VERIFY=OK", timeout=WEEP_TIMEOUT_S)
        return WriteResult(verified=True)
