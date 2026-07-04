"""Serial transport for the ACS37610 programmer (GUI plan §5.2).

Synchronous, thread-safe core: owns the serial port, frames '\n'-terminated
command/response lines, enforces single-in-flight via a lock. Usable directly
from scripts and the REPL for hardware bring-up (G2 "scriptable" deliverable).

The Qt integration (worker QThread emitting connection_changed/line_received
signals) arrives with the GUI in G4 — that thread will own an instance of this
class; nothing here imports Qt.
"""
from __future__ import annotations

import threading
import time

import serial
from serial.tools import list_ports as _list_ports

DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT_S = 2.0


class TransportError(Exception):
    """Serial link problem: port not open, write failure, garbled framing."""


class TransportTimeout(TransportError):
    """No complete response line arrived within the deadline."""


def list_ports() -> list[str]:
    """Names of serial ports present on this machine (e.g. ['COM5'])."""
    return [p.device for p in _list_ports.comports()]


class SerialTransport:
    """Owns one serial port; sends a command line, returns the response line.

    The firmware answers exactly one line per command (plan §3.1), so the
    API is strictly request/response. All access is serialized by a lock —
    safe to call from any thread, one command in flight at a time.
    """

    def __init__(self) -> None:
        self._ser: serial.Serial | None = None
        self._lock = threading.Lock()

    # -- lifecycle ----------------------------------------------------------

    def open(self, port: str, baud: int = DEFAULT_BAUD) -> None:
        """Open a real serial port. Raises TransportError if already open."""
        with self._lock:
            if self._ser is not None:
                raise TransportError("transport already open")
            try:
                self._ser = serial.Serial(port=port, baudrate=baud, timeout=0.1)
            except serial.SerialException as exc:
                raise TransportError(f"cannot open {port}: {exc}") from exc

    def attach(self, ser) -> None:
        """Attach an already-open serial-like object (tests, custom setups).

        Needs: read(n)->bytes, write(bytes), reset_input_buffer(), close(),
        and writable 'timeout' / readable 'in_waiting' attributes.
        """
        with self._lock:
            if self._ser is not None:
                raise TransportError("transport already open")
            self._ser = ser

    def close(self) -> None:
        with self._lock:
            if self._ser is not None:
                try:
                    self._ser.close()
                finally:
                    self._ser = None

    @property
    def is_open(self) -> bool:
        return self._ser is not None

    # -- request/response ---------------------------------------------------

    def send(self, line: str, timeout: float = DEFAULT_TIMEOUT_S) -> str:
        """Send one command line, return the one response line (stripped).

        Stale input (e.g. firmware boot banner) is discarded before sending,
        so the next line received is the answer to this command.
        """
        with self._lock:
            ser = self._ser
            if ser is None:
                raise TransportError("transport not open")

            try:
                ser.reset_input_buffer()
                ser.write(line.strip().encode("ascii") + b"\n")
            except (serial.SerialException, OSError) as exc:
                raise TransportError(f"write failed: {exc}") from exc

            deadline = time.monotonic() + timeout
            buf = bytearray()
            while b"\n" not in buf:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TransportTimeout(
                        f"no response to {line.strip()!r} within {timeout} s")
                ser.timeout = min(0.1, remaining)
                try:
                    chunk = ser.read(max(1, getattr(ser, "in_waiting", 0)))
                except (serial.SerialException, OSError) as exc:
                    raise TransportError(f"read failed: {exc}") from exc
                buf += chunk

            reply = buf.split(b"\n", 1)[0].decode("ascii", errors="replace")
            return reply.strip()
