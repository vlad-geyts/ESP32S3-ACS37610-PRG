"""Parameterized register tab for EE_CUST0/1/2 (GUI plan §7.2-§7.4).

Layout: field table (EEPROM column, Shadow column when the register has one,
Edit column) + raw-hex line synced two-way with the field editors (§8.6) +
Read / Write EEPROM / Write Shadow buttons with Read and Write indicators.

Write flow (§8.5): encode edits -> WEEP/WRAM -> read back -> compare -> the
Write indicator goes green (Verified) or red. The worker performs the
read-back; results also refresh the value columns.

WRITE_LOCK guard (§7.6, EE_CUST0 only): the WRITE_LOCK editor is disabled
behind an "Enable WRITE_LOCK editing" checkbox, and a WEEP whose data sets
bit[25] requires typing LOCK in a confirmation dialog (then sent with FORCE
for the firmware-side guard).
"""
from __future__ import annotations

from PySide6.QtCore import Signal, Slot
from PySide6.QtWidgets import (
    QCheckBox, QHBoxLayout, QInputDialog, QLabel, QLineEdit, QPushButton,
    QVBoxLayout, QWidget,
)

from ..registers import DATA26_MASK, Register
from ..widgets.field_table import FieldTable
from ..widgets.status_indicator import StatusIndicator


