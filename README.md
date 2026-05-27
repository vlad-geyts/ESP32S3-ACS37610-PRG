> From Chat GPT ---------------------------------------------------------------------------------------------
**Important Accuracy Notes**

The ESP32-S3 ADC is known for:

- nonlinearity
- noise
- chip-to-chip Vref variation

Raw readings are usually not precision-grade.
Typical internal Vref: 1100mV, but actual value varies between chips.


**Near-Ground Behavior**

The ADC can technically measure close to 0V, but:

- lowest few counts are noisy
- offset error exists
- readings below ~50mV may be unreliable

Practical low-end accuracy usually starts around: 20mV–50mV


**Source Impedance Matters**

ESP32 ADC sample-and-hold capacitor is small.

Recommended source impedance:

< 2.5kΩ ideally
< 10kΩ maximum practical

Higher impedance causes:

- gain error
- unstable readings
- nonlinearity

Often a buffer op-amp or RC filter is used.


**Recommended Real-World Range**

For good linearity on ESP32-S3:

Attenuation	Recommended Accurate Range
- 0 dB	0.1V – 0.9V
- `11 dB	0.15V – 2.8V`

Top-end near full scale becomes increasingly nonlinear.



> From Qwen ----------------------------------------------------------------------------------------

🔍 Key Rationale & Design Notes
1. ADC Selection (GPIO7 & GPIO8)

    Use `ADC1`, not `ADC2`: On ESP32-S3, `ADC2` is shared with Wi-Fi/Bluetooth radios. If your project ever enables wireless, `ADC2` readings will fail or block. `ADC1` is fully independent.
    Both on `ADC1: GPIO7 (ADC1_CH6)` and `GPIO8 (ADC1_CH7)` belong to the same controller, allowing unified calibration, attenuation settings, and continuous/oneshot driver usage.
    Calibration is mandatory: ESP32-S3 ADCs are non-linear. Use ESP-IDF's esp_adc_cal or Arduino's analogReadResolution() + lookup table for accurate voltage mapping.
    Attenuation: Set to 11 dB (or ADC_ATTEN_DB_11) for ~0–3.3V range. Lower attenuations cap at ~1.1V or ~1.5V.

2. I2C Selection (GPIO41 & GPIO42)

    Native multi-device support: I2C inherently supports multiple slaves on one bus. You only need:
        Unique 7-bit addresses for both devices
        External pull-up resistors (2.2kΩ–4.7kΩ to 3.3V)
        100kHz is Standard Mode, well within spec
    GPIO41/42 advantages: High-number GPIOs on S3 are free of strapping, boot, USB-JTAG, and ADC functions. They route cleanly through the GPIO matrix to I2C0 or I2C1.
    Bus sharing tip: If devices have identical addresses, you'll need an I2C multiplexer (e.g., TCA9548A). Otherwise, standard bus topology works.

🔄 Alternative Pin Options (if routing conflicts arise)
    For ADC:  GPIO8 & GPIO9  or GPIO1 & GPIO7 (avoid GPIO1 if UART0 is exposed)
    For I2C: GPIO38 & GPIO39 or GPIO15 & GPIO16 (note: these are ADC2 pins, but safe as digital I2C)
	
⚠️ Important Implementation Tips

    I2C Pull-ups: ESP32-S3 internal pull-ups (~40kΩ) are too weak for 100kHz with 2 devices. Add external 4.7kΩ resistors to 3.3V.
    ADC Sampling: Use adc_oneshot driver for simplicity. If you need synchronized readings, consider adc_continuous with DMA.
    Noise Mitigation: Place a 0.1µF decoupling capacitor near each analog input pin to VSS. Keep analog traces away from high-speed SPI lines (your OLED).

3. ESP-IDF/Arduino Setup:

>// I2C Init (ESP-IDF)
>i2c_config_t conf = {
>   .mode = I2C_MODE_MASTER,
>   .sda_io_num = 42,
>   .scl_io_num = 41,
>   .sda_pullup_en = GPIO_PULLUP_ENABLE,
>   .scl_pullup_en = GPIO_PULLUP_ENABLE,
>   .master.clk_speed = 100000,
>};
>i2c_param_config(I2C_NUM_0, &conf);
>i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);	

(Arduino equivalent uses `Wire.begin(42, 41, 100000)`)

Here is a complete, production-ready Arduino sketch tailored for your ESP32-S3 setup. It uses your +2.5V precision reference to dynamically calibrate the ADC gain, reads the battery through a voltage divider, and calculates State of Charge (SOC) using a standard 2S LiPo discharge curve approximation.

🔍 Key Implementation Notes
1. Voltage Divider Calculation

    You must update `DIVIDER_RATIO` to match your actual resistors:
    DIVIDER_RATIO = R_bottom / (R_top + R_bottom)

    Example: R_top = 120kΩ, R_bottom = 75kΩ → 75 / 195 = 0.3846

    At 8.4V battery: 8.4 × 0.3846 = 3.23V (safe within 3.3V ADC max)
    Use 1% tolerance resistors or measure the actual ratio with a DMM for best accuracy.

