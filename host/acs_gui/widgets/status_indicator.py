"""Reusable colored status indicator (GUI plan §5.2).

States: IDLE (grey), ACTIVE (blue, operation in progress), OK (green),
FAIL (red). Used for Comm, Power, Device, Read All and Load status.
"""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QLabel


class StatusIndicator(QLabel):
    IDLE = "idle"
    ACTIVE = "active"
    OK = "ok"
    FAIL = "fail"

    _COLORS = {
        IDLE:   "#8a8a8a",
        ACTIVE: "#1976d2",
        OK:     "#2e7d32",
        FAIL:   "#c62828",
    }
    _DEFAULT_TEXT = {IDLE: "—", ACTIVE: "BUSY", OK: "OK", FAIL: "FAIL"}

    def __init__(self, title: str, parent=None):
        super().__init__(parent)
        self._title = title
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumWidth(160)
        self.set_state(self.IDLE)

    def set_state(self, state: str, text: str | None = None) -> None:
        label = text if text is not None else self._DEFAULT_TEXT[state]
        self.setText(f"{self._title}: {label}")
        self.setStyleSheet(
            f"QLabel {{ background: {self._COLORS[state]}; color: white;"
            f" border-radius: 4px; padding: 4px 8px; font-weight: bold; }}"
        )
