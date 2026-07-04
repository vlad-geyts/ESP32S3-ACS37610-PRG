"""SerialTransport framing tests against a fake serial object (no hardware)."""
import pytest

from acs_gui.transport import SerialTransport, TransportError, TransportTimeout


class FakeSerial:
    """Duck-types the pyserial surface SerialTransport uses."""

    def __init__(self, replies=b""):
        self.rx = bytearray(replies)   # bytes the "firmware" will send us
        self.written = bytearray()     # bytes we wrote to the port
        self.stale = bytearray()       # pre-existing junk in the input buffer
        self.timeout = 0.1
        self.closed = False

    @property
    def in_waiting(self):
        return len(self.stale) + len(self.rx)

    def reset_input_buffer(self):
        self.stale.clear()

    def write(self, data):
        self.written += data

    def read(self, n=1):
        take, self.stale = self.stale[:n], self.stale[n:]
        if len(take) < n:
            more = n - len(take)
            take2, self.rx = self.rx[:more], self.rx[more:]
            take += take2
        return bytes(take)

    def close(self):
        self.closed = True


def make(replies=b""):
    t = SerialTransport()
    fake = FakeSerial(replies)
    t.attach(fake)
    return t, fake


def test_send_appends_newline_and_strips_reply():
    t, fake = make(b"OK\r\n")
    assert t.send("PWRON") == "OK"
    assert fake.written == b"PWRON\n"


def test_send_strips_input_whitespace():
    t, fake = make(b"ID ACS37610-PRG 1.0.0\n")
    assert t.send("  *IDN?  ") == "ID ACS37610-PRG 1.0.0"
    assert fake.written == b"*IDN?\n"


def test_stale_input_discarded_before_send():
    t, fake = make(b"OK\n")
    fake.stale += b"[BOOT] banner leftover\n"   # e.g. firmware boot print
    assert t.send("PWRON") == "OK"


def test_timeout_when_no_reply():
    t, _ = make(b"")
    with pytest.raises(TransportTimeout):
        t.send("PWRON", timeout=0.05)


def test_timeout_on_partial_line():
    t, _ = make(b"OK")   # never newline-terminated
    with pytest.raises(TransportTimeout):
        t.send("PWRON", timeout=0.05)


def test_send_requires_open():
    t = SerialTransport()
    with pytest.raises(TransportError):
        t.send("PWRON")


def test_double_open_rejected():
    t, _ = make()
    with pytest.raises(TransportError):
        t.attach(FakeSerial())


def test_close_releases_port():
    t, fake = make()
    t.close()
    assert fake.closed
    assert not t.is_open
    # reusable after close
    t.attach(FakeSerial(b"OK\n"))
    assert t.send("PWRON") == "OK"
