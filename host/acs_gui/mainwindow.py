"""Main window: tab container + worker-thread wiring (GUI plan §5/§7)."""
from __future__ import annotations

from PySide6.QtCore import QThread
from PySide6.QtWidgets import QMainWindow, QTabWidget

from . import __version__, registers
from .views.fault_tab import FaultTab
from .views.main_tab import MainTab
from .views.reg_tab import RegTab
from .worker import ProgrammerWorker


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ACS37610 Programmer")
        self.resize(860, 680)

        # --- worker thread: owns all serial I/O (plan §2.1) ---------------
        self._thread = QThread(self)
        self._worker = ProgrammerWorker()
        self._worker.moveToThread(self._thread)
        self._thread.start()

        # --- tabs -----------------------------------------------------------
        tabs = QTabWidget()
        self.main_tab = MainTab()
        tabs.addTab(self.main_tab, "Main")
        self.reg_tabs = [RegTab(registers.EE_CUST0),
                         RegTab(registers.EE_CUST1),
                         RegTab(registers.EE_CUST2)]
        for tab, reg in zip(self.reg_tabs,
                            (registers.EE_CUST0, registers.EE_CUST1,
                             registers.EE_CUST2)):
            tabs.addTab(tab, reg.name)
        self.fault_tab = FaultTab()
        tabs.addTab(self.fault_tab, "FAULT_STATUS")
        self.setCentralWidget(tabs)
        self.statusBar().showMessage(f"acs_gui {__version__}")

        # --- UI -> worker (queued: emitted in UI thread, run in worker) ----
        w = self._worker
        self.main_tab.sig_connect.connect(w.op_connect)
        self.main_tab.sig_disconnect.connect(w.op_disconnect)
        self.main_tab.sig_power_on.connect(w.op_power_on)
        self.main_tab.sig_power_off.connect(w.op_power_off)
        self.main_tab.sig_enable_device.connect(w.op_enable_device)
        self.main_tab.sig_read_all.connect(w.op_read_all)
        for tab in self.reg_tabs:
            tab.sig_read.connect(w.op_read_register)
            tab.sig_write_eeprom.connect(w.op_write_eeprom_verified)
            tab.sig_write_ram.connect(w.op_write_ram_verified)
        self.fault_tab.sig_read.connect(w.op_read_register)

        # --- worker -> UI ----------------------------------------------------
        w.log.connect(self.main_tab.on_log)
        w.op_error.connect(self.main_tab.on_error)
        w.busy_changed.connect(self.main_tab.on_busy)
        w.conn_changed.connect(self.main_tab.on_conn_changed)
        w.power_changed.connect(self.main_tab.on_power_changed)
        w.device_enabled_changed.connect(self.main_tab.on_device_enabled)
        w.read_result.connect(self.main_tab.on_read_result)
        w.read_all_done.connect(self.main_tab.on_read_all_done)
        for tab in (*self.reg_tabs, self.fault_tab):
            w.read_result.connect(tab.on_read_result)
            w.reg_read_done.connect(tab.on_reg_read_done)
            w.busy_changed.connect(tab.on_busy)
            w.device_enabled_changed.connect(tab.on_device_enabled)
        for tab in self.reg_tabs:
            w.write_done.connect(tab.on_write_done)

    def closeEvent(self, event) -> None:
        # Queued before quit(), so the worker powers the DUT off and closes
        # the port before its event loop stops.
        self.main_tab.sig_disconnect.emit()
        self._thread.quit()
        self._thread.wait(3000)
        super().closeEvent(event)