class RegTab(QWidget):
    sig_read = Signal(int, int)              # eeprom addr, shadow addr (-1 = none)
    sig_write_eeprom = Signal(int, int, bool)  # addr, data26, force
    sig_write_ram = Signal(int, int)         # shadow addr, data26

    def __init__(self, register: Register, parent=None):
        super().__init__(parent)
        self._reg = register
        self._busy = False
        self._enabled = False
        self._updating_raw = False

        shadow = register.shadow_addr
        columns = [("eeprom", f"EEPROM 0x{register.addr:02X}")]
        if shadow is not None:
            columns.append(("shadow", f"Shadow 0x{shadow:02X}"))

        root = QVBoxLayout(self)

        self.table = FieldTable(register, columns, editable=True)
        root.addWidget(self.table, 1)

        # raw-hex editor, two-way synced with the field editors
        raw_row = QHBoxLayout()
        raw_row.addWidget(QLabel("Edit value (26-bit hex):"))
        self.raw_edit = QLineEdit("0x0000000")
        self.raw_edit.setMaximumWidth(140)
        raw_row.addWidget(self.raw_edit)
        raw_row.addStretch(1)
        root.addLayout(raw_row)

        # WRITE_LOCK guard checkbox (EE_CUST0 only)
        self._has_write_lock = any(f.name == "WRITE_LOCK"
                                   for f in register.fields)
        if self._has_write_lock:
            self.lock_enable_cb = QCheckBox("Enable WRITE_LOCK editing "
                                            "(one-time-programmable — caution)")
            self.lock_enable_cb.toggled.connect(
                lambda on: self.table.set_editor_enabled("WRITE_LOCK", on))
            self.table.set_editor_enabled("WRITE_LOCK", False)
            root.addWidget(self.lock_enable_cb)

        # buttons + indicators
        btn_row = QHBoxLayout()
        self.read_btn = QPushButton("Read")
        self.read_btn.setProperty("buttonRole", "safe")
        self.write_btn = QPushButton(f"Write EEPROM 0x{register.addr:02X}")
        btn_row.addWidget(self.read_btn)
        btn_row.addWidget(self.write_btn)
        self.write_sh_btn = None
        if shadow is not None:
            self.write_sh_btn = QPushButton(f"Write Shadow 0x{shadow:02X}")
            self.write_sh_btn.setToolTip(
                "Volatile RAM write — safe iterative tuning before EEPROM commit")
            btn_row.addWidget(self.write_sh_btn)
        self.ind_read = StatusIndicator("Read")
        self.ind_write = StatusIndicator("Write")
        btn_row.addStretch(1)
        btn_row.addWidget(self.ind_read)
        btn_row.addWidget(self.ind_write)
        root.addLayout(btn_row)

        # wiring
        self.table.edited.connect(self._sync_raw_from_fields)
        self.raw_edit.editingFinished.connect(self._sync_fields_from_raw)
        self.read_btn.clicked.connect(self._on_read_clicked)
        self.write_btn.clicked.connect(self._on_write_eeprom_clicked)
        if self.write_sh_btn is not None:
            self.write_sh_btn.clicked.connect(self._on_write_shadow_clicked)

        self._update_controls()

    # ---------------------------------------------------- raw <-> fields

    def _sync_raw_from_fields(self) -> None:
        if self._updating_raw:
            return
        self.raw_edit.setText(f"0x{self.table.edit_raw():07X}")

    def _sync_fields_from_raw(self) -> None:
        try:
            raw = int(self.raw_edit.text(), 16)
        except ValueError:
            self._sync_raw_from_fields()   # restore last valid value
            return
        raw &= DATA26_MASK
        self._updating_raw = True
        try:
            self.table.set_edits_from_raw(raw)
        finally:
            self._updating_raw = False
        self.raw_edit.setText(f"0x{raw:07X}")

    # ------------------------------------------------------------ actions

    def _on_read_clicked(self) -> None:
        self.ind_read.set_state(StatusIndicator.ACTIVE, "Reading…")
        shadow = self._reg.shadow_addr
        self.sig_read.emit(self._reg.addr, -1 if shadow is None else shadow)

    def _confirm_write_lock(self) -> bool:
        text, ok = QInputDialog.getText(
            self, "PERMANENT WRITE_LOCK",
            "This write sets WRITE_LOCK[25]=1 and PERMANENTLY locks the\n"
            "device EEPROM. This cannot be undone.\n\nType LOCK to proceed:")
        return ok and text.strip() == "LOCK"

    def _on_write_eeprom_clicked(self) -> None:
        data = self.table.edit_raw()
        force = False
        if self._has_write_lock and (data >> 25) & 1:
            if not self._confirm_write_lock():
                self.ind_write.set_state(StatusIndicator.IDLE, "Cancelled")
                return
            force = True
        self.ind_write.set_state(StatusIndicator.ACTIVE, "WEEP…")
        self.sig_write_eeprom.emit(self._reg.addr, data, force)

    def _on_write_shadow_clicked(self) -> None:
        self.ind_write.set_state(StatusIndicator.ACTIVE, "WRAM…")
        self.sig_write_ram.emit(self._reg.shadow_addr, self.table.edit_raw())

    # ------------------------------------------------- worker feedback

    @Slot(int, int, str)
    def on_read_result(self, addr: int, data: int, ecc: str) -> None:
        if addr == self._reg.addr:
            self.table.set_values("eeprom", data)
            # prefill editors with the live EEPROM value (edits start from it)
            self.table.set_edits_from_raw(data & DATA26_MASK)
            self._sync_raw_from_fields()
        elif addr == self._reg.shadow_addr:
            self.table.set_values("shadow", data)

    @Slot(int, bool)
    def on_reg_read_done(self, addr: int, ok: bool) -> None:
        if addr == self._reg.addr:
            self.ind_read.set_state(
                StatusIndicator.OK if ok else StatusIndicator.FAIL)

    @Slot(int, bool, str)
    def on_write_done(self, addr: int, ok: bool, msg: str) -> None:
        if addr in (self._reg.addr, self._reg.shadow_addr):
            if ok:
                self.ind_write.set_state(StatusIndicator.OK, "Verified")
            else:
                self.ind_write.set_state(StatusIndicator.FAIL)
            self.ind_write.setToolTip(msg)

    @Slot(bool)
    def on_busy(self, busy: bool) -> None:
        self._busy = busy
        self._update_controls()

    @Slot(bool)
    def on_device_enabled(self, enabled: bool) -> None:
        self._enabled = enabled
        self._update_controls()

    @Slot(object)
    def on_snapshot_loaded(self, values: dict) -> None:
        """Populate the Edit column from a loaded file snapshot (plan §8.7)."""
        if self._reg.addr in values:
            self.table.set_edits_from_raw(values[self._reg.addr] & DATA26_MASK)
            self._sync_raw_from_fields()

    def _update_controls(self) -> None:
        allow = self._enabled and not self._busy
        self.read_btn.setEnabled(allow)
        self.write_btn.setEnabled(allow)
        if self.write_sh_btn is not None:
            self.write_sh_btn.setEnabled(allow)
