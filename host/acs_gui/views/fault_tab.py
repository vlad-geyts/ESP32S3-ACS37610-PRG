"""FAULT_STATUS tab — read-only decoded flags + TEMP_OUT (GUI plan §7.5).

Live *_STAT bits and latched *_EV event bits render as colored flags:
grey = 0 (clear), red = 1 (active/latched). Volatile register — Read only.
Also refreshed automatically by Read All (listens to the same read_result).
"""
from __future__ import annotations

from PySide6.QtCore import Signal, Slot
from PySide6.QtWidgets import (
    QGridLayout, QGroupBox, QHBoxLayout, QLabel, QPushButton, QVBoxLayout,
    QWidget,
)

from .. import registers
from ..widgets.status_indicator import StatusIndicator

_STATUS_FLAGS = ("UV_STAT", "OV_STAT", "OC_STAT", "OT_STAT", "FP_STAT")
_EVENT_FLAGS = ("UV_EV", "OV_EV", "OC_EV", "OT_EV", "FP_EV")


class FaultTab(QWidget):
    sig_read = Signal(int, int)   # (0x20, -1) — same op as the register tabs

    def __init__(self, parent=None):
        super().__init__(parent)
        self._busy = False
        self._enabled = False

        root = QVBoxLayout(self)

        top = QHBoxLayout()
        self.read_btn = QPushButton("Read")
        self.read_btn.setProperty("buttonRole", "safe")
        self.ind_read = StatusIndicator("Read")
        self.raw_label = QLabel("RAW: —")
        self.temp_label = QLabel("TEMP_OUT: —")
        top.addWidget(self.read_btn)
        top.addWidget(self.ind_read)
        top.addStretch(1)
        top.addWidget(self.temp_label)
        top.addWidget(self.raw_label)
        root.addLayout(top)

        self._flags: dict[str, StatusIndicator] = {}
        for title, names in (("Live status", _STATUS_FLAGS),
                             ("Latched events", _EVENT_FLAGS)):
            box = QGroupBox(title)
            grid = QGridLayout(box)
            for col, name in enumerate(names):
                ind = StatusIndicator(name)
                ind.set_state(StatusIndicator.IDLE, "—")
                grid.addWidget(ind, 0, col)
                self._flags[name] = ind
            root.addWidget(box)
        root.addStretch(1)

        self.read_btn.clicked.connect(self._on_read_clicked)
        self._update_controls()

    def _on_read_clicked(self) -> None:
        self.ind_read.set_state(StatusIndicator.ACTIVE, "Reading…")
        self.sig_read.emit(registers.FAULT_STATUS.addr, -1)

    # ------------------------------------------------- worker feedback

    @Slot(int, int, str)
    def on_read_result(self, addr: int, data: int, ecc: str) -> None:
        if addr != registers.FAULT_STATUS.addr:
            return
        fields = registers.decode(addr, data)
        self.raw_label.setText(f"RAW: 0x{data:08X}")
        self.temp_label.setText(f"TEMP_OUT: {fields['TEMP_OUT']}"
                                f" (0x{fields['TEMP_OUT']:03X})")
        for name, ind in self._flags.items():
            if fields[name]:
                ind.set_state(StatusIndicator.FAIL, "1")
            else:
                ind.set_state(StatusIndicator.IDLE, "0")

    @Slot(int, bool)
    def on_reg_read_done(self, addr: int, ok: bool) -> None:
        if addr == registers.FAULT_STATUS.addr:
            self.ind_read.set_state(
                StatusIndicator.OK if ok else StatusIndicator.FAIL)

    @Slot(bool)
    def on_busy(self, busy: bool) -> None:
        self._busy = busy
        self._update_controls()

    @Slot(bool)
    def on_device_enabled(self, enabled: bool) -> None:
        self._enabled = enabled
        self._update_controls()

    def _update_controls(self) -> None:
        self.read_btn.setEnabled(self._enabled and not self._busy)
