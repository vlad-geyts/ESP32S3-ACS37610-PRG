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
| `acs_gui/worker.py`    | G4 | Qt worker thread owning all serial I/O; UI talks via signals |
| `acs_gui/mainwindow.py` + `views/main_tab.py` | G4 | MAIN tab: connect, Power On/Off, ENABLE DEVICE gating, Read All, activity log |
| `views/reg_tab.py` + `views/fault_tab.py` + `widgets/field_table.py` | G5 | Register tabs: field editors, raw-hex sync, Read / Write with read-back verify, WRITE_LOCK guard |
| `acs_gui/storage.py` | G6 | JSON snapshot format (plan §9); MAIN-tab Save to File / Load from File |

## Run the GUI

```bat
cd host
.venv\Scripts\python.exe -m acs_gui.app
```

## Portable build (G7) — run on any PC without Python/VS Code

```bat
cd host
build_exe.bat
```

Produces `dist\ACS37610-Programmer\ACS37610-Programmer.exe` — copy the whole
folder to any Windows 10/11 x64 machine and run the exe. No install, no
Python, no drivers beyond the serial port's own (CH343 driver for the UART
bridge; the native-USB firmware uses Windows' built-in `usbser`).

## Firmware transports (G7)

| PlatformIO env | Serial goes to | COM port on Windows |
|----------------|----------------|--------------------|
| `esp32-s3-devkitc-1-n16r8v` (default) | CH343 UART bridge (GPIO43/44) | CH343 driver port |
| `esp32-s3-usb` | Native USB Serial/JTAG (USB-OTG connector, GPIO19/20) | Built-in `usbser` port |

Flash the native-USB variant with `pio run -e esp32-s3-usb -t upload`, connect
the **USB-OTG** connector, and qualify with `hw_smoke.py` on the new COM port.
The protocol is identical — the GUI just selects a different port. Note: the
native-USB port disappears while the ESP32 reboots (re-enumerates a moment
later), unlike the CH343 port which stays present.

Workflow: select COM port → Connect → Power On → **ENABLE DEVICE** (data controls stay
grey until it succeeds) → Read All. The activity log shows every protocol line.

- **Save to File** reads all six registers and writes a JSON snapshot (any read failure
  aborts — no partial files). A prompt asks for a **device label** (e.g. "Motor board #3,
  Phase U") stored as `device_id` — essential on multi-sensor boards, where each snapshot
  belongs to one specific sensor.
- **Load from File** shows that label in a confirmation dialog before writing — verify the
  connected PROG pin matches the labeled sensor.
- **Load from File** validates the snapshot, writes it to EEPROM `0x09`/`0x0A`/`0x0B`
  (each if present in the file), reads back and verifies; the Load indicator shows the
  outcome. Loaded values also populate the tab editors. A snapshot with WRITE_LOCK[25]=1
  requires typing `LOCK` to confirm.
- **Hand-editing snapshots:** `raw` is authoritative; `fields` is an informational decode.
  Edit `raw` to change a value — a `fields` entry that disagrees with `raw` makes Load fail
  with an error naming the field (so edits can't be silently ignored).

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
