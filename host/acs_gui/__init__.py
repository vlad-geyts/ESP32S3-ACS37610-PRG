"""ACS37610 programmer host application (GUI plan v1.1).

G2/G3 layers (no Qt dependency — scriptable and unit-testable):
  transport.py — serial port ownership + line framing
  protocol.py  — typed client for the ASCII command protocol (plan §3)
  registers.py — register/field model + 26/32-bit codec (plan §6)
"""

__version__ = "1.0.0"
