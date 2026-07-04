# ACS37610 Programmer — Host Application

Python host for the ESP32-S3 ACS37610 programmer. Spec: `Documents/ACS37610_GUI_Development_Plan_v1.md` (v1.1).

## Setup

```bat
py -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

## Layers

| Module | Phase | Responsibility |
|--------|-------|----------------|
| `acs_gui/transport.py` | G2 | Serial port + line framing, single in-flight command |
| `acs_gui/protocol.py`  | G2 | Typed client: `idn/status/power_on/auth/read_register/write_ram/write_eeprom`; `ERR` → typed exceptions |
| `acs_gui/registers.py` | G3 | Register/field model + encode/decode codec (plan §6) |
| GUI (`app.py`, tabs)   | G4+ | Not yet implemented |

## Tests (no hardware needed)

```bat
cd host
.venv\Scripts\python.exe -m pytest
```

## Hardware smoke test (programmer on a COM port)

```bat
cd host
.venv\Scripts\python.exe hw_smoke.py COM5
```

Runs `*IDN?` → `PWRON` → `AUTH` → decoded read of all six registers → `PWROFF`. Read-only.

## Scripting example

```python
from acs_gui.transport import SerialTransport
from acs_gui.protocol import ProtocolClient
from acs_gui import registers

t = SerialTransport(); t.open("COM5")
c = ProtocolClient(t)
c.power_on(); c.auth()
r = c.read_register(0x09)
print(hex(r.data), registers.decode(0x09, r.data))
```
