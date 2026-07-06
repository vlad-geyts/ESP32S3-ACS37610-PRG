"""JSON register-snapshot save/load (GUI plan §9, v1.1 semantics).

Pure module — no Qt, no serial. The snapshot stores the full 32-bit response
word per register as authoritative `raw` (ECC bits and TEMP_OUT included as
read), plus decoded `fields` for human diffing. On Load, `raw` is
authoritative and callers mask to DATA[25:0] before writing to the device.

Validation is strict: unknown format/version, unknown register names,
address mismatches and out-of-range raw values raise StorageError with a
descriptive message — mismatches are reported, never silently ignored.
"""
from __future__ import annotations

import json
from datetime import datetime, timezone
from typing import Mapping

from . import registers

FORMAT = "acs37610-registers"
VERSION = 1

_RAW32_MASK = 0xFFFFFFFF


class StorageError(Exception):
    """Snapshot file is malformed, wrong version, or inconsistent."""


def build_snapshot(values: Mapping[int, int], *, fw_version: str = "",
                   device_id: str = "", timestamp: str | None = None) -> dict:
    """Assemble the §9 JSON document from {addr: raw32} read results."""
    regs: dict[str, dict] = {}
    for reg in registers.REGISTERS:
        if reg.addr not in values:
            continue
        raw = values[reg.addr] & _RAW32_MASK
        entry: dict = {
            "addr": f"0x{reg.addr:02X}",
            "raw": f"0x{raw:08X}",
            "fields": registers.decode(reg.addr, raw),
        }
        if reg.shadow_addr is not None and reg.shadow_addr in values:
            entry["shadow_addr"] = f"0x{reg.shadow_addr:02X}"
            entry["shadow_raw"] = f"0x{values[reg.shadow_addr] & _RAW32_MASK:08X}"
        regs[reg.name] = entry

    return {
        "format": FORMAT,
        "version": VERSION,
        "device_id": device_id,
        "timestamp": timestamp if timestamp is not None else
                     datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "fw_version": fw_version,
        "registers": regs,
    }


def _parse_hex(name: str, text) -> int:
    try:
        v = int(str(text), 16)
    except (TypeError, ValueError):
        raise StorageError(f"{name}: {text!r} is not a hex value") from None
    if not 0 <= v <= _RAW32_MASK:
        raise StorageError(f"{name}: 0x{v:X} outside 32-bit range")
    return v


def parse_snapshot(doc: dict) -> dict[int, int]:
    """Validate a snapshot document; return {addr: raw32} (shadows included)."""
    if not isinstance(doc, dict):
        raise StorageError("snapshot root is not a JSON object")
    if doc.get("format") != FORMAT:
        raise StorageError(f"unknown format {doc.get('format')!r}"
                           f" (expected {FORMAT!r})")
    if doc.get("version") != VERSION:
        raise StorageError(f"unsupported version {doc.get('version')!r}"
                           f" (expected {VERSION})")
    regs = doc.get("registers")
    if not isinstance(regs, dict) or not regs:
        raise StorageError("snapshot contains no registers")

    values: dict[int, int] = {}
    for name, entry in regs.items():
        reg = registers.BY_NAME.get(name)
        if reg is None:
            raise StorageError(f"unknown register name {name!r}")
        if not isinstance(entry, dict):
            raise StorageError(f"{name}: entry is not an object")
        addr = _parse_hex(f"{name}.addr", entry.get("addr"))
        if addr != reg.addr:
            raise StorageError(f"{name}: addr 0x{addr:02X} does not match"
                               f" expected 0x{reg.addr:02X}")
        values[reg.addr] = _parse_hex(f"{name}.raw", entry.get("raw"))

        # `raw` is authoritative, but a hand-edited `fields` value that
        # disagrees with it must fail loudly, not be silently ignored —
        # otherwise an edited field would load green without taking effect.
        fields = entry.get("fields")
        if isinstance(fields, dict):
            decoded = registers.decode(reg.addr, values[reg.addr])
            for fname, fval in fields.items():
                if fname not in decoded:
                    raise StorageError(f"{name}.fields: unknown field {fname!r}")
                try:
                    fval_int = int(fval)
                except (TypeError, ValueError):
                    raise StorageError(f"{name}.fields.{fname}:"
                                       f" {fval!r} is not an integer") from None
                if fval_int != decoded[fname]:
                    raise StorageError(
                        f"{name}.fields.{fname}={fval_int} disagrees with"
                        f" raw={entry.get('raw')} (which decodes to"
                        f" {decoded[fname]}). 'raw' is authoritative — edit"
                        f" 'raw', or keep 'fields' consistent with it.")
        if "shadow_raw" in entry:
            if reg.shadow_addr is None:
                raise StorageError(f"{name}: has shadow_raw but no shadow register")
            if "shadow_addr" in entry:
                sh = _parse_hex(f"{name}.shadow_addr", entry["shadow_addr"])
                if sh != reg.shadow_addr:
                    raise StorageError(
                        f"{name}: shadow_addr 0x{sh:02X} does not match"
                        f" expected 0x{reg.shadow_addr:02X}")
            values[reg.shadow_addr] = _parse_hex(
                f"{name}.shadow_raw", entry["shadow_raw"])
    return values


def save_snapshot(path: str, values: Mapping[int, int], *,
                  fw_version: str = "", device_id: str = "") -> None:
    doc = build_snapshot(values, fw_version=fw_version, device_id=device_id)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")


def load_snapshot(path: str) -> dict[int, int]:
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except OSError as exc:
        raise StorageError(f"cannot read {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise StorageError(f"{path} is not valid JSON: {exc}") from exc
    return parse_snapshot(doc)
