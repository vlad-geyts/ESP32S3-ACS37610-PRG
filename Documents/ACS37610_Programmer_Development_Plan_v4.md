# ACS37610LLUATR-010B3 Programmer Development Plan
**Controller:** ESP32-S3-DEVKITC-1N16R8V (development board)  
**Device (DUT):** ACS37610LLUATR-010B3 on custom eval board  
**Document version:** v4.0 — TX architecture change: Manchester TX implemented as bit-bang (`gpio_set_level` + `esp_rom_delay_us`), replacing the originally planned RMT approach. RMT TX abandoned due to legacy API semaphore bug on ESP32-S3. PROG idle voltage root cause documented (Zener reverse current — hardware issue, not firmware). Phase 3 TX validation complete; Phase 3 RX pending.  
**Status:** Approved

---

## Table of Contents

1. [Overview](#1-overview)
2. [Protocol Specification](#2-protocol-specification)
3. [Hardware Design](#3-hardware-design)
4. [Firmware Architecture](#4-firmware-architecture)
5. [Development Phases & Schedule](#5-development-phases--schedule)
6. [Bill of Materials](#6-bill-of-materials)
7. [Risk Register](#7-risk-register)

---

## 1. Overview

### 1.1 System Architecture

The programmer consists of two main components connected via a single bidirectional PROG line:

| Role | Component | Notes |
|------|-----------|-------|
| Controller | ESP32-S3-DEVKITC-1N16R8V | Development board; drives Manchester-encoded serial traffic; USB-CDC for PC interface |
| Device (DUT) | ACS37610LLUATR-010B3 | Allegro current sensor on custom eval board; programmed via PROG pin |

The ESP32-S3-DEVKITC-1N16R8V development board is used for this project. It provides native USB-CDC (no external USB-UART bridge needed), and sufficient GPIO for power control and status indication. Manchester TX is implemented via bit-bang; the RMT peripheral is reserved for RX decoding (Phase 3 RX). The Controller and Device are connected with a short 50 mm 3-wire cable (VCC, GND, PROG).

### 1.2 DUT Electrical Characteristics

| Parameter | Value | Notes |
|-----------|-------|-------|
| Supply voltage (VCC) | 3.3 V | Device operates at 3.3 V |
| Max supply current | 50 mA | Sufficient for normal operation and programming |
| Typical operating current | 19 mA | During normal operational mode and programming |
| PROG pin logic level | 3.3 V | Open-drain, bidirectional |
| EEPROM programming VCC | ≤ 5 V | VCC must not exceed 5 V during EEPROM writes |

> **Correction applied (v0 review):** Device supply is 3.3 V at up to 50 mA — not 5 V at 150 mA as previously stated.

### 1.3 Key Design Goals

- Implement all four PROG commands: Write Access Code, Write RAM, Write EEPROM, Read
- Manchester-encoded serial protocol (G. E. Thomas convention), MSB first
- USB-CDC host interface (serial terminal or Python GUI)
- Verify-after-write on every EEPROM write operation
- Controlled DUT power sequencing with settle delays
- Optional stretch goal: Wi-Fi web UI for production floor use

---

## 2. Protocol Specification

### 2.1 Manchester Encoding — G. E. Thomas Convention

Each bit occupies one full bit period **T**. The mid-point transition encodes the bit value:

| Mid-point transition | Bit value |
|---------------------|-----------|
| Rising edge (LOW → HIGH) | **0** |
| Falling edge (HIGH → LOW) | **1** |

Address and data are transmitted **MSB first**. The PROG line is open-drain with a pull-up resistor to 3.3 V.

```
Bit = 0 (rising at mid-point):   ___/‾‾‾
Bit = 1 (falling at mid-point):  ‾‾‾\___
                                  |←T→|
```

### 2.2 Timing Parameters

| Parameter | Min | Typical | Max | Unit |
|-----------|-----|---------|-----|------|
| Bit rate | 1 | 30 | 333 | kbps |
| Bit period T | 3.0 | 33 | 1000 | µs |
| Program time delay (between consecutive R/W during same Manchester event) | — | 74 | — | µs |
| EEPROM write time t_w | — | 25 | 35 | ms |

**Notes:**
- No special timing or voltage requirements for read operations. Reads can be performed at any time, provided Manchester protocol timing is respected.
- No additional wait time is required after issuing a read command; the device responds immediately per protocol.
- EEPROM writes require a mandatory delay of t_w (typ. 25 ms, max 35 ms) between consecutive writes.
- The device uses an internal charge pump for EEPROM programming.
- **RMT RX idle threshold:** Set to ~50 µs (≈ 1.5 × T/2 at 30 kbps) to reliably detect end-of-response frame without conflicting with the 74 µs inter-command delay.

### 2.3 General Command Frame Structure

All commands share the same base frame format:

```
SYNC[2] | R/W[1] | ADDR[6] | DATA[32] | CRC[3]
```

| Field | Width | Description |
|-------|-------|-------------|
| SYNC | 2 bits | Both bits = 0; marks start of frame. **Excluded from CRC calculation.** |
| R/W | 1 bit | 0 = Write, 1 = Read |
| ADDR | 6 bits | Target register address |
| DATA | 32 bits | Payload (see per-command detail below) |
| CRC | 3 bits | CRC-3 over R/W + ADDR + DATA (write commands); over R/W + ADDR (read request) |

> **Correction applied (v0 review):** The previously proposed format `CMD[2] | ADDR[5] | DATA[16]` was incorrect. The correct frame is `SYNC[2] | R/W[1] | ADDR[6] | DATA[32] | CRC[3]` for all write commands, and a modified format for reads (see §2.6).

---

### 2.3a CRC Specification (B1 — Closed)

| Parameter | Value |
|-----------|-------|
| Polynomial | g(x) = x³ + x + 1 (CRC-3, divisor `0b1011`) |
| Initial value | `0b111` (all ones — set at power-up) |
| Fields covered — write command | R/W[1] + ADDR[6] + DATA[32] |
| Fields covered — read request | R/W[1] + ADDR[6] |
| Fields covered — device response | DATA[32] |
| SYNC bits | **Excluded** from CRC calculation |

The CRC is computed MSB first over the fields listed above. The firmware `crc3.cpp` module must be unit-tested against known-good vectors (synthesised from the polynomial above) before any DUT connection.

---

### 2.4 Write Access Code Command (Controller → Device)

Unlocks EEPROM write operations. Must be sent before any non-volatile (EEPROM) write. The access code is fixed for the ACS37610 family.

**Frame:**

```
SYNC[2] | R/W[1] | ADDR[6] | DATA[32] | CRC[3]
```

| Field | Value | Notes |
|-------|-------|-------|
| SYNC | `0b00` | Both bits zero; excluded from CRC |
| R/W | `0` | Write |
| ADDR | `0x31` | Fixed access-code register address |
| DATA | `0x2C413736` | Fixed access code — enables programming while keeping VOUT active |
| CRC | Computed | CRC-3 over R/W + ADDR + DATA |

> **Correction applied (Open Items B4):** Access code value corrected to `0x2C413736` (not `0x2C413737` as previously stated). This code enables programming access while the analog output (VOUT) remains enabled.

**Behaviour:**
- This command must be re-issued after every power cycle before EEPROM writes.
- It does not affect RAM write operations.

---

### 2.5 Write RAM / Write EEPROM Commands (Controller → Device)

Both RAM and EEPROM writes use the same command frame format. The distinction is the target **ADDR** value and the post-write requirements.

**Frame (identical for RAM and EEPROM):**

```
SYNC[2] | R/W[1] | ADDR[6] | DATA[32] | CRC[3]
```

| Field | RAM write | EEPROM write |
|-------|-----------|--------------|
| SYNC | `0b00` | `0b00` |
| R/W | `0` | `0` |
| ADDR | RAM register address | EEPROM register address |
| DATA[31:26] | Not used (set to 0) | Set to 0 — device generates ECC automatically |
| DATA[25:0] | 26-bit data payload | 26-bit data payload |
| CRC | CRC-3 over R/W + ADDR + DATA | CRC-3 over R/W + ADDR + DATA |

#### 2.5.1 RAM Write

- No special voltage or timing requirements beyond Manchester protocol timing.
- Changes take effect immediately.
- Data is lost on power cycle.
- Safe for iterative tuning of calibration values before committing to EEPROM.

#### 2.5.2 EEPROM Write

- Requires a prior **Write Access Code** command (§2.4).
- **VCC must not exceed 5 V** during the EEPROM write operation (relevant if the supply rail is adjustable).
- A mandatory delay of **t_w (typ. 25 ms, max 35 ms)** must be observed after each EEPROM write before the next operation.
- The device uses an **internal charge pump** for EEPROM programming; no external high-voltage supply is needed.
- After writing to EEPROM, a power cycle or wait time may be needed for some changes to take effect.
- **DATA[31:26] shall be set to zero.** The device automatically generates and stores its own 6-bit ECC based on DATA[25:0]; the controller-supplied value in DATA[31:26] is ignored. No ECC computation is required in firmware.
- Firmware must **verify after every EEPROM write** by issuing a Read command and comparing the returned DATA[25:0] to the written value.

---

### 2.6 Read Command (Controller → Device, then Device → Controller)

Reading requires two phases: the Controller sends a Read request frame, then releases the PROG line; the Device responds with a data frame. The GPIO must switch direction between phases.

#### Phase 1 — Controller sends Read request

**Frame:**

```
SYNC[2] | R/W[1] | ADDR[6] | CRC[3]
```

| Field | Value | Notes |
|-------|-------|-------|
| SYNC | `0b00` | Both bits zero; excluded from CRC |
| R/W | `1` | Read |
| ADDR | Target address | RAM or EEPROM register |
| CRC | Computed | CRC-3 over R/W + ADDR |

#### Phase 2 — Device responds with data frame

After the Controller releases the PROG line (GPIO switched to high-impedance input / open-drain released), the Device drives:

**Frame:**

```
SYNC[2] | DATA[32] | CRC[3]
```

| Field | Width | Notes |
|-------|-------|-------|
| SYNC | 2 bits | Both bits zero |
| DATA[31:28] | 4 bits | Not relevant |
| DATA[27:26] | 2 bits | ECC Pass/Fail status (EEPROM reads only; not relevant for RAM) |
| DATA[25:0] | 26 bits | Register data payload |
| CRC | 3 bits | Over SYNC + DATA |

**Read timing notes:**
- No special timing or voltage requirements.
- No additional wait time required; the device responds immediately per protocol.
- The firmware must switch the GPIO from open-drain output to input **before** the device begins its response frame.
- On EEPROM reads, firmware shall check DATA[27:26] and report an ECC fault if the pass/fail status indicates an error. No ECC recomputation is required — the device reports the result directly.

---

### 2.7 Register Map (B3 — Closed)

Six registers are accessible via the PROG interface. All are 26-bit data fields (DATA[25:0]).

#### Register 1 — EE_CUST0 (EEPROM) — Address `0x09` — R/W

| Bits | Field | Description |
|------|-------|-------------|
| [25] | WRITE_LOCK | Lock the device (prevents further writes) |
| [24] | COM_LOCK | Disable communication on VOUT / disable OVD |
| [23] | SPARE | Reserved; no effect |
| [22] | OTF_DIS | Disable overtemperature fault |
| [21] | POL | Change output polarity |
| [20] | CLAMP_EN | Enable output clamps |
| [19] | FAULT_DIS | Disable fault |
| [18] | FAULTR_DIS | Disable fault internal pull-up resistor |
| [17:9] | QVO[9] | Offset adjustment (9 bits) |
| [8:0] | SENS_FINE[9] | Sensitivity fine adjustment (9 bits) |

> ⚠️ **WRITE_LOCK [25]:** Once set to 1 and written to EEPROM, the device is permanently locked. Do not set this bit during development.

---

#### Register 2 — SH_CUST0 (Shadow / RAM) — Address `0x19` — R/W

Bit mapping is identical to EE_CUST0 (`0x09`). Writes to this address affect the volatile shadow register only; changes take effect immediately and are lost on power cycle.

---

#### Register 3 — EE_CUST1 (EEPROM) — Address `0x0A` — R/W

| Bits | Field | Description |
|------|-------|-------------|
| [25:24] | OCF_HYST[2] | Overcurrent fault hysteresis |
| [23] | FAULT_LATCH | Enable fault latch |
| [22] | OCF_P_DIS | Disable positive overcurrent fault |
| [21] | OCF_N_DIS | Disable negative overcurrent fault |
| [20:18] | OCF_QUAL[3] | Overcurrent fault qualifier / short-pulse filter |
| [17:14] | OTF_THRESH[4] | Overtemperature fault threshold |
| [13:7] | OCF_N_THRES[7] | Negative overcurrent fault threshold |
| [6:0] | OCF_P_THRES[7] | Positive overcurrent fault threshold |

---

#### Register 4 — SH_CUST1 (Shadow / RAM) — Address `0x1A` — R/W

Bit mapping is identical to EE_CUST1 (`0x0A`). Volatile shadow register.

---

#### Register 5 — EE_CUST2 (EEPROM) — Address `0x0B` — R/W

| Bits | Field | Description |
|------|-------|-------------|
| [25:0] | C_SPARE[26] | Customer scratchpad — no effect on device functionality |

---

#### Register 6 — FAULT_STATUS (Volatile) — Address `0x20` — Read Only

| Bits | Field | Description |
|------|-------|-------------|
| [27:16] | TEMP_OUT[12] | Temperature output reading |
| [15:13] | SPARE[3] | Reserved |
| [12] | UV_STAT | Undervoltage status |
| [11] | OV_STAT | Overvoltage status |
| [10] | OC_STAT | Overcurrent status |
| [9] | OT_STAT | Overtemperature status |
| [8] | FP_STAT | FAULT pin status |
| [7:5] | SPARE[3] | Reserved |
| [4] | UV_EV | Undervoltage event |
| [3] | OV_EV | Overvoltage event |
| [2] | OC_EV | Overcurrent event |
| [1] | OT_EV | Overtemperature event |
| [0] | FP_EV | FAULT pin event |

> **Note:** FAULT_STATUS is read-only and volatile (not backed by EEPROM). It is useful for diagnostic reads during bring-up and production test.

---

### 2.8 CRC Specification (B1 — Closed)

The CRC-3 polynomial, initial value, and field coverage are fully specified in §2.3a above.

---

## 3. Hardware Design

### 3.1 ESP32-S3 Pin Assignment

| GPIO | Signal | Configuration | Purpose |
|------|--------|---------------|---------|
| GPIO4 | PROG | Open-drain output / input | Bidirectional Manchester data line |
| GPIO5 | PWR_EN | Push-pull output | Enable DUT 3.3 V supply |
| GPIO48 | STATUS_LED | PWM-controlled RGB LED | Slow blink (2 Hz) GREEN = idle; slow blink (2 Hz) RED = error; fast blink BLUE = active |
| GPIO7 | TRIG_IN | Input with pull-up | Optional fixture trigger (active-low) |
| GPIO21 | STROB_OUT | Push-pull output | Debug oscilloscope trigger (frame start/end) |
| GPIO43/44 | UART TX/RX | UART–UART | Low-level debug terminal (CH343 bridge; used during Phases 3–4) |
| GPIO19/20 | USB D+/D− | Native USB | USB-CDC host interface — enabled in Phase 5 |

### 3.2 Power Architecture

```
USB VBUS (5 V)
    │
    └──► LDO (3.3 V) ──┬──► ESP32-S3 VCC
                       │
                       └──► P-channel MOSFET (PWR_EN) ──► DUT VCC (3.3 V)
```

The DUT is supplied from the **3.3 V LDO output** (not directly from the 5 V USB rail), switched via a P-channel MOSFET controlled by GPIO5. Sourcing the DUT from the regulated 3.3 V rail ensures the device never sees more than its 3.3 V operating voltage and avoids any risk of applying 5 V to the DUT. This allows:
- Controlled power sequencing (power-on, settle delay, power-off)
- Safe reset of the DUT between programming operations
- Current measurement (optional: insert shunt resistor)

### 3.3 PROG Line Circuit

```
ESP32-S3 GPIO4 (open-drain)
        │
       10 kΩ pull-up to 3.3 V
        │
      TVS diode (PRTR5V0U2X) to GND
        │
      DUT PROG pin
```

- GPIO4 must be configured as **open-drain output** for TX; the pull-up resistor provides the high state.
- For RX (read response), the GPIO is switched to **input mode**; the device drives the line.
- The TVS diode provides ESD protection on the PROG line.

> ⚠️ **Do NOT use a Zener diode for PROG ESD protection.** A 3.6 V Zener has significant reverse leakage current at 3.3 V (operating point is close to its breakdown knee). With a 10 kΩ pull-up, this reverse current creates a voltage divider that pulls the idle PROG line down to ~2.2 V instead of 3.3 V, corrupting all Manchester idle levels. Use a proper TVS (e.g. PRTR5V0U2X) which has negligible leakage well below its clamp voltage.

### 3.4 Protection and Signal Integrity

| Item | Implementation |
|------|---------------|
| ESD on PROG | TVS diode (e.g. PRTR5V0U2X) to GND |
| DUT power switch | P-channel MOSFET (e.g. AO3401), gate via 1 kΩ to GPIO5 |
| LDO | AMS1117-3.3 or AP2112K-3.3; input pi-filter |
| Decoupling | 100 nF + 10 µF at each supply rail |
| Boot/Reset | BOOT (GPIO0) pull-down 10 kΩ; 100 nF on ENABLE |

### 3.5 Connector

A 2×4 pin 2.54 mm header exposes: PROG, GND, VCC (3.3 V), and spare GPIO for fixture use.

---

## 4. Firmware Architecture

### 4.1 Software Stack

| Layer | Technology |
|-------|-----------|
| IDE | VS Code + PlatformIO |
| Framework | Arduino for ESP32 (ESP32 Arduino Core) |
| Language standard | C++17 (`-std=gnu++17`) |
| Manchester TX driver | Bit-bang — `gpio_set_level()` + `esp_rom_delay_us()` on Core 1, priority 5 |
| Manchester RX driver | ESP32-S3 RMT peripheral (RX decoder — Phase 3 RX, pending) |
| CRC module | Software — CRC-3, g(x) = x³+x+1, init `0b111` |
| Host interface | UART (CH343 bridge) for Phases 3–4 debug; native USB-CDC enabled in Phase 5 |
| Host GUI (optional) | Python + PySerial + Tkinter or Rich CLI |
| Unit tests — pure software | PlatformIO `[env:native]` — runs on PC, no hardware required (CRC-3) |
| Unit tests — hardware | PlatformIO `[env:esp32-s3-devkitc-1-n16r8v]` — RMT loopback tests |

> **Note (v3):** The ECC software module has been removed. The ACS37610 automatically generates and stores its own 6-bit ECC on every EEPROM write; DATA[31:26] is ignored by the device on write and must be set to zero by the controller. ECC pass/fail status is reported by the device in DATA[27:26] of the read response and checked by firmware without recomputation.

### 4.2 Manchester TX — Bit-Bang Implementation (Phase 3 TX ✅ Complete)

RMT TX was evaluated and abandoned. Two blocking issues were found on ESP32-S3 with Arduino Core 2.x / ESP-IDF 4.x:

1. **Legacy `rmt_write_items()` semaphore bug:** With `wait_tx_done=true`, the function returns prematurely on calls 2+ due to a semaphore race condition in the ESP32-S3 RMT v2 hardware compatibility layer. Only the first TX frame was transmitted; subsequent calls produced wrong strobe durations with no PROG waveform.
2. **GPIO matrix conflict:** `gpio_set_direction()` internally calls `gpio_matrix_out(SIG_GPIO_OUT_IDX)`, which disconnects any RMT TX signal from the GPIO. GPIO4 then outputs the software GPIO register default (0 V).

**Bit-bang TX** eliminates both issues:

```cpp
// G.E. Thomas convention — MSB first
// Bit 0: LOW(T/2) then HIGH(T/2)
// Bit 1: HIGH(T/2) then LOW(T/2)
gpio_set_level(kProgGpio, b ? 1u : 0u);
esp_rom_delay_us(s_half_us);           // T/2 = 16 µs
gpio_set_level(kProgGpio, b ? 0u : 1u);
esp_rom_delay_us(s_half_us);
```

**Key implementation parameters:**
- GPIO4: `GPIO_MODE_INPUT_OUTPUT_OD`, both `GPIO_PULLDOWN_DISABLE` and `GPIO_PULLUP_DISABLE`
- After `gpio_config()`, also call `rtc_gpio_pulldown_dis(GPIO_NUM_4)` — GPIO4 is RTC_GPIO4 and has an independent RTC-domain pulldown (~45 kΩ) that `gpio_config()` does not clear
- T/2 = 16 µs (integer division of 33/2); actual T = 32 µs = 31.25 kbps (within ACS37610 1–333 kbps tolerance)
- Blocks ~384 µs per 12-bit frame; runs on Core 1 priority 5 (`programmerTask`)
- `gpio_set_level(kProgGpio, 1)` at end of each frame releases the open-drain line to idle HIGH via the 10 kΩ pull-up
- GPIO21 (STROB_OUT) is driven HIGH for the frame duration as an oscilloscope trigger

**Scope-verified results (Phase 3 TX validation):**
- PROG idle: 3.3 V ✅
- Frame duration: 395.71 µs ≈ 12 × 32 µs ✅
- Manchester waveform: SYNC=00, R/W=1, ADDR=0x20, CRC=5 verified bit-by-bit ✅
- Subsequent bursts: all frames correct in NORMAL trigger mode ✅
- Pre-TX LOW glitch: eliminated ✅
- Post-TX LOW glitch: eliminated ✅

### 4.2a Manchester RX — RMT Approach (Phase 3 RX — Pending)

RMT RX is still the preferred approach for the receive path (device response after a Read command). Since TX is now bit-banged (no RMT TX channel), there is no GPIO matrix conflict for attaching an RMT RX channel to GPIO4.

```cpp
// RX channel setup (legacy API — same driver version used for TX evaluation)
rmt_config_t rx_cfg = RMT_DEFAULT_CONFIG_RX(GPIO_NUM_4, RMT_CHANNEL_1);
rx_cfg.clk_div = 8;                  // 10 MHz RMT clock
rx_cfg.rx_config.idle_threshold = 500;  // 50 µs — end-of-frame detect
rx_cfg.rx_config.filter_en = true;
rx_cfg.rx_config.filter_ticks_thresh = 40;  // 4 µs glitch filter
rmt_config(&rx_cfg);
rmt_driver_install(RMT_CHANNEL_1, 2048, 0);
```

**Decode algorithm:** Expand each RMT `rmt_item32_t` pulse record into two half-periods (duration ≈ T/2 each). Pair consecutive half-periods: (LOW, HIGH) → bit 0; (HIGH, LOW) → bit 1 (G.E. Thomas convention).

**TX → RX turnaround (Read Phase 2):**
1. Bit-bang TX frame completes; `gpio_set_level(kProgGpio, 1)` releases line
2. Start RMT RX (GPIO4 already `INPUT_OUTPUT_OD` — input path is active)
3. Wait 74 µs inter-command delay
4. Device drives SYNC + DATA + CRC response; RMT RX captures edges
5. Decode response from ring buffer

### 4.3 Firmware Module Breakdown

| Module | Responsibility |
|--------|---------------|
| `manchester.cpp` | Bit-bang TX encoder (`gpio_set_level` + `esp_rom_delay_us`); RMT RX decoder (Phase 3 RX — pending); open-drain GPIO4 configuration; STROB_OUT trigger on GPIO21 |
| `acs37610_cmd.cpp` | Frame builders for all four commands; CRC computation; DATA[31:26] zeroed for EEPROM writes; access-code sequencing; verify-after-write; DATA[27:26] ECC pass/fail check on EEPROM reads |
| `power_ctrl.cpp` | DUT power-on/off sequencing, settle delays, STATUS_LED state machine |
| `usb_cdc.cpp` | Arduino `Serial` USB-CDC task; ASCII command parser; response formatter (Phase 5) |
| `crc3.cpp` | CRC-3 module: g(x)=x³+x+1, init=0b111, MSB first; SYNC excluded; unit-tested via `[env:native]` |

### 4.4 USB-CDC Command Protocol

| Command | Arguments | Action |
|---------|-----------|--------|
| `AUTH` | — | Send Write Access Code frame (`ADDR=0x31`, `DATA=0x2C413736`) |
| `WRAM <addr> <data>` | hex addr, hex data (26-bit) | Write volatile RAM register |
| `WEEP <addr> <data>` | hex addr, hex data (26-bit) | Write EEPROM (requires prior AUTH; enforces t_w delay; verifies) |
| `READ <addr>` | hex addr | Read register; returns hex data and ECC pass/fail status |
| `PWRON` | — | Enable DUT 3.3 V supply; wait settle delay |
| `PWROFF` | — | Disable DUT supply |
| `STATUS` | — | Return programmer state, last error code, DUT power state |

All responses include a status prefix: `OK`, `ERR <code>`, or `DATA <hex>`.

### 4.5 EEPROM Write Sequence (Firmware State Machine)

```
PWRON
  └─► AUTH (Write Access Code: ADDR=0x31, DATA=0x2C413736)
        └─► WEEP <addr> <data>
              ├─► Build frame (DATA[31:26]=0; compute CRC-3 over R/W+ADDR+DATA)
              ├─► Transmit Manchester frame via bit-bang TX
              ├─► Wait t_w (25 ms typical, 35 ms max)
              └─► READ <addr>  ←── Verify: compare returned DATA[25:0] to written value
                                           check DATA[27:26] ECC pass/fail status
                    ├─► Match + ECC OK → return OK
                    ├─► Mismatch        → return ERR_VERIFY
                    └─► ECC fault       → return ERR_ECC
```

---

## 5. Development Phases & Schedule

### Phase 1 — Datasheet Deep-Dive & Protocol Specification (Weeks 1–2) ✅ Complete

**Objectives:**
- ~~Obtain CRC polynomial and ECC algorithm~~ — **Closed (B1, B2):** CRC-3 g(x)=x³+x+1 init=0b111 confirmed; ECC generated automatically by device (see §4.1 note)
- ~~Map all register addresses for RAM and EEPROM~~ — **Closed (B3):** Full register map documented in §2.7
- ~~Confirm access code value~~ — **Closed (B4):** Access code confirmed as `0x2C413736`
- ~~Confirm PROG pin turnaround timing~~ — **Closed (B6):** No turnaround delay required; 74 µs inter-command delay confirmed
- ~~Set up VS Code + PlatformIO environment; verify ESP32-S3-DEVKITC-1N16R8V USB-CDC enumeration~~
- ~~Procure ACS37610 custom eval board~~

**Deliverables:** ✅ Development environment verified; eval board received; protocol specification signed off.

---

### Phase 2 — Hardware Design & Bring-Up (Weeks 2–4) ✅ Complete

**Objectives:**
- ~~Hardware setup uses the ESP32-S3-DEVKITC-1N16R8V development board — no custom PCB required for this phase~~
- ~~Prepare 50 mm 3-wire cable assembly (VCC, GND, PROG) with appropriate connectors for the custom ACS37610 eval board~~
- ~~Verify 3.3 V supply rail from DevKit powers the DUT eval board within spec (≤ 50 mA)~~
- ~~Add 10 kΩ pull-up resistor on PROG line to 3.3 V; add TVS diode (PRTR5V0U2X) for ESD protection~~
- ~~Bring-up checklist: USB-CDC enumeration on DevKit, PROG GPIO loopback, power rail measurements on DUT header~~

**Deliverables:** ✅ Wired prototype (DevKit + eval board + cable); bring-up checklist completed.

---

### Phase 3 — Manchester Driver & Encoder (Weeks 4–6)

**Objectives:**
- ~~Implement `crc3.cpp` module and unit-test via `[env:native]` (runs on PC, no hardware)~~ ✅ Complete — 6 Unity tests passing
- ~~Add `[env:native]` to `platformio.ini` for PC-hosted unit tests~~ ✅ Complete
- ~~Implement Manchester TX encoder (configurable bit period T, default 33 µs / 30 kbps)~~ ✅ Complete — bit-bang implementation (RMT TX abandoned; see §4.2)
- ~~Configure GPIO4 open-drain mode~~ ✅ Complete — `GPIO_MODE_INPUT_OUTPUT_OD`; RTC-domain pulldown also disabled via `rtc_gpio_pulldown_dis()`
- ~~Verify bit-period accuracy with oscilloscope using GPIO21 (STROB_OUT) as scope trigger~~ ✅ Complete — frame ΔX = 395.71 µs ≈ 12 × 32 µs; waveform correct on all bursts
- Implement RMT RX Manchester decoder with edge capture; set idle threshold ~50 µs, filter ~4 µs — **next**
- GPIO direction-switch logic for TX → RX turnaround
- Implement 74 µs inter-command delay between consecutive R/W operations
- Loopback self-test (TX GPIO → RX GPIO on same DevKit board via wire jumper)
- Unit-test all four command frame builders with known register values

**Deliverables:** ~~Passing `[env:native]` CRC-3 tests~~ ✅; ~~oscilloscope captures showing correct Manchester waveform at 30 kbps~~ ✅; RMT RX decoder + loopback test (pending).

---

### Phase 4 — DUT Integration & Full Command Set (Weeks 6–9)

**Objectives:**
- Connect real ACS37610 eval board via 50 mm 3-wire cable; begin with READ of FAULT_STATUS (`0x20`) — non-destructive diagnostic
- Read EE_CUST0 (`0x09`), EE_CUST1 (`0x0A`), EE_CUST2 (`0x0B`) and compare against factory defaults
- Capture PROG waveform with logic analyser; verify frame structure (SYNC, R/W, ADDR, DATA, CRC) against §2 spec
- Implement and test AUTH (`ADDR=0x31`, `DATA=0x2C413736`) → WEEP → verify sequence
- Implement WRAM writes to SH_CUST0 (`0x19`) and SH_CUST1 (`0x1A`) for iterative calibration
- Validate 74 µs inter-command delay and 35 ms t_w EEPROM delay in practice
- Handle and report all error conditions: CRC mismatch, ECC fault (DATA[27:26]), verify fail, timeout, WRITE_LOCK set

**Deliverables:** All four commands functional on real device; annotated logic-analyser captures; error handling verified.

---

### Phase 5 — Host GUI, Validation & Production Hardening (Weeks 9–12)

**Objectives:**
- Enable native USB-CDC (`ARDUINO_USB_CDC_ON_BOOT=1`) and implement `usb_cdc.cpp` host interface
- Python CLI / GUI over USB-CDC (command-line for engineering; simple GUI for operators)
- Automated test script: PWRON → AUTH → read-all → WRAM tune → WEEP commit → verify → PWROFF
- EEPROM write-cycle stress test (within device endurance limits)
- Operator guide and calibration procedure document
- Optional: Wi-Fi web UI for production floor (no laptop required)

**Deliverables:** Validated programmer; operator guide; production test script.

---

### Schedule Summary

| Phase | Weeks | Duration | Status |
|-------|-------|----------|--------|
| 1 — Protocol spec | 1–2 | 2 weeks | ✅ Complete |
| 2 — Hardware | 2–4 | 2 weeks | ✅ Complete |
| 3 — Manchester driver | 4–6 | 2 weeks | TX ✅ / RX pending |
| 4 — DUT integration | 6–9 | 3 weeks | Pending |
| 5 — GUI & validation | 9–12 | 3 weeks | Pending |
| **Total** | **1–12** | **12 weeks** | |

---

## 6. Bill of Materials

| Component | Part / Value | Qty | Purpose |
|-----------|-------------|-----|---------|
| Controller board | ESP32-S3-DEVKITC-1N16R8V | 1 | Main controller — development board with USB-CDC, 16 MB flash, 8 MB PSRAM |
| Current sensor eval board | ACS37610LLUATR-010B3 custom eval board | 1+ | Device under test |
| Interconnect cable | 3-wire, 50 mm, VCC / GND / PROG | 1 | Controller ↔ DUT connection |
| Pull-up resistor | 10 kΩ, 0402 or THT | 1 | PROG open-drain pull-up to 3.3 V |
| TVS diode | PRTR5V0U2X (SOT363) | 1 | ESD protection on PROG line |
| Breadboard / proto board | Half-size breadboard or small protoboard | 1 | Mount pull-up, TVS, and cable termination |
| Logic analyser | Saleae Logic 2 or compatible | 1 | PROG waveform capture and Manchester decode |
| Oscilloscope probe | 10× passive probe | 1 | Bit-period accuracy verification |

> **Note (B5):** BOM reflects the development-phase hardware (DevKit + eval board). A custom PCB is not required for this phase. BOM section is under continued review for production fixture requirements.

---

## 7. Risk Register

### R1 — Remaining undocumented protocol details
**Severity:** Low *(downgraded from High — CRC, register map, and access code are confirmed; ECC is device-managed)*  
**Probability:** Low  
**Mitigation:** CRC-3 polynomial, full register map, and access code are documented in §2 (B1–B4 closed). ECC is handled internally by the device — no firmware computation required. Obtain the full Allegro programming application note to catch any remaining edge cases (preamble requirements, error recovery). Implement configurable T in firmware.

---

### R2 — EEPROM write permanently damages sensor (OTP risk)
**Severity:** High  
**Probability:** Low (if procedure followed)  
**Mitigation:** Validate all register values in RAM first using WRAM. Require explicit `AUTH` + confirmation before any WEEP command. Enforce verify-after-write. Log write count per device serial number to track endurance.

---

### R3 — CRC implementation error in firmware
**Severity:** High  
**Probability:** Low *(algorithm confirmed — risk is now implementation, not specification)*  
**Mitigation:** CRC-3 (g(x)=x³+x+1, init=0b111) is confirmed (B1). Generate unit-test vectors analytically from the polynomial before coding. Unit-test `crc3.cpp` in the PlatformIO `[env:native]` environment before any DUT connection. Begin Phase 4 with READ-only operations to validate frame structure independently of write logic.

> **Note (v3):** ECC firmware computation has been removed — the device generates ECC automatically. CRC-3 is the only firmware-computed integrity check.

---

### R4 — Manchester TX timing error exceeds device tolerance
**Severity:** Low *(downgraded — bit-bang TX validated by scope)*  
**Probability:** Low  
**Mitigation:** TX is implemented as bit-bang on Core 1, priority 5 (`xTaskCreatePinnedToCore`). No RMT peripheral timing dependency. Scope-verified: frame duration 395.71 µs ≈ 12 × 32 µs; T = 32 µs = 31.25 kbps, within ACS37610 1–333 kbps tolerance. For RX (RMT-based), risk remains low since RMT RX only captures edges — it has no semaphore race with wait_for_done; validate with loopback test before DUT connection.

---

### R5 — EEPROM write timing violation (t_w not respected)
**Severity:** Medium  
**Probability:** Medium  
**Mitigation:** Implement fixed delay of 35 ms (max t_w) in firmware after every EEPROM write. Document this in the USB-CDC command response (caller will see the delay). Do not allow configurable override below 35 ms.

---

### R6 — USB-CDC driver issues on Windows
**Severity:** Low  
**Probability:** Low  
**Mitigation:** UART debug via CH343 bridge is used through Phase 4. Native USB-CDC is enabled in Phase 5 only. Test enumeration on Windows 10/11 at that stage. Install the Espressif USB CDC driver if required on older Windows systems.

---

## Appendix A — Critical First Steps

The following actions should be completed before hardware bring-up begins:

1. **Set up VS Code + PlatformIO** with the ESP32 Arduino Core. Verify the ESP32-S3-DEVKITC-1N16R8V enumerates as a USB-CDC serial device on the development PC. ✅ Complete

2. **Receive and inspect the ACS37610 custom eval board.** Verify supply voltage (3.3 V), check PROG pin accessibility, and confirm connector pinout before wiring. ✅ Complete

3. **Build and test the PROG line interface.** Wire the 10 kΩ pull-up and TVS diode on the breadboard. Verify the open-drain GPIO on the DevKit can drive the line low and release it cleanly before connecting to the DUT. ✅ Complete

4. **Contact Allegro for the full programming application note** to confirm any details not yet captured in this document (preamble requirements, error recovery behaviour, endurance specifications).

---

## Appendix B — Open Items Tracking

| # | Item | Status | Resolution |
|---|------|--------|------------|
| B1 | CRC-3 polynomial and initial value | ✅ Closed | g(x)=x³+x+1; init=`0b111`; SYNC excluded; covers R/W+ADDR+DATA (write) — see §2.3a |
| B2 | ECC algorithm for DATA[31:26] | ✅ Closed | Device generates ECC automatically on every EEPROM write; DATA[31:26] set to 0 by controller. ECC pass/fail reported in DATA[27:26] of read response — see §2.5.2, §2.6 |
| B3 | Full EEPROM and RAM register map | ✅ Closed | 6 registers documented — see §2.7 |
| B4 | Access code value | ✅ Closed | Confirmed `0x2C413736` (VOUT stays enabled) — see §2.4 |
| B5 | Hardware, Firmware, BOM, Risk sections | ✅ Closed | H/W: DevKit + custom eval board + 3-wire cable. Firmware: VS Code + PlatformIO + Arduino C++17. BOM and Risk: confirmed |
| B6 | PROG pin turnaround timing | ✅ Closed | No turnaround delay required; 74 µs inter-command delay confirmed — see §2.2 |

---

*End of document — ACS37610 Programmer Development Plan v4.0*
