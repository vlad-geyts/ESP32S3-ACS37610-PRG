"""Entry point:  python -m acs_gui.app   (run from host/, venv active)."""
import sys

from PySide6.QtWidgets import QApplication

from .mainwindow import MainWindow

# Explicit button styling, independent of the Windows color palette: the
# native "windowsvista" style renders disabled buttons nearly identical to
# enabled ones. Color code (buttons opt in via the "buttonRole" property):
#   blue  (default) = state-changing actions (Connect, Power, Load from File)
#   green ("safe")  = read-only actions (Refresh, Read All, Save to File)
#   amber ("gate")  = ENABLE DEVICE — the gate that unlocks the data controls
#   grey            = disabled (rule is last so it wins for every role)
_STYLESHEET = """
QPushButton {
    background: #1976d2; color: white; font-weight: 600;
    border: none; border-radius: 4px; padding: 6px 14px; min-height: 18px;
}
QPushButton:hover   { background: #1e88e5; }
QPushButton:pressed { background: #0d47a1; }

QPushButton[buttonRole="safe"]          { background: #2e7d32; }
QPushButton[buttonRole="safe"]:hover    { background: #388e3c; }
QPushButton[buttonRole="safe"]:pressed  { background: #1b5e20; }

QPushButton[buttonRole="gate"]          { background: #ef6c00; }
QPushButton[buttonRole="gate"]:hover    { background: #f57c00; }
QPushButton[buttonRole="gate"]:pressed  { background: #e65100; }

QPushButton:disabled { background: #e4e4e4; color: #a6a6a6; }
"""


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("ACS37610 Programmer")
    # Fusion: palette-independent cross-platform style; predictable with CSS.
    app.setStyle("Fusion")
    app.setStyleSheet(_STYLESHEET)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
