# ACS37610 Programmer — User Guide

**Release:** 1.0.0 (application 1.0.0 · firmware 1.0.0)
**Applies to:** ACS37610-Programmer Windows application + ESP32-S3 programmer hardware
**Audience:** operators reading, calibrating and programming ACS37610 current sensors

---

## 1. Overview

The ACS37610 Programmer reads and writes the customer registers of the Allegro
**ACS37610** hall-effect current sensor through its single-wire PROG pin. The system
has two parts:

- **Programmer hardware** — an ESP32-S3 module that generates the sensor's Manchester-encoded
  serial protocol on the PROG line and controls the sensor's 3.3 V supply rail.
- **ACS37610-Programmer application** — a Windows program that connects to the programmer
  over USB and provides register displays, field editors, EEPROM programming with automatic
  verification, and snapshot save/load to files.

With it you can:
- Read all sensor registers and see every bit-field decoded
- Tune calibration values in volatile shadow registers (safe, lost at power-off)
- Commit values permanently to EEPROM, with automatic read-back verification
- Save a sensor's complete register set to a file and program it into another sensor
- Monitor the sensor's fault flags and die temperature

---

## 2. What You Need

| Item | Notes |
|------|-------|
| Programmer hardware | ESP32-S3 board with the ACS37610-PRG firmware 1.0.0 flashed |
| USB cable | To the PC. Two connectors exist on the programmer — see §3.2 |
| Target board | Any board carrying an ACS37610 with its PROG pin, GND and 3.3 V rail accessible |
| Windows PC | Windows 10/11, 64-bit. **No software installation required** |
| `ACS37610-Programmer` folder | The application — copied from the release location |

---

## 3. Installation

### 3.1 The application — no install

Copy the entire `ACS37610-Programmer` folder to the PC (any location, e.g. Desktop) and run
**`ACS37610-Programmer.exe`** inside it. There is nothing to install — no Python, no runtime,
no admin rights. Do not remove files from the folder; the exe needs the `_internal` directory
beside it.

### 3.2 USB connection and drivers

The programmer board has **two USB connectors**; which one to use depends on the firmware
variant flashed:

| Firmware variant | Use connector | Windows driver |
|------------------|--------------|----------------|
| **Native USB** (standard release) | **USB-OTG** (native USB) | Built-in (`usbser`) — nothing to install |
| UART-bridge variant | UART (CH343 bridge) | CH343 driver (install once) |

With the standard native-USB firmware, plug into the USB-OTG connector and Windows creates a
COM port automatically. Note the port number in Device Manager → *Ports (COM & LPT)* if unsure.

### 3.3 Connecting the sensor

