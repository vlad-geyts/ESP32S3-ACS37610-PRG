"""Register/field model + codec for the ACS37610 (GUI plan §6, dev plan v4.1 §2.7).

Pure declarative tables and pure functions — no Qt, no I/O. Shadow registers
(0x19/0x1A) share the bit map of their EEPROM twins (0x09/0x0A).

FAULT_STATUS (0x20) is decoded from the full 32-bit response word — TEMP_OUT
extends to bit 27, so DATA[27:26] are data there, not ECC (plan §6.1). All
other registers carry a 26-bit payload DATA[25:0].
"""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class Field:
    name: str
    msb: int
    lsb: int
    description: str = ""

    @property
    def width(self) -> int:
        return self.msb - self.lsb + 1

    @property
    def max(self) -> int:
        return (1 << self.width) - 1

    @property
    def mask(self) -> int:
        return self.max << self.lsb


@dataclass(frozen=True)
class Register:
    name: str
    addr: int
    fields: tuple[Field, ...]
    shadow_addr: int | None = None
    access: str = "RW"            # "RW" | "RO"
    payload_bits: int = 26        # 32 for FAULT_STATUS (full response word)

    @property
    def payload_mask(self) -> int:
        return (1 << self.payload_bits) - 1

    @property
    def covered_mask(self) -> int:
        """Union of all field masks (bits the field map defines)."""
        m = 0
        for f in self.fields:
            m |= f.mask
        return m

    def field(self, name: str) -> Field:
        for f in self.fields:
            if f.name == name:
                return f
        raise KeyError(f"{self.name} has no field {name!r}")


def _bit(name: str, pos: int, desc: str = "") -> Field:
    return Field(name, pos, pos, desc)


EE_CUST0 = Register(
    name="EE_CUST0", addr=0x09, shadow_addr=0x19,
    fields=(
        _bit("WRITE_LOCK", 25, "OTP: permanently locks the device — guard!"),
        _bit("COM_LOCK", 24),
        _bit("SPARE", 23),
        _bit("OTF_DIS", 22),
        _bit("POL", 21, "output polarity"),
        _bit("CLAMP_EN", 20),
        _bit("FAULT_DIS", 19),
        _bit("FAULTR_DIS", 18),
        Field("QVO", 17, 9, "quiescent output voltage trim"),
        Field("SENS_FINE", 8, 0, "fine sensitivity trim"),
    ),
)

EE_CUST1 = Register(
    name="EE_CUST1", addr=0x0A, shadow_addr=0x1A,
    fields=(
        Field("OCF_HYST", 25, 24),
        _bit("FAULT_LATCH", 23),
        _bit("OCF_P_DIS", 22),
        _bit("OCF_N_DIS", 21),
        Field("OCF_QUAL", 20, 18),
        Field("OTF_THRESH", 17, 14),
        Field("OCF_N_THRES", 13, 7),
        Field("OCF_P_THRES", 6, 0),
    ),
)

EE_CUST2 = Register(
    name="EE_CUST2", addr=0x0B,
    fields=(
        Field("C_SPARE", 25, 0, "customer scratchpad"),
    ),
)

FAULT_STATUS = Register(
    name="FAULT_STATUS", addr=0x20, access="RO", payload_bits=32,
    fields=(
        Field("TEMP_OUT", 27, 16, "die temperature"),
        Field("SPARE_HI", 15, 13),      # plan §6 names both spares "SPARE"
        _bit("UV_STAT", 12),
        _bit("OV_STAT", 11),
        _bit("OC_STAT", 10),
        _bit("OT_STAT", 9),
        _bit("FP_STAT", 8),
        Field("SPARE_LO", 7, 5),
        _bit("UV_EV", 4),
        _bit("OV_EV", 3),
        _bit("OC_EV", 2),
        _bit("OT_EV", 1),
        _bit("FP_EV", 0),
    ),
)

REGISTERS: tuple[Register, ...] = (EE_CUST0, EE_CUST1, EE_CUST2, FAULT_STATUS)

# Address -> register; shadow addresses map to their EEPROM twin's bit map.
BY_ADDR: dict[int, Register] = {}
for _reg in REGISTERS:
    BY_ADDR[_reg.addr] = _reg
    if _reg.shadow_addr is not None:
        BY_ADDR[_reg.shadow_addr] = _reg

BY_NAME: dict[str, Register] = {r.name: r for r in REGISTERS}

# Read All / Save to File sequence (GUI plan §7.1, v1.1).
ALL_ADDRS: tuple[int, ...] = (0x09, 0x19, 0x0A, 0x1A, 0x0B, 0x20)


def decode(addr: int, raw: int) -> dict[str, int]:
    """Split a raw register word into {field_name: value}."""
    reg = BY_ADDR[addr]
    raw &= reg.payload_mask
    return {f.name: (raw >> f.lsb) & f.max for f in reg.fields}


def encode(addr: int, fields: dict[str, int]) -> int:
    """Assemble a raw register word from field values.

    Unknown field names and out-of-range values raise ValueError; fields not
    mentioned encode as 0.
    """
    reg = BY_ADDR[addr]
    known = {f.name: f for f in reg.fields}
    raw = 0
    for name, value in fields.items():
        f = known.get(name)
        if f is None:
            raise ValueError(f"{reg.name} has no field {name!r}")
        if not 0 <= value <= f.max:
            raise ValueError(
                f"{reg.name}.{name}={value} outside 0..{f.max} ({f.width} bits)")
        raw |= value << f.lsb
    return raw
