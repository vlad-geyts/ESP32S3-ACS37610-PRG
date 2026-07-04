# ACS37610LLUATR-010B3 Programmer — GUI Development Plan
**Host application:** Python 3.10+ / PySide6 (Qt) HMI on Windows PC
**Controller (target):** ESP32-S3-DEVKITC-1N16R8V running the ACS37610 programmer firmware
**Device (DUT):** ACS37610LLUATR-010B3 on custom eval board
**Document version:** v1.1 — Main-tab updates (ENABLE DEVICE gating, Save to File, Load from File with write+verify)
**Status:** Draft for review

> **v1.1 changes (user requirements, 2026-07-04):**
> 1. New **ENABLE DEVICE** button — sends the Access Code (`AUTH`). Until it succeeds after
>    programmer power-up, all other control buttons are disabled (grey).
> 2. New **Save to File** button — runs the full read sequence (`0x09, 0x19, 0x0A, 0x1A, 0x0B,
>    0x20`) and saves the contents to a file.
> 3. New **Load from File** button + status indicator — writes saved values to EEPROM `0x09` and
>    `0x0A`, then triggers a read-back sequence to verify there are no EEPROM write errors.
>    (Supersedes v1.0's "Load never auto-writes" rule in §8.7.)
> No specific layout requirement — readable and intuitive.

> **Companion document.** This plan is the detailed GUI counterpart to
> `ACS37610_Programmer_Development_Plan_v4.md` (header v4.1, "Approved"), and expands its
> Phase 5 ("Host GUI, Validation & Production Hardening"). It also expands
> `GUI Implementation Ideas.md` into an executable specification. Protocol, hardware, and the
> low-level Manchester/CRC firmware are owned by v4.1 and are **not** redefined here — only
> referenced.

---

## Table of Contents

1. [Overview & Scope](#1-overview--scope)
2. [System Architecture](#2-system-architecture)
3. [Host ↔ Firmware Serial Command Protocol](#3-host--firmware-serial-command-protocol)
4. [Firmware Command Parser (firmware-side scope)](#4-firmware-command-parser-firmware-side-scope)
5. [Host GUI Architecture (PySide6)](#5-host-gui-architecture-pyside6)
6. [Register / Data Model](#6-register--data-model)
7. [GUI Screen Specifications](#7-gui-screen-specifications)
8. [Feature Workflows](#8-feature-workflows)
9. [File Format (Save / Load)](#9-file-format-save--load)
10. [Development Phases & Schedule](#10-development-phases--schedule)
11. [Testing & Verification](#11-testing--verification)
12. [Risks & Open Items](#12-risks--open-items)
13. [Requirements Traceability Matrix](#13-requirements-traceability-matrix)

---

## 1. Overview & Scope

### 1.1 Purpose

Provide an operator-facing HMI on the host PC to read, edit, write, verify, save, and restore the
ACS37610 register set through the ESP32-S3 programmer. The GUI replaces the raw serial terminal
used during firmware bring-up (Phases 3–4) with a guided, safe, multi-tab interface suitable for
engineering calibration and production use.

### 1.2 Scope

| In scope | Out of scope (owned elsewhere) |
|----------|-------------------------------|
| Host GUI application (PySide6/Qt) | Manchester encoding/decoding (`lib/manchester`, v4.1 §4.2) |
| **Firmware command parser** that exposes an ASCII command protocol over serial, replacing the hardcoded read loop in `src/main.cpp` | CRC-3 engine (`lib/crc3`, v4.1 §2.3a) |
| `acs37610_cmd` firmware module: command frame builders, sequencing, verify-after-write, ECC check | Hardware design, PROG line circuit, power architecture (v4.1 §3) |
| Host↔firmware serial protocol contract (§3) | Protocol/register-map definition (v4.1 §2 — referenced, not redefined) |
| Save/load file format, parameter editing, verification UX | |

> **Why the firmware parser is in scope.** The GUI cannot function against the current firmware:
> `src/main.cpp` today sends the Access Code once at boot and then loops a FAULT_STATUS (0x20)
> read every 2 s, printing human-readable lines to UART. There is no command interface. Delivering
> a working GUI therefore requires defining and implementing the host↔firmware command protocol
> end-to-end. The host and firmware halves are developed against the single contract in §3.

### 1.3 Transport note (UART now, USB-CDC later)

The protocol is **transport-agnostic** — a line-based ASCII protocol over a serial COM port. It
runs unchanged over:

- **Today:** the CH343 USB-UART bridge at 115200 baud (`ARDUINO_USB_MODE=0`,
  `ARDUINO_USB_CDC_ON_BOOT=0` in `platformio.ini`).
- **Later (v4.1 Phase 5):** native USB-CDC, enabled by the firmware build-flag switch
  (`ARDUINO_USB_CDC_ON_BOOT=1`). This is a firmware change only; the GUI just selects a different
  COM port.

The GUI must not assume a fixed COM port or baud — it offers a port selector (§7.1).

---

## 2. System Architecture

```
┌──────────────────────────────────────────────────┐
│                  Host PC (Windows)                 │
│                                                    │
│   ┌────────────────────────────────────────────┐  │
│   │           PySide6 GUI (Qt UI thread)         │  │
│   │  MainWindow · 5 tabs · status indicators     │  │
│   └───────────────▲────────────────────┬─────────┘  │
│        Qt signals │                     │ method calls │
│   ┌───────────────┴─────────┐  ┌────────▼──────────┐  │
│   │  ProtocolClient (typed) │  │  Register model   │  │
│   │  read/write/auth/power  │  │  + bit-field codec│  │
│   └───────────────▲─────────┘  └───────────────────┘  │
│                   │ enqueue command / emit response     │
│   ┌───────────────┴───────────────────────────────┐    │
│   │     SerialTransport (QThread + pyserial)        │    │
│   │     single in-flight command, line framing      │    │
│   └───────────────▲────────────────────────────────┘    │
└───────────────────┼─────────────────────────────────────┘
                    │  ASCII line protocol (§3) over COM port
                    │  (CH343 UART @115200, or USB-CDC)
┌───────────────────┴─────────────────────────────────────┐
│                ESP32-S3 Programmer (firmware)             │
│   cmd_parser  →  acs37610_cmd  →  manchester / crc3       │
│   (ASCII I/O)    (frame build,     (bit-bang TX,          │
│                   verify, ECC)      RMT RX, CRC-3)         │
└───────────────────────────────┬──────────────────────────┘
                                │  PROG line (Manchester, open-drain)
                       ┌────────▼────────┐
                       │  ACS37610 DUT   │
                       └─────────────────┘
```

### 2.1 Host threading model

- **Qt UI thread** owns all widgets. It never performs blocking serial I/O.
- **Serial worker thread** (`QThread`) owns the `pyserial` port. It serializes commands
  (one in-flight at a time), reads response lines, and emits Qt signals
  (`response_received`, `error`, `connection_changed`) back to the UI thread.
- Communication is request/response and **strictly serialized**: the GUI disables the relevant
  controls while a command is outstanding to prevent overlapping frames on the PROG line.

---

## 3. Host ↔ Firmware Serial Command Protocol

The contract both halves are built against. Keep it simple, line-based, and human-readable so it
also works from a plain terminal during debugging.

### 3.1 Framing & conventions

- **Encoding:** ASCII. Commands and responses are single lines terminated by `\n` (`\r` tolerated).
- **Numbers:** hex, with or without `0x` prefix. Addresses are 6-bit (`0x00`–`0x3F`); data is the
  26-bit payload (`0x000000`–`0x3FFFFFF`).
- **One response per command.** The firmware always answers exactly one line (an `OK…`, `DATA…`,
  `ID…`, `STATUS…`, or `ERR …`). The host waits for that line or times out.
- **Echo of address:** read/write responses echo the address for host-side correlation.
- **Case-insensitive** command keywords; the firmware uppercases on parse.
- **Single in-flight:** the host issues the next command only after the previous response (or
  timeout). The firmware need not queue.

### 3.2 Command set

| Command | Args | Success response | Purpose |
|---------|------|------------------|---------|
| `*IDN?` (alias `PING`) | — | `ID ACS37610-PRG <fw_ver>` | Identify / connection probe (Comm indicator) |
| `STATUS` | — | `STATUS PWR=<0\|1> PORT=<0\|1> ERR=<code>` | Power state, port-open state, last error |
| `PWRON` | — | `OK` | Drive PWR_EN **low** → enable DUT 3.3 V rail |
| `PWROFF` | — | `OK` | Drive PWR_EN **high** → disable DUT rail |
| `AUTH` | — | `OK` | Send Access Code `0x2C413736`→`ADDR 0x31`; wait 120 µs settle; opens device serial port |
| `READ <addr>` | hex addr | `DATA <addr> <hex8> ECC=<OK\|FAIL\|NA>` | Read register; `hex8` = 32-bit response data; ECC from DATA[27:26] on EEPROM reads, `NA` otherwise |
| `WRAM <addr> <data>` | hex addr, hex data | `OK` | Write volatile shadow/RAM register |
| `WEEP <addr> <data>` | hex addr, hex data | `OK VERIFY=OK` | Write EEPROM; enforce t_w (35 ms); read-back verify; ECC check |

### 3.3 Error responses

Uniform `ERR <code>` replies (firmware also reflects last code in `STATUS … ERR=<code>`):

| Code | Meaning |
|------|---------|
| `ERR ARG` | Malformed command / bad address or data |
| `ERR PORT` | READ/WRITE attempted before `AUTH` (device port not open) |
| `ERR TIMEOUT` | No device response within RX window (~100 ms) |
| `ERR CRC` | CRC-3 mismatch on device response |
| `ERR ECC` | EEPROM read/verify reported ECC fault (DATA[27:26]) |
| `ERR VERIFY` | EEPROM write read-back value ≠ written value |
| `ERR LOCKED` | Device reports WRITE_LOCK set / write refused |
| `ERR PWROFF` | Command requires DUT power but rail is off |

### 3.4 State requirements

1. **Port-open ordering (v4.1 §2.2/§2.4):** the device ignores READ/WRITE until the Access Code
   has opened the serial port. The firmware tracks a `port_open` flag (set by `AUTH`, cleared on
   `PWROFF`/power-cycle). READ/WRITE before AUTH → `ERR PORT`. The **host orchestrates** the
   normal sequence `PWRON → AUTH → (settle) → READ/WRITE` (see §8.2); a firmware option to
   auto-AUTH inside `PWRON` is offered (§4.4) but the primitives remain separately callable.
2. **Timing owned by firmware:** the 120 µs post-AUTH settle, the 74 µs inter-command delay, and
   the 35 ms EEPROM `t_w` are enforced inside the firmware command handlers — the host does not
   time these.

### 3.5 Example session (annotated)

```
> *IDN?
< ID ACS37610-PRG 1.0.0
> PWRON
< OK
> AUTH
< OK
> READ 09
< DATA 09 0x000A1F37 ECC=OK
> WRAM 19 0x000A1F40
< OK
> READ 19
< DATA 19 0x000A1F40 ECC=NA
> WEEP 09 0x000A1F40
< OK VERIFY=OK
> READ 20
< DATA 20 0x0A3C0000 ECC=NA
> PWROFF
< OK
```

---

## 4. Firmware Command Parser (firmware-side scope)

Implements the §3 contract on the ESP32-S3. Replaces the hardcoded boot-AUTH + 2 s read loop in
`src/main.cpp` (current `programmerTask`).

### 4.1 New / changed modules

| Module | Location | Responsibility |
|--------|----------|----------------|
| Command parser | `lib/cmd_parser/cmd_parser.{h,cpp}` (or `src/usb_cdc.cpp` per v4.1 §4.3) | Read `\n`-terminated lines from `Serial`; tokenize; dispatch to handlers; format and emit the single ASCII response line. Runs as the renamed programmer task. |
| ACS command layer | `lib/acs37610_cmd/acs37610_cmd.{h,cpp}` | Frame builders for all four commands (refactored from the inline frame building now in `main.cpp` lines ~83–104); CRC via `crc3`; TX/RX via `manchester`; DATA[31:26]=0 on EEPROM writes; 120 µs / 74 µs / 35 ms timing; verify-after-write; DATA[27:26] ECC check on EEPROM reads. |
| `src/main.cpp` | edit | Keep setup (NeoPixel, GPIO, `manchester_tx_init`/`manchester_rx_init`, power-rail bring-up). Replace `programmerTask` body with a command loop calling `cmd_parser`. Keep `heartbeatTask`. |

> Both new modules go under `lib/<name>/` so the PlatformIO LDF picks them up for firmware **and**
> the native test env — matching the established project pattern used by `lib/crc3` and
> `lib/manchester`.

### 4.2 Command handler responsibilities

- `PWRON`/`PWROFF`: drive `Config::PwrEn` (GPIO5) low/high; track `pwr_on`. `PWROFF` clears
  `port_open`.
- `AUTH`: build the 44-bit access-code frame (`ADDR 0x31`, `DATA 0x2C413736`, CRC-3), TX, wait
  120 µs, set `port_open=true`. Requires `pwr_on` else `ERR PWROFF`.
- `READ <addr>`: require `port_open`; build 12-bit read request; TX; release line; arm RMT RX;
  await 44-bit response (~100 ms); validate CRC; for EEPROM addresses interpret DATA[27:26] as
  ECC; reply `DATA <addr> <hex8> ECC=<…>`.
- `WRAM <addr> <data>`: require `port_open`; build 44-bit write frame (DATA[31:26]=0); TX; `OK`.
- `WEEP <addr> <data>`: require `port_open`; build write frame; TX; wait `t_w` (35 ms); read back;
  compare DATA[25:0]; check ECC; reply `OK VERIFY=OK` or `ERR VERIFY`/`ERR ECC`.
- `STATUS`/`*IDN?`: report flags / identity; available without `port_open`.

### 4.3 Error & safety handling

- Map every failure to the §3.3 codes; store the last code for `STATUS`.
- **WRITE_LOCK guard (defense in depth):** the GUI guards WRITE_LOCK (§7.5), but the firmware
  should additionally reject a `WEEP 09`/`WEEP 0A` whose DATA bit [25]=1 unless an explicit
  override token is present (e.g. `WEEP 09 <data> FORCE`). This prevents an accidental permanent
  lock from a malformed host command. (Open item — see §12.)

### 4.4 Sequencing option

`PWRON` may optionally auto-run `AUTH` + settle (returning `OK PORT=1`) to simplify host logic.
Default keeps them separate so the host owns sequencing per v4.1 §4.5; the convenience behavior is
a compile-time/`STATUS`-discoverable option.

### 4.5 Firmware testability

- Unit-test `acs37610_cmd` frame builders in `[env:native]` (reuse the `lib/`-based native test
  pattern from `lib/crc3`): assert built frames/CRС for known register values, and assert the
  parser maps lines→handler calls and handler results→response strings (handlers mocked).
- Hardware loopback (TX GPIO → RX GPIO jumper) for the read path, per v4.1 Phase 3 deliverables.

---

## 5. Host GUI Architecture (PySide6)

### 5.1 Package layout

```
host/
  acs_gui/
    __init__.py
    app.py                 # entry point; QApplication, MainWindow
    mainwindow.py          # QMainWindow, QTabWidget, wiring
    transport.py           # SerialTransport: QThread + pyserial, framing, signals
    protocol.py            # ProtocolClient: typed command methods over transport
    registers.py           # Register/Field definitions + 26/32-bit codec
    storage.py             # JSON save/load (§9)
    widgets/
      status_indicator.py  # colored status-bar widget (Idle/Active/OK/Fail)
      field_table.py       # editable bit-field table (decode/encode)
    views/
      main_tab.py
      reg_tab.py           # parameterized base for EE_CUST0/1/2
      fault_tab.py
  tests/                   # pytest: codec round-trip, protocol parsing, mock-serial
  requirements.txt         # PySide6, pyserial, pytest
  README.md                # run instructions
```

### 5.2 Layer responsibilities

- **`transport.py` — SerialTransport.** Owns the `pyserial.Serial` in a `QThread`. API:
  `list_ports()`, `open(port, baud)`, `close()`, `send(line, timeout) -> reply`. Maintains a
  command queue (single in-flight), reads lines, and emits signals: `connection_changed(bool)`,
  `line_received(str)`, `error(str)`. A watchdog marks the link down on repeated timeouts.
- **`protocol.py` — ProtocolClient.** Typed wrapper turning §3 into Python:
  `idn()`, `status() -> Status`, `power_on()`, `power_off()`, `auth()`,
  `read_register(addr) -> ReadResult(data, ecc)`, `write_ram(addr, data)`,
  `write_eeprom(addr, data) -> WriteResult(verify)`. Parses responses, raises typed exceptions on
  `ERR …`. Knows nothing about widgets.
- **`registers.py` — model + codec.** Declarative register/field tables (§6) and pure functions
  `decode(addr, raw) -> dict[field]=value` and `encode(addr, fields) -> raw`. No Qt dependency →
  unit-testable.
- **`widgets/status_indicator.py` — StatusIndicator.** Reusable colored bar with states:
  `IDLE` (grey), `ACTIVE` (blue, operation in progress), `OK`/`COMPLETED` (green),
  `FAIL` (red). Used for Comm, Power, Read All, and each tab's Read/Write status.
- **`views/` — tabs.** One module per tab; `reg_tab.py` is parameterized by a register definition
  so EE_CUST0/1/2 share one implementation.

### 5.3 Connection / comm-status logic

On Connect: open port → send `*IDN?`. Valid `ID …` reply → Comm indicator **green**; timeout/bad
reply → **red** and port closed. Any subsequent transport `error`/repeated timeout flips Comm to
red. `STATUS` polling (or piggy-backing on each command's success) keeps it live.

---

## 6. Register / Data Model

Source of truth: dev plan v4.1 §2.7. All payloads are 26-bit `DATA[25:0]` except FAULT_STATUS
decode (see note). Shadow registers share the bit map of their EEPROM twin.

| Tab | EEPROM addr | Shadow addr | Access | Field map (bit range) |
|-----|------------|-------------|--------|-----------------------|
| **EE_CUST0** | `0x09` | `0x19` (SH_CUST0) | R/W | WRITE_LOCK[25] · COM_LOCK[24] · SPARE[23] · OTF_DIS[22] · POL[21] · CLAMP_EN[20] · FAULT_DIS[19] · FAULTR_DIS[18] · QVO[17:9] · SENS_FINE[8:0] |
| **EE_CUST1** | `0x0A` | `0x1A` (SH_CUST1) | R/W | OCF_HYST[25:24] · FAULT_LATCH[23] · OCF_P_DIS[22] · OCF_N_DIS[21] · OCF_QUAL[20:18] · OTF_THRESH[17:14] · OCF_N_THRES[13:7] · OCF_P_THRES[6:0] |
| **EE_CUST2** | **`0x0B`** | — | R/W | C_SPARE[25:0] (customer scratchpad) |
| **FAULT_STATUS** | `0x20` | — | **Read-only** | TEMP_OUT[27:16] · SPARE[15:13] · UV_STAT[12] · OV_STAT[11] · OC_STAT[10] · OT_STAT[9] · FP_STAT[8] · SPARE[7:5] · UV_EV[4] · OV_EV[3] · OC_EV[2] · OT_EV[1] · FP_EV[0] |

### 6.1 Decode/encode notes

- **EE_CUST2 address discrepancy (open item).** `GUI Implementation Ideas.md` states EE_CUST2 =
  EEPROM `0x08`. The Approved dev plan v4.1 §2.7 states `0x0B`. **This plan uses `0x0B`** and
  centralizes the address in `registers.py` so it can be changed in one place once confirmed
  against the Allegro datasheet. See §12.
- **FAULT_STATUS is decoded from the full 32-bit response,** not the 26-bit `DATA[25:0]` payload:
  `TEMP_OUT` occupies bits [27:16]. Because FAULT_STATUS is volatile (not EEPROM-backed),
  `DATA[27:26]` are **part of TEMP_OUT — not ECC**; the protocol client reports `ECC=NA` and the
  codec uses the documented bit map directly.
- **Field rendering:** single-bit fields → checkbox/toggle; multi-bit fields → numeric (hex/dec)
  with min/max from bit width; status/event bits on FAULT_STATUS → read-only colored flags.

---

## 7. GUI Screen Specifications

Five tabs in a `QTabWidget`: **Main**, **EE_CUST0**, **EE_CUST1**, **EE_CUST2**, **FAULT_STATUS**.

### 7.1 Main tab (programmer dashboard)

| Element | Behavior |
|---------|----------|
| **COM port** selector + **Connect/Disconnect** | Lists serial ports; Connect opens port and probes `*IDN?` (§5.3) |
| **Communication** status bar | Green = app is communicating with the programmer; Red = not connected / no response (Ideas req #2) |
| **Power** status bar | Green = DUT powered (PWR=1); Red = DUT unpowered (Ideas req #2) |
| **Power On** button | Sends `PWRON` (PWR_EN low → 3.3 V rail on). Does **not** auto-AUTH — port opening is the explicit ENABLE DEVICE action (v1.1) |
| **Power Off** button | Sends `PWROFF` (PWR_EN high); clears port-open state and re-disables the gated controls |
| **ENABLE DEVICE** button (v1.1) | Sends `AUTH` (Access Code → `0x31`). **Gating rule:** after programmer power-up (and after any DUT power cycle / Power Off), all control buttons below this row are disabled (grey) until ENABLE DEVICE succeeds. Only COM/Connect, Power On/Off and ENABLE DEVICE itself remain active |
| **Read All** button | Reads every register backing the tabs: `0x09, 0x19, 0x0A, 0x1A, 0x0B, 0x20` (§8.3) |
| **Read All** status bar | Green = all reads completed; Red = any read failed (no response / CRC / ECC) (Ideas req #2) |
| **Save to File** button (v1.1) | Runs the full read sequence over `0x09, 0x19, 0x0A, 0x1A, 0x0B, 0x20`, then writes the snapshot to JSON (§9). Any read failure aborts the save |
| **Load from File** button + status indicator (v1.1) | Loads a JSON snapshot, writes the saved values to EEPROM `0x09` and `0x0A` (`WEEP`), then triggers a read-back command sequence to verify there are no EEPROM write errors. Indicator: green = all writes verified; red = any write/verify error. WRITE_LOCK guard (§7.6) still applies |
| **Activity log** panel | Scrolling view of sent commands / responses / errors (engineering aid) |

> "Read All" reads the six registers shown across the four data tabs. The Ideas doc phrase
> "all 4 EEPROM locations" is interpreted as the four data tabs; shadow registers are included so
> EE_CUST0/1 show both EEPROM and shadow values.

### 7.2 / 7.3 EE_CUST0 & EE_CUST1 tabs

- **Two value columns:** **EEPROM** (0x09 / 0x0A) and **Shadow** (0x19 / 0x1A), with a third
  **Edit** column of per-field editors (from §6 field map).
- **Buttons + status bars (Ideas req #3):**
  - **Read** — reads both the EEPROM and shadow registers; populates the EEPROM/Shadow columns;
    status bar green on success, red on fail.
  - **Write** — writes the edited values to the **EEPROM** register (`WEEP`), then read-back-
    compares (§8.5); status bar green = Completed, red = Fail (Ideas req #8).
  - **Recommended extra — Write Shadow (RAM):** writes edits to the shadow register (`WRAM`,
    volatile) for safe iterative tuning before committing to EEPROM, matching the dev-plan
    RAM-first workflow (v4.1 §2.5.1 / Phase 4). Also read-back-compared.
- **Raw hex field** shows/accepts the full 26-bit word alongside decoded fields (kept in sync).

### 7.4 EE_CUST2 tab

- Single **C_SPARE[25:0]** field (hex), EEPROM `0x0B`. **Read** / **Write** buttons + status bars,
  same Write→verify behavior as §7.2.

### 7.5 FAULT_STATUS tab

- **Read-only** display of all decoded status/event flags (colored) and `TEMP_OUT`.
- **Read** button only + status bar (Ideas req #3 — volatile/read-only register, no Write).

### 7.6 WRITE_LOCK safety guard

`WRITE_LOCK[25]` on EE_CUST0 permanently locks the device once committed to EEPROM (v4.1 §2.7 /
risk R2). The GUI:
- Renders the WRITE_LOCK editor **disabled by default**, behind an explicit "Enable WRITE_LOCK
  editing" checkbox.
- Requires a typed confirmation dialog before any `WEEP 0x09` whose data sets bit [25]=1.
- The firmware enforces the same guard independently (§4.3).

---

## 8. Feature Workflows

Each workflow drives the §3 commands and sets the relevant StatusIndicator
(`ACTIVE` while running → `OK`/`FAIL`).

### 8.1 Connect & communication status
`open(port)` → `*IDN?`. Valid `ID …` → Comm green; else Comm red + close. (§5.3)

### 8.2 Power On / Off
- **Power On:** `PWRON` → on `OK`, Power indicator green. No auto-AUTH (v1.1) — the port stays
  closed and gated controls stay grey until ENABLE DEVICE (§8.2a).
- **Power Off:** `PWROFF` → Power indicator red; port-open state cleared; gated controls re-grey.

### 8.2a Enable Device (v1.1)
`AUTH` → on `OK`: port marked open, all gated control buttons enabled. On error: controls stay
grey, log the error. Required once after every DUT power cycle (the device closes its serial port
on power loss).

### 8.3 Read All
Iterate `READ` over `[0x09, 0x19, 0x0A, 0x1A, 0x0B, 0x20]`; decode each into its tab. All succeed
→ Read All green; any failure (TIMEOUT/CRC/ECC) → Read All red and the offending tab's Read status
red. Requires power on + port open (auto-AUTH if needed).

### 8.4 Per-register Read
Tab **Read** → `READ <addr>` (both EEPROM+shadow for EE_CUST0/1) → decode → populate columns →
status bar green/red.

### 8.5 Per-register Write **with read-back compare** (Ideas req #8)
1. Encode edited fields → 26-bit word (`registers.encode`).
2. `WEEP <addr> <data>` (EEPROM) or `WRAM <addr> <data>` (shadow).
3. **Read back** the same address (`READ <addr>`) and compare returned `DATA[25:0]` to the written
   value.
4. Match (and ECC OK for EEPROM) → status bar **green/Completed**; mismatch → **red/Fail**.

> Read-back compare is performed by the GUI for **both** RAM and EEPROM writes. The firmware
> already verifies `WEEP` internally; the GUI repeats the read-back so the requirement holds
> uniformly (RAM writes are not firmware-verified) and so the user sees the confirmed value.

### 8.6 Edit fields
Editors update the raw-hex field live and vice-versa (two-way sync via the codec). Out-of-range
entries are clamped/rejected per field bit width. Edits are local until a Write.

### 8.7 Save / Load file (revised v1.1)
- **Save to File:** run the Read All sequence (§8.3) over `0x09, 0x19, 0x0A, 0x1A, 0x0B, 0x20`;
  on success write the snapshot to JSON (§9). Any read failure aborts the save (partial snapshots
  are not written) and flags the status red.
- **Load from File:** parse and validate the JSON → write the saved values to EEPROM `0x09`
  (`WEEP 09`) and `0x0A` (`WEEP 0A`) → run a read-back command sequence and compare against the
  written values to verify there are no EEPROM write errors → Load status indicator green
  (all verified) or red (any failure). The WRITE_LOCK guard (§7.6) applies to the loaded `0x09`
  value; a snapshot with bit[25]=1 requires the same explicit confirmation. Loaded values are also
  populated into the tab editors so the user sees what was written.

---

## 9. File Format (Save / Load)

JSON, human-readable, versioned. Stores raw words and decoded fields plus metadata.

```json
{
  "format": "acs37610-registers",
  "version": 1,
  "device_id": "optional user/serial label",
  "timestamp": "2026-06-23T14:05:00Z",
  "fw_version": "1.0.0",
  "registers": {
    "EE_CUST0": { "addr": "0x09", "raw": "0x000A1F37",
                  "shadow_addr": "0x19", "shadow_raw": "0x000A1F40",
                  "fields": { "QVO": 271, "SENS_FINE": 311, "POL": 0, "WRITE_LOCK": 0 } },
    "EE_CUST1": { "addr": "0x0A", "raw": "0x00000000", "fields": { } },
    "EE_CUST2": { "addr": "0x0B", "raw": "0x00000000", "fields": { "C_SPARE": 0 } },
    "FAULT_STATUS": { "addr": "0x20", "raw": "0x0A3C0000", "fields": { } }
  }
}
```

- `fields` is informational/diff-friendly; `raw` is authoritative on Load.
- Load validates `format`/`version`, addresses, and word width; mismatches are reported, not
  silently ignored.

---

## 10. Development Phases & Schedule

| Phase | Deliverable | Notes |
|-------|------------|-------|
| **G1 — Protocol + firmware parser** | `cmd_parser` + `acs37610_cmd` modules; `main.cpp` loop replaced; §3 commands answer over UART | Foundation; validated from a plain serial terminal first |
| **G2 — Host transport + protocol client** | `transport.py`, `protocol.py`; mock-serial unit tests | No GUI yet; scriptable |
| **G3 — Register model + codec** | `registers.py`; pytest encode/decode round-trips for all registers | Pure Python, no hardware |
| **G4 — Main tab** | Connect, Comm/Power indicators, Power On/Off, Read All, activity log | First end-to-end GUI path |
| **G5 — Register tabs** | EE_CUST0/1/2 + FAULT_STATUS; Read/Write/verify; field editors; WRITE_LOCK guard | Core operator features |
| **G6 — Save / Load** | `storage.py`; Save All / Load All wired to Main tab | File round-trip |
| **G7 — Validation & polish** | End-to-end on hardware; operator guide; styling; packaging | Aligns with v4.1 Phase 5 deliverables |

> Sequencing matches v4.1's "GUI & validation" (Phase 5, weeks 9–12). G1 is the critical
> dependency — the GUI (G4+) cannot be exercised until the firmware speaks §3.

---

## 11. Testing & Verification

### 11.1 Firmware
- Native (`pio test -e native`): `acs37610_cmd` frame/CRC builders against known vectors; parser
  line→handler dispatch and response formatting (handlers mocked).
- Hardware loopback (TX→RX jumper) for the read path before DUT connection (v4.1 Phase 3).

### 11.2 Host (no hardware)
- `pytest`: `registers.py` encode/decode round-trips for every register and field; boundary/clamp
  cases.
- `protocol.py` against a **mock serial** that emulates §3 replies (including each `ERR` code) →
  asserts parsing, typed exceptions, and verify logic.
- GUI logic tests with the mock serial: Read All success/partial-fail flips indicators correctly;
  Write→verify sets green/red; WRITE_LOCK guard blocks without confirmation.

### 11.3 End-to-end (GUI ↔ ESP32 ↔ DUT)
1. Connect → Comm green; Power On → Power green; `AUTH` ok.
2. Read All → all tabs populate; Read All green.
3. RAM write to SH_CUST0 (0x19) → read-back compare green.
4. EEPROM write to a safe register (EE_CUST2 0x0B C_SPARE) → `OK VERIFY=OK` → green.
5. Save All → file; power-cycle; Load All → values match snapshot.
6. FAULT_STATUS read → flags/TEMP_OUT decode sensibly.
7. Negative paths: disconnect cable (Comm red); read before AUTH (`ERR PORT`); induce CRC/timeout.

---

## 12. Risks & Open Items

| # | Item | Disposition |
|---|------|-------------|
| O1 | **EE_CUST2 address 0x08 vs 0x0B** | Plan uses **0x0B** (v4.1 §2.7, Approved). Address centralized in `registers.py`. **Confirm against Allegro datasheet** before EEPROM writes to EE_CUST2. |
| O2 | AUTH / port-open ownership | **Resolved v1.1:** AUTH is an explicit user action (ENABLE DEVICE button, §7.1). `PWRON` never auto-AUTHs; firmware keeps the primitives separate. |
| O3 | USB-CDC vs UART COM enumeration on Windows | GUI is port-agnostic (selector). When firmware switches to `ARDUINO_USB_CDC_ON_BOOT=1`, re-verify enumeration / driver (v4.1 R6). |
| O4 | **WRITE_LOCK OTP** | Permanent lock if bit[25] committed. Guarded in **both** GUI (§7.6) and firmware (§4.3). Keep disabled by default. |
| O5 | Serial concurrency | Strictly single in-flight command; UI disables controls during a command. No overlapping PROG frames. |
| O6 | FAULT_STATUS 32-bit decode | TEMP_OUT extends to bit 27; DATA[27:26] are **not** ECC for this volatile register — codec/`ECC=NA` handle this (§6.1). |
| O7 | Long EEPROM operations block UI thread? | No — all serial I/O on worker thread; `t_w` waited firmware-side; UI shows ACTIVE state. |

---

## 13. Requirements Traceability Matrix

Maps every requirement in `GUI Implementation Ideas.md` to where this plan satisfies it.

| Ideas req | Summary | Satisfied by |
|-----------|---------|-------------|
| 1 | Multi-tab HMI: Main, EE_CUST0, EE_CUST1, EE_CUST2, FAULT_STATUS | §7 (five tabs); §6 (EE_CUST2 mapped to 0x0B, FAULT_STATUS to 0x20) |
| 2 | Main-tab controls: Comm status, Power status, Power On/Off, Read All + status | §7.1; workflows §8.1–§8.3 |
| 2 (Power On→PWR_EN low) | Power On drives PWR_EN low to enable 3.3 V | §3 `PWRON`; §4.2; §8.2 |
| 3 | Other tabs have ≥ Read/Write + status bars; FAULT_STATUS Read-only | §7.2–§7.5 |
| 4 | Read all registers and save to a host file | §7.1 Save All; §8.7; §9 |
| 5 | Load all register values from a host file for review | §7.1 Load All; §8.7 (no auto-write) |
| 6 | Edit current parameter values per register | §6 codec; §7 field editors; §8.6 |
| 7 | Write updated values via per-tab Write button | §7.2–§7.4; §3 `WRAM`/`WEEP`; §8.5 |
| 8 | Read back after each write, compare, set status green/red | §8.5 (RAM **and** EEPROM); §4.2 (`WEEP` verify); §7 status bars |

---

*End of document — ACS37610 Programmer GUI Development Plan v1.0*