Three wires from the programmer to the target board: **PROG**, **GND**, and the sensor's
**3.3 V supply** (the programmer switches this rail on and off — the sensor must be powered
*by the programmer*, not by the target board's own supply, during programming).

> **Boards with more than one ACS37610** (e.g. a 3-phase motor board with sensors on Phase U
> and Phase W): the PROG protocol is strictly point-to-point. The programmer talks to
> **whichever sensor's PROG pin is physically connected** — always double-check which phase
> you are clipped to before writing, and use snapshot labels (§8) to keep files per sensor.

---

## 4. Application Tour

The window has five tabs:

| Tab | Purpose |
|-----|---------|
| **Main** | Connection, power, device enable, Read All, Save/Load to file, activity log |
| **EE_CUST0** | Calibration register 0x09 + its shadow 0x19 (polarity, QVO, fine sensitivity, locks) |
| **EE_CUST1** | Calibration register 0x0A + its shadow 0x1A (overcurrent-fault thresholds) |
| **EE_CUST2** | Register 0x0B — 26-bit customer scratchpad |
| **FAULT_STATUS** | Read-only fault flags and die temperature (register 0x20) |

### 4.1 Color language

**Buttons** (a grey button is disabled — not available in the current state):

| Color | Meaning | Examples |
|-------|---------|----------|
| Blue | Changes device or connection state | Connect, Power On/Off, Write EEPROM, Load from File |
| Green | Read-only / safe | Refresh, Read, Read All, Save to File |
| Amber | The gate that unlocks everything else | ENABLE DEVICE |

**Status indicators:**

| Color | Meaning |
|-------|---------|
| Grey | Idle / not applicable |
| Blue | Operation in progress |
| Green | Success (Connected / ON / ENABLED / Completed / Verified / Saved) |
| Red | Failure — check the activity log for the reason |

### 4.2 Activity log

The Main tab's log shows every command sent (`>`), every reply (`<`), and every error. When
anything unexpected happens, the explanation is here. The log keeps the last 2000 lines.

---

## 5. Standard Workflow

Every session follows the same sequence — the interface enforces it by keeping buttons grey
until their prerequisites are met:

1. **Select the COM port** (use *Refresh* if you plugged in after starting the app) and click
   **Connect**. The Comm indicator turns green and shows the programmer's identity
   (`ACS37610-PRG 1.0.0`).
2. **Power On** — enables the sensor's 3.3 V rail. Power indicator green = ON.
3. **ENABLE DEVICE** (amber) — sends the sensor its access code, opening its serial port.
   The Device indicator turns green and the data controls (Read All, Save, Load, and all
   tab buttons) come alive.
4. Work: read, edit, write, save, load — see the following sections.
5. **Power Off** when finished. Disconnect and close the app.

Things to know:
- **ENABLE DEVICE must be repeated after every Power Off/On cycle** — the sensor forgets its
  port-open state when unpowered. The controls re-grey automatically to remind you.
- Closing the application (or clicking Disconnect) **automatically powers the sensor off** —
  the rig is never left energized by accident.
- One operation runs at a time; controls grey out briefly while a command is in flight.

---

## 6. Reading Registers

- **Read All** (Main tab) reads all six locations — both EE_CUST0/1 EEPROM registers and
  their shadows, EE_CUST2, and FAULT_STATUS — and fills every tab in one go.
- Each register tab's **Read** button refreshes just that tab (EEPROM and shadow together).
- The **EEPROM** and **Shadow** columns show the decoded value of every bit-field. At
  power-on the sensor loads shadows from EEPROM, so the columns normally match; they diverge
  after a shadow (RAM) write.
- **ECC**: EEPROM reads report the sensor's error-correction status. `ECC=OK` is normal;
  `ECC=FAIL` on a read indicates corrupted EEPROM content. Shadows and FAULT_STATUS show
  `ECC=NA` (not applicable).

---

## 7. Editing and Writing

### 7.1 Editing

Each register tab has an **Edit** column — checkboxes for single-bit fields, numeric entry
for multi-bit fields (limits enforced automatically). Below the table, the **raw hex field**
shows the combined 26-bit value; you can type a hex word there and the field editors update,
or edit fields and watch the hex update. Reading a register pre-fills the editors with the
current EEPROM value, so edits always start from what is actually in the device.

### 7.2 Write Shadow — safe iteration

**Write Shadow** writes the edited value to the volatile shadow register (RAM). It takes
effect immediately, is verified by read-back (Write indicator green *Verified*), and is
**lost at power-off** — ideal for trying calibration values before committing them.

### 7.3 Write EEPROM — permanent

**Write EEPROM** commits the edited value permanently. The programmer automatically waits
the EEPROM programming time, reads the register back, compares, and checks ECC — the Write
indicator shows green *Verified* only if all of that passed. Expect roughly a tenth of a
second per write.

### 7.4 WRITE_LOCK — read this before touching it

`WRITE_LOCK` (EE_CUST0, bit 25) **permanently and irreversibly locks the sensor's EEPROM**.
Once written to 1, the device can never be reprogrammed. Because of this the application
guards it three ways:

1. The WRITE_LOCK editor is disabled until you tick *Enable WRITE_LOCK editing*.
2. Any write that would set the bit demands you type **`LOCK`** in a confirmation dialog.
3. The programmer firmware independently refuses such writes without an explicit override.

Do not set this bit unless the sensor's calibration is final and locking is intended.

---

## 8. Saving and Loading Snapshots

### 8.1 Save to File

**Save to File** (Main tab) reads all six registers and writes them to a JSON file. You are
asked for a **device label** — on multi-sensor boards this is essential: enter which board
and which sensor the snapshot came from, e.g. `Motor board #3, Phase U`. If any register
read fails, no file is written (never a partial snapshot).

### 8.2 Load from File

**Load from File** programs a saved snapshot into the connected sensor:

1. The file is validated (format, addresses, value ranges) before anything is written.
2. A **confirmation dialog shows the snapshot's label, save time and target registers** —
   check that the label matches the sensor you are physically connected to, then confirm.
3. EE_CUST0 (0x09), EE_CUST1 (0x0A) and EE_CUST2 (0x0B) are written to EEPROM, each verified
   by read-back. The Load indicator ends green **Verified** or red with the reason logged.
4. The loaded values also appear in the tab editors.

A snapshot whose EE_CUST0 value has WRITE_LOCK set triggers the same typed-`LOCK` guard as
manual writes.

### 8.3 Editing snapshot files by hand

Snapshots are human-readable JSON. Each register entry has a `raw` value (authoritative) and
a decoded `fields` block (informational). **To change a value, edit `raw`.** If a `fields`
entry disagrees with `raw`, Load refuses the file and names the offending field — edits can
never be silently ignored.

---

## 9. FAULT_STATUS Tab

Read-only view of register 0x20:

- **Live status** flags (`UV/OV/OC/OT/FP_STAT`) — the condition exists *right now*.
- **Latched events** (`UV/OV/OC/OT/FP_EV`) — the condition occurred since power-on.
  **`UV_EV = 1` right after Power On is normal** — the supply ramp itself latches an
  undervoltage event. Live `UV_STAT` should still be 0.
- **TEMP_OUT** — raw 12-bit die-temperature reading (uncalibrated units; useful as a
  relative indication — you can watch it rise as the sensor warms).

Flags show grey `0` (clear) or red `1` (set). Read All refreshes this tab too.

---

## 10. Errors and Troubleshooting

### 10.1 Error codes (activity log)

| Code | Meaning | What to do |
|------|---------|-----------|
| `ERR PWROFF` | Command needs sensor power | Click Power On first |
| `ERR PORT` | Sensor's serial port not open | Click ENABLE DEVICE (required after every power cycle) |
| `ERR TIMEOUT` | Sensor did not answer on PROG | Check PROG/GND/3.3 V wiring and clip contact |
| `ERR CRC` | Reply arrived corrupted | Retry; persistent → check wiring quality/length |
| `ERR ECC` | Sensor reports EEPROM ECC fault | Register content corrupt; rewrite the register |
| `ERR VERIFY` | EEPROM read-back ≠ written value | Retry; persistent → sensor EEPROM may be at end of life or locked |
| `ERR LOCKED` | Write refused — WRITE_LOCK guard | Intentional lock attempt requires the confirmation flow (§7.4) |
| `ERR ARG` | Malformed command (internal) | Should not occur in normal GUI use |

### 10.2 Common situations

| Symptom | Likely cause / fix |
|---------|-------------------|
| No COM port in the list | Cable/connector; click Refresh; check Device Manager. CH343 variant needs its driver installed |
| Connect fails / times out | Wrong COM port selected — or **the firmware on the board doesn't match the connector**: the native-USB connector shows a COM port even when UART firmware is flashed, but commands then time out. Use the other port or reflash the matching firmware variant |
| Comm indicator goes red mid-session | USB cable disturbed; reconnect (Connect button) and repeat Power On + ENABLE DEVICE |
| Everything greyed out | ENABLE DEVICE hasn't been run since the last power-up — that's the gate |
| Sensor reads all zeros / timeouts with good wiring | Verify the sensor is powered by the programmer's rail (Power indicator ON) and the correct sensor's PROG pin is connected |
| Load ends red | See the log line — file validation error, or a write/verify failure on a specific register |

---

## 11. Version Reference

| Component | Version |
|-----------|---------|
| ACS37610-Programmer application | 1.0.0 (shown in the window status bar) |
| Programmer firmware | ACS37610-PRG 1.0.0 (shown by the Comm indicator after Connect) |
| Snapshot file format | `acs37610-registers` version 1 |

Firmware variants: `esp32-s3-usb` (native USB — standard release) and
`esp32-s3-devkitc-1-n16r8v` (CH343 UART bridge). Same protocol and features; only the USB
connector and Windows driver differ.

For developers: source, build instructions (firmware and application), test suites and design
documents live in the project repository — see `host/README.md` and
`Documents/ACS37610_GUI_Development_Plan_v1.md`.

---

*ACS37610 Programmer User Guide — release 1.0.0*