2. Why Software Calibration via 2.5V Reference?

    ESP32-S3 ADCs have inherent gain/offset errors that vary between chips and with temperature.
    Using a precision reference at runtime automatically compensates for these errors without needing ESP-IDF's deprecated `esp_adc_cal` or complex multi-point tables.
    The calibration runs once at boot. You can call `calibrateADC()` periodically if your reference source has low drift.

3. ADC Configuration & Arduino Version Compatibility

    The `#if ESP_ARDUINO_VERSION` block handles the API change between Arduino-ESP32 v2.x and v3.x.
    `ADC_11db/ADC_ATTEN_DB_11` sets the input range to `~0-3.3V`. This is required since your divider outputs up to `3.23V`
    
4. SOC Calculation Accuracy

    LiPo discharge curves are non-linear and load-dependent. The piecewise function used here matches standard light-load curves (~0.2C).
    For higher accuracy under varying loads, replace `calculateSOC()` with a 1D lookup table interpolated by voltage, or integrate current (Coulomb counting) if you have a shunt..

5. Hardware Recommendations

    Add a `100nF` ceramic capacitor from `GPIO7` to `GND` (as close to the pin as possible) to filter high-frequency noise from the voltage divider.
    Keep `GPIO8` (2.5V ref) isolated from digital switching noise. Use a short trace and add a `10kΩ` series resistor if the reference source has high output impedance.
    Ensure your voltage divider resistors are `≥100kΩ` total to minimize battery drain (`<50µA` at 8.4V).

🛠️ Optional: On-Demand Recalibration
If you want to recalibrate without rebooting (e.g., via serial command or button press), simply call:
>is_calibrated = false;
>calibrateADC();

`I see that calibration() function does perform only ADC gain calibration, by using 2.5V reference source. We also need to compensate ADC DC offset. Should we all we use for that purpose the 3rd ADC input connected to GND? `

You are absolutely correct. Compensating for DC offset alongside gain significantly improves accuracy, especially at low voltages. Using a 3rd ADC channel tied to a solid GND reference is the standard, practical approach for runtime 2-point linear calibration.
Here is the updated, production-ready Arduino sketch with offset + gain calibration, plus hardware recommendations to ensure reliable offset measurement.

🔍 How the 2-Point Calibration Works
The ESP32-S3 ADC transfer function is approximately linear but has two systematic errors:

Offset Error: `ADC(0V) ≠ 0 counts`
Gain Error: `Slope ≠ Ideal (3.3V / 4095)`

By measuring two known points:
`V₁ = 0V → Raw₁ = offset_counts`
`V₂ = 2.5V → Raw₂ = ref_avg`

We solve for the calibrated voltage:
`V_calibrated = (Raw - offset_counts) × [V_ref / (ref_avg - offset_counts)]`
This automatically cancels both offset and gain errors in a single runtime operation.

⚠️ Critical Hardware Recommendations for Offset Pin
To ensure the offset measurement is stable and accurate:

- GND Connection: Tie `GPIO9` directly to the analog ground plane (not digital GND). Use a short, thick trace.

- Noise Filtering: Place a `100nF` ceramic capacitor from `GPIO9 to AGND`. Add a `10kΩ` series resistor if GND has switching noise.

- Pin Configuration: Do not enable internal pull-ups/pull-downs on `OFFSET_ADC_PIN`. Leave it floating in hardware but tied to `GND` externally.

- Averaging: `CAL_SAMPLES = 500` is intentional. Offset drifts slightly with temperature and power-up transients. More samples = stable baseline.

📊 Is Offset Calibration Really Necessary?

- LiPo SOC monitoring: 	Offset typically contributes `<15mV` error at 11dB attenuation. After voltage divider scaling, this is `~0.05V` at battery terminals. SOC error: `~1-2%`. Often acceptable without offset calibration.

- Precision instrumentation: Mandatory. Use the 2-point method above.

- Wide temperature range: Offset drifts `~0.5-1 count/°C`. Consider periodic recalibration or store factory offset in NVS.



> Question by me -----------------------------
*Please explain in details, how the Exponential Moving Average code is working. It is used inside readBatteryVoltage() function.*

> Replay by Qwen -----------------------------
The `Exponential Moving Average (EMA)` in your code is a recursive digital low-pass filter. It smooths out ADC noise and load transients while tracking slow voltage changes, using minimal CPU and memory. Here's a detailed breakdown of how it works in your specific implementation.

🔢 1. **The Core Formula**
The mathematical definition of EMA is:
y[n] = α · x[n] + (1 - α) · y[n-1]

- x[n]    v_battery (current raw reading, already calibrated & scaled);
- y[n]    bat_voltage_filtered (new filtered output);
- y[n-1]  bat_voltage_filtered (previous filtered output, stored globally);
- α       0.15 (smoothing factor, 15% weight to new data)

