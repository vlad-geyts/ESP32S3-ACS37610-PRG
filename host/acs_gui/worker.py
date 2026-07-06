"""Qt worker hosting the serial transport + protocol client (GUI plan §2.1).

A ProgrammerWorker instance is moved to a dedicated QThread by the main
window. All pyserial I/O happens in that thread; the UI communicates only
through queued signal/slot connections, so the GUI never blocks on serial
traffic. One operation runs at a time (single in-flight, plan O5) — the UI
disables controls while `busy_changed(True)` is in effect.

This module is the Qt layer on top of the deliberately Qt-free G2 modules
(transport.py / protocol.py), which keeps those unit-testable without Qt.
"""
from __future__ import annotations

from PySide6.QtCore import QObject, Signal, Slot

from . import registers, storage
from .protocol import ProtocolClient, ProtocolError
from .transport import SerialTransport, TransportError


class _LoggingTransport:
    """Wraps a transport so every protocol line lands in the activity log."""

    def __init__(self, inner, emit_log):
        self._inner = inner
        self._log = emit_log

    def send(self, line: str, timeout: float = 2.0) -> str:
        self._log(f"> {line}")
        try:
            reply = self._inner.send(line, timeout)
        except Exception as exc:
            self._log(f"! {exc}")
            raise
        self._log(f"< {reply}")
        return reply


class ProgrammerWorker(QObject):
    log = Signal(str)                    # activity-log lines (>, <, !, info)
    busy_changed = Signal(bool)          # an operation is running
    conn_changed = Signal(bool, str)     # connected?, idn / reason
    power_changed = Signal(bool)         # DUT rail state
    device_enabled_changed = Signal(bool)  # AUTH succeeded (port open)
    read_result = Signal(int, int, str)  # addr, data32, ecc — per register
    read_all_done = Signal(bool)         # overall Read All outcome
    reg_read_done = Signal(int, bool)    # per-tab Read outcome (eeprom addr)
    write_done = Signal(int, bool, str)  # addr written, verified?, detail
    save_done = Signal(bool, str)        # Save to File outcome, path / detail
    load_done = Signal(bool, str)        # Load from File outcome, detail
    op_error = Signal(str)               # operation failure (also logged)

    def __init__(self):
        super().__init__()
        self._transport = SerialTransport()
        self._client = ProtocolClient(
            _LoggingTransport(self._transport, self.log.emit))
        self._idn = ""   # remembered for snapshot metadata

    # -- operations (invoked via queued connections from the UI thread) ----

    @Slot(str)
    def op_connect(self, port: str) -> None:
        self.busy_changed.emit(True)
        try:
            self._transport.open(port)
            idn = self._client.idn()
            self._idn = idn
            self.log.emit(f"[connected to {port}: {idn}]")
            self.conn_changed.emit(True, idn)
        except (TransportError, ProtocolError) as exc:
            self._transport.close()
            self.conn_changed.emit(False, str(exc))
        finally:
            self.busy_changed.emit(False)

    @Slot()
    def op_disconnect(self) -> None:
        self.busy_changed.emit(True)
        try:
            if self._transport.is_open:
                try:
                    self._client.power_off()   # don't leave the DUT powered
                    self.log.emit("[DUT powered off before disconnect]")
                except (TransportError, ProtocolError):
                    pass
            self._transport.close()
        finally:
            self.power_changed.emit(False)
            self.device_enabled_changed.emit(False)
            self.conn_changed.emit(False, "disconnected")
            self.busy_changed.emit(False)

    @Slot()
    def op_power_on(self) -> None:
        self.busy_changed.emit(True)
        try:
            self._client.power_on()
            self.power_changed.emit(True)
        except (TransportError, ProtocolError) as exc:
            self.op_error.emit(f"Power On failed: {exc}")
        finally:
            self.busy_changed.emit(False)

    @Slot()
    def op_power_off(self) -> None:
        self.busy_changed.emit(True)
        try:
            self._client.power_off()
            self.power_changed.emit(False)
            # device serial port closes on power loss -> re-grey gated controls
            self.device_enabled_changed.emit(False)
        except (TransportError, ProtocolError) as exc:
            self.op_error.emit(f"Power Off failed: {exc}")
        finally:
            self.busy_changed.emit(False)

    @Slot()
    def op_enable_device(self) -> None:
        """ENABLE DEVICE (plan §7.1 v1.1): send the Access Code."""
        self.busy_changed.emit(True)
        try:
            self._client.auth()
            self.device_enabled_changed.emit(True)
        except (TransportError, ProtocolError) as exc:
            self.device_enabled_changed.emit(False)
            self.op_error.emit(f"ENABLE DEVICE failed: {exc}")
        finally:
            self.busy_changed.emit(False)

    @Slot(int, int)
    def op_read_register(self, addr: int, shadow_addr: int) -> None:
        """Per-tab Read (plan §8.4): EEPROM register + its shadow if any."""
        self.busy_changed.emit(True)
        ok = True
        try:
            for a in ((addr,) if shadow_addr < 0 else (addr, shadow_addr)):
                try:
                    r = self._client.read_register(a)
                    self.read_result.emit(a, r.data, r.ecc)
                except (ProtocolError, TransportError) as exc:
                    ok = False
                    self.op_error.emit(f"READ {a:02X} failed: {exc}")
            self.reg_read_done.emit(addr, ok)
        finally:
            self.busy_changed.emit(False)

    @Slot(int, int, bool)
    def op_write_eeprom_verified(self, addr: int, data: int, force: bool) -> None:
        """WEEP + GUI-side read-back compare (plan §8.5). The firmware already
        verifies internally; the read-back here confirms independently and
        refreshes the displayed value."""
        self.busy_changed.emit(True)
        try:
            self._client.write_eeprom(addr, data, force)
            r = self._client.read_register(addr)
            self.read_result.emit(addr, r.data, r.ecc)
            if r.data26 != data:
                self.write_done.emit(
                    addr, False,
                    f"read-back 0x{r.data26:07X} != written 0x{data:07X}")
            elif r.ecc == "FAIL":
                self.write_done.emit(addr, False, "ECC fault on read-back")
            else:
                self.write_done.emit(addr, True, "EEPROM write verified")
        except (ProtocolError, TransportError) as exc:
            self.op_error.emit(f"WEEP {addr:02X} failed: {exc}")
            self.write_done.emit(addr, False, str(exc))
        finally:
            self.busy_changed.emit(False)

    @Slot(int, int)
    def op_write_ram_verified(self, addr: int, data: int) -> None:
        """WRAM + read-back compare — RAM writes are not firmware-verified,
        so the GUI read-back is the only check (plan §8.5 note)."""
        self.busy_changed.emit(True)
        try:
            self._client.write_ram(addr, data)
            r = self._client.read_register(addr)
            self.read_result.emit(addr, r.data, r.ecc)
            if r.data26 != data:
                self.write_done.emit(
                    addr, False,
                    f"read-back 0x{r.data26:07X} != written 0x{data:07X}")
            else:
                self.write_done.emit(addr, True, "RAM write verified")
        except (ProtocolError, TransportError) as exc:
            self.op_error.emit(f"WRAM {addr:02X} failed: {exc}")
            self.write_done.emit(addr, False, str(exc))
        finally:
            self.busy_changed.emit(False)

    @Slot()
    def op_read_all(self) -> None:
        """Read the six registers backing the tabs (plan §8.3)."""
        self.busy_changed.emit(True)
        ok = True
        try:
            for addr in registers.ALL_ADDRS:
                try:
                    r = self._client.read_register(addr)
                    self.read_result.emit(addr, r.data, r.ecc)
                except ProtocolError as exc:
                    ok = False
                    self.op_error.emit(f"READ {addr:02X} failed: {exc}")
                except TransportError as exc:
                    ok = False
                    self.op_error.emit(f"READ {addr:02X}: link error: {exc}")
                    break   # link is gone; no point continuing the sequence
            self.read_all_done.emit(ok)
        finally:
            self.busy_changed.emit(False)

    @Slot(str)
    def op_save_to_file(self, path: str) -> None:
        """Save to File (plan §8.7 v1.1): read ALL_ADDRS, then write the JSON
        snapshot. Any read failure aborts — no partial snapshots."""
        self.busy_changed.emit(True)
        try:
            values: dict[int, int] = {}
            for addr in registers.ALL_ADDRS:
                try:
                    r = self._client.read_register(addr)
                    values[addr] = r.data
                    self.read_result.emit(addr, r.data, r.ecc)
                except (ProtocolError, TransportError) as exc:
                    self.op_error.emit(f"READ {addr:02X} failed: {exc}")
                    self.save_done.emit(False,
                                        f"aborted — READ {addr:02X} failed")
                    return
            try:
                storage.save_snapshot(path, values, fw_version=self._idn)
            except (storage.StorageError, OSError) as exc:
                self.save_done.emit(False, f"file write failed: {exc}")
                return
            self.log.emit(f"[snapshot saved to {path}]")
            self.save_done.emit(True, path)
        finally:
            self.busy_changed.emit(False)

    @Slot(int, int, bool)
    def op_load_snapshot(self, data09: int, data0a: int, force: bool) -> None:
        """Load from File (plan §8.7 v1.1): write the snapshot values to
        EEPROM 0x09 and 0x0A, then read back and verify each. Aborts the
        sequence on the first failure."""
        self.busy_changed.emit(True)
        try:
            for addr, data, f in ((0x09, data09, force), (0x0A, data0a, False)):
                try:
                    self._client.write_eeprom(addr, data, f)
                    r = self._client.read_register(addr)
                    self.read_result.emit(addr, r.data, r.ecc)
                    if r.data26 != data:
                        self.load_done.emit(
                            False, f"0x{addr:02X}: read-back 0x{r.data26:07X}"
                                   f" != written 0x{data:07X}")
                        return
                    if r.ecc == "FAIL":
                        self.load_done.emit(False,
                                            f"0x{addr:02X}: ECC fault")
                        return
                except (ProtocolError, TransportError) as exc:
                    self.op_error.emit(f"Load: WEEP {addr:02X} failed: {exc}")
                    self.load_done.emit(False, f"WEEP {addr:02X}: {exc}")
                    return
            self.log.emit("[snapshot written to 0x09/0x0A and verified]")
            self.load_done.emit(True, "written and verified")
        finally:
            self.busy_changed.emit(False)
