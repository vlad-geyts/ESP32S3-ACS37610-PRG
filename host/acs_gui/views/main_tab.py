"""MAIN tab — programmer dashboard (GUI plan §7.1, v1.1).

Gating rule (v1.1): after application start / Power Off / disconnect, all
data controls (Read All, Save to File, Load from File) stay disabled until
ENABLE DEVICE succeeds. Connection and power controls remain available.

All button clicks emit request signals; MainWindow connects them to the
ProgrammerWorker slots (queued, cross-thread). Worker feedback arrives via
the on_* slots below.
"""
from __future__ import annotations

from PySide6.QtCore import Signal, Slot
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QComboBox, QGridLayout, QGroupBox, QHBoxLayout, QPlainTextEdit,
    QPushButton, QVBoxLayout, QWidget,
)

from .. import registers
from ..transport import list_ports
from ..widgets.status_indicator import StatusIndicator


class MainTab(QWidget):
    # requests to the worker (connected in MainWindow, queued across threads)
    sig_connect = Signal(str)
    sig_disconnect = Signal()
    sig_power_on = Signal()
    sig_power_off = Signal()
    sig_enable_device = Signal()
    sig_read_all = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._connected = False
        self._powered = False
        self._device_enabled = False
        self._busy = False
        self._build_ui()
        self.refresh_ports()
        self._update_controls()

    # ------------------------------------------------------------------ UI

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)

        # --- Connection -------------------------------------------------
        conn_box = QGroupBox("Connection")
        conn_row = QHBoxLayout(conn_box)
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(120)
        self.refresh_btn = QPushButton("Refresh")
        self.connect_btn = QPushButton("Connect")
        self.ind_comm = StatusIndicator("Comm")
        conn_row.addWidget(self.port_combo, 1)
        conn_row.addWidget(self.refresh_btn)
        conn_row.addWidget(self.connect_btn)
        conn_row.addWidget(self.ind_comm)
        root.addWidget(conn_box)

        # --- DUT control --------------------------------------------------
        ctrl_box = QGroupBox("DUT Control")
        grid = QGridLayout(ctrl_box)
        self.pwr_on_btn = QPushButton("Power On")
        self.pwr_off_btn = QPushButton("Power Off")
        self.ind_power = StatusIndicator("Power")
        self.enable_btn = QPushButton("ENABLE DEVICE")
        self.enable_btn.setToolTip(
            "Send the Access Code — required after every power-up before any"
            " read/write. Data controls stay disabled until this succeeds.")
        self.ind_device = StatusIndicator("Device")
        grid.addWidget(self.pwr_on_btn, 0, 0)
        grid.addWidget(self.pwr_off_btn, 0, 1)
        grid.addWidget(self.ind_power, 0, 2)
        grid.addWidget(self.enable_btn, 1, 0, 1, 2)
        grid.addWidget(self.ind_device, 1, 2)
        root.addWidget(ctrl_box)

        # --- Data (gated) --------------------------------------------------
        data_box = QGroupBox("Data")
        dgrid = QGridLayout(data_box)
        self.read_all_btn = QPushButton("Read All")
        self.ind_read_all = StatusIndicator("Read All")
        self.save_btn = QPushButton("Save to File")
        self.load_btn = QPushButton("Load from File")
        self.ind_load = StatusIndicator("Load")
        for b in (self.save_btn, self.load_btn):
            b.setToolTip("Implemented in phase G6")
        dgrid.addWidget(self.read_all_btn, 0, 0, 1, 2)
        dgrid.addWidget(self.ind_read_all, 0, 2)
        dgrid.addWidget(self.save_btn, 1, 0)
        dgrid.addWidget(self.load_btn, 1, 1)
        dgrid.addWidget(self.ind_load, 1, 2)
        root.addWidget(data_box)

        # --- Activity log --------------------------------------------------
        log_box = QGroupBox("Activity Log")
        log_lay = QVBoxLayout(log_box)
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(2000)
        mono = QFont("Consolas")
        mono.setStyleHint(QFont.StyleHint.Monospace)
        self.log_view.setFont(mono)
        log_lay.addWidget(self.log_view)
        root.addWidget(log_box, 1)

        # --- button color roles (see stylesheet in app.py) -----------------
        self.enable_btn.setProperty("buttonRole", "gate")     # amber
        for safe in (self.refresh_btn, self.read_all_btn, self.save_btn):
            safe.setProperty("buttonRole", "safe")            # green, read-only

        # --- wiring ---------------------------------------------------------
        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.connect_btn.clicked.connect(self._on_connect_clicked)
        self.pwr_on_btn.clicked.connect(self.sig_power_on)
        self.pwr_off_btn.clicked.connect(self.sig_power_off)
        self.enable_btn.clicked.connect(self._on_enable_clicked)
        self.read_all_btn.clicked.connect(self._on_read_all_clicked)

    # ------------------------------------------------------------- actions

    @Slot()
    def refresh_ports(self) -> None:
        current = self.port_combo.currentText()
        self.port_combo.clear()
        self.port_combo.addItems(list_ports())
        if current:
            idx = self.port_combo.findText(current)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)

    def _on_connect_clicked(self) -> None:
        if self._connected:
            self.sig_disconnect.emit()
        else:
            port = self.port_combo.currentText()
            if port:
                self.ind_comm.set_state(StatusIndicator.ACTIVE, "Connecting…")
                self.sig_connect.emit(port)

    def _on_enable_clicked(self) -> None:
        self.ind_device.set_state(StatusIndicator.ACTIVE, "AUTH…")
        self.sig_enable_device.emit()

    def _on_read_all_clicked(self) -> None:
        self.ind_read_all.set_state(StatusIndicator.ACTIVE, "Reading…")
        self.sig_read_all.emit()

    # ------------------------------------------------- worker feedback

    @Slot(str)
    def on_log(self, line: str) -> None:
        self.log_view.appendPlainText(line)

    @Slot(str)
    def on_error(self, msg: str) -> None:
        self.log_view.appendPlainText(f"ERROR: {msg}")

    @Slot(bool)
    def on_busy(self, busy: bool) -> None:
        self._busy = busy
        self._update_controls()

    @Slot(bool, str)
    def on_conn_changed(self, connected: bool, info: str) -> None:
        self._connected = connected
        if connected:
            self.ind_comm.set_state(StatusIndicator.OK, info)
            self.ind_power.set_state(StatusIndicator.FAIL, "OFF")
        else:
            self._powered = False
            self._device_enabled = False
            if info == "disconnected":
                self.ind_comm.set_state(StatusIndicator.IDLE)
            else:
                self.ind_comm.set_state(StatusIndicator.FAIL)
                self.on_error(f"connect: {info}")
            self.ind_power.set_state(StatusIndicator.IDLE)
            self.ind_device.set_state(StatusIndicator.IDLE)
        self.connect_btn.setText("Disconnect" if connected else "Connect")
        self._update_controls()

    @Slot(bool)
    def on_power_changed(self, on: bool) -> None:
        self._powered = on
        if self._connected:
            if on:
                self.ind_power.set_state(StatusIndicator.OK, "ON")
            else:
                self.ind_power.set_state(StatusIndicator.FAIL, "OFF")
        self._update_controls()

    @Slot(bool)
    def on_device_enabled(self, enabled: bool) -> None:
        self._device_enabled = enabled
        if enabled:
            self.ind_device.set_state(StatusIndicator.OK, "ENABLED")
        elif self._connected:
            self.ind_device.set_state(StatusIndicator.IDLE, "DISABLED")
        self._update_controls()

    @Slot(int, int, str)
    def on_read_result(self, addr: int, data: int, ecc: str) -> None:
        reg = registers.BY_ADDR[addr]
        kind = "shadow" if addr == reg.shadow_addr else reg.access
        self.log_view.appendPlainText(
            f"  {reg.name} [{kind}] @0x{addr:02X}: DATA=0x{data:08X} ECC={ecc}")

    @Slot(bool)
    def on_read_all_done(self, ok: bool) -> None:
        if ok:
            self.ind_read_all.set_state(StatusIndicator.OK, "Completed")
        else:
            self.ind_read_all.set_state(StatusIndicator.FAIL)

    # --------------------------------------------------------- gating

    def _update_controls(self) -> None:
        busy = self._busy
        self.port_combo.setEnabled(not self._connected and not busy)
        self.refresh_btn.setEnabled(not self._connected and not busy)
        self.connect_btn.setEnabled(not busy)

        self.pwr_on_btn.setEnabled(self._connected and not busy
                                   and not self._powered)
        self.pwr_off_btn.setEnabled(self._connected and not busy
                                    and self._powered)
        self.enable_btn.setEnabled(self._connected and self._powered
                                   and not busy)

        # v1.1 gating: data controls dead until ENABLE DEVICE succeeds
        gated_ok = self._device_enabled and not busy
        self.read_all_btn.setEnabled(gated_ok)
        # Save/Load stay disabled until G6 regardless of gating
        self.save_btn.setEnabled(False)
        self.load_btn.setEnabled(False)