>bat_voltage_filtered = (alpha * v_battery) + ((1.0 - alpha) * bat_voltage_filtered);
is a direct implementation of this formula.

⚙️ 2. **Step-by-Step Execution**
Because `bat_voltage_filtered` is declared as a global variable, its value persists between `loop()` calls. Here's what happens each second:

1. **Read & Convert**: `v_battery` is calculated from 32 averaged ADC samples → calibrated → scaled by divider ratio.
2. **Fetch Previous State**: The CPU reads the current value of `bat_voltage_filtered` from RAM (this is y[n-1]).
3. **Blend**: 

    `15%` of the new reading is added
    `85%` of the previous filtered value is retained

4. **Update State**: The result overwrites `bat_voltage_filtered` (becomes y[n] for the next cycle).
5. **Return**: The smoothed value is used for SOC calculation and serial output.

**Numerical Example**  (assuming steady `7.80V` battery):

Call#           x[n]            y[n-1]             y[n]
  1            7.80V            0.00V          	   0.15×7.80 + 0.85×0.00 = 1.17V
  2            7.80V            0.17V              0.15×7.80 + 0.85×1.17 = 2.16V
  3            7.80V            2.16V              0.15×7.80 + 0.85×2.16 = 3.01V
  ...
  10           7.80V            6.52V              7.28V
  ...
  20           7.80V            7.72V              7.79V
  ...
  ~            7.80V            7.80V              7.80V

  The filter exponentially converges to the true value. After `~20 seconds`, it's within `1%` of the actual voltage.


 📈 3. **Why** `α = 0.15`? **(Response vs Smoothing Trade-off)** 
 - Low α (e.g., 0.05): Heavy smoothing, ignores noise well, but reacts slowly to real battery changes.
 - High α (e.g., 0.5): Fast response, but passes more ADC jitter to the SOC calculator.
 - α = 0.15: A practical balance for battery monitoring at 1 Hz sampling:
    - *Effective time constant*: `τ ≈ Δt / α ≈ 1s / 0.15 ≈ 6.7s`
    - *95% settling time*: `~3τ ≈ 20s` (perfect for tracking slow LiPo discharge)
    - *Noise attenuation*: Reduces high-frequency ADC/RF noise by `~18 dB` at 1 Hz cutoff.

⚠️ 4. **The "Cold Start" Quirk & Simple Fix**
    Since `bat_voltage_filtered` initializes to `0.0`, the first few readings will ramp up from zero. This is usually harmless for SOC, but you can eliminate it by *seeding the filter on the first valid reading*:

 >   // Add this inside readBatteryVoltage(), before EMA line:
 >   static bool first_run = true;
 >   if (first_run) {
 >       bat_voltage_filtered = v_battery; // Seed with actual reading
 >       first_run = false;
 >   }

 🆚 5. **Why EMA Instead of a Simple Moving Average (SMA)?**

    Metric              EMA                                 SMA

    Memory              1 float (4 bytes)                   N floats (e.g. 32x4 = 128 bytes)
    CPU per call        2mul, 1 add. 1 sub                  N additions + 1 division
    Responsiveness      Exponential decay (always current)  Fixed window (lag exactly N/2 samples)
    Implementation      Trivial                             Requires circular buffer or memove()  

For an ESP32-S3 running Wi-Fi/SPI/I2C, EMA is the `industry standard` for real-time sensor filtering due to its O(1) complexity.


🎛️ 6. **How to Tune** alpha **for Your Use Case**

*Scenario*                                  *Recommended*              *Behavior*
Stable bench testing                        0.05                       Very smooth, `~60s` to settle
Normal battery monitoring                   0.10 - 0.15                Balanced noise rejection & responsiveness
High-load transients (motors, RF bursts)    0.20 - 0.30                Faster tracking, slightly more noise
Need step-change detection                  0.50 + or switch           Reacts in 1-2 seconds                          
(e.g., charger plugged)                     to adaptive `a`

*Pro Tip*: If you want dynamic smoothing, make `alpha` adaptive:

>float delta = abs(v_battery - bat_voltage_filtered);
>float adaptive_alpha = constrain(delta * 2.0, 0.05, 0.5);
>bat_voltage_filtered = (adaptive_alpha * v_battery) + ((1.0 - adaptive_alpha) * bat_voltage_filtered);

This increases responsiveness during sudden voltage changes while maintaining heavy smoothing during steady state.


> Qustion by Me --------------------------------------------------
*What are pros and cons of using raw ADC counts vs scaled voltage?*


> Respond by Qwen ------------------------------------------------
📊 *Quick Verdict*
For `constant calibration parameters`, filtering raw ADC counts vs. scaled voltage is `mathematically identical`. The choice comes down to `workflow, debugging, and how you handle calibration updates`.

🔍 **Detailed Comparison**