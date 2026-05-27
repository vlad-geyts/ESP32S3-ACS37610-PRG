`By Gimini`

This sounds like an excellent engineering project, Vlad. The Allegro ACS37610 is a highly precise coreless current sensor, and building a custom programming fixture using the ESP32-S3 is a great way to handle its specific Manchester-encoded single-wire interface.

Since the PROG pin on the ACS37610 combines both transmit and receive over a single line (often requiring specific voltage levels or open-drain configurations with pull-ups depending on the exact implementation of Mode 2), getting Phase 1 solid is the perfect starting point.

I am absolutely ready to help you drive this through all three phases using VS Code + PlatformIO (Arduino framework) and modern C++17.

**Technical Overview & Hardware Considerations**
Before diving straight into the Phase 1 software architecture, we should quickly note the physical layer requirements for the ACS37610's `PROG` pin:

- `Bidirectional Single-Wire`: Because communication happens over a single `PROG` pin, your ESP32-S3 firmware will need to dynamically switch a GPIO between Output (for writing/initiating) and Input (for reading), or use an external hardware transceiver circuit (like an open-drain buffer) to protect the MCU lines.
- Voltage Levels: Ensure the programming voltage levels ($V_{CC}$ and $V_{PRG}$) match the ACS37610 datasheet specs. If it requires voltages higher than 3.3V for programming EEPROM registers, you'll need level-shifting circuitry, though Phase 1 (Read Only) typically operates at standard logic levels.

**Phase #1 Strategy: Read Only Function**
To achieve robust Manchester decoding on the ESP32-S3 without blocking the CPU, we can leverage the ESP32's hardware peripherals. Given your background with oscilloscopes and timing analysis, we have two primary architectural paths for Phase 1:

**Option A: The Hardware RMT (Remote Control) Peripheral**
The ESP32-S3 features an RMT module that acts like a mini logic analyzer/pattern generator. It captures edge transitions and pulses lengths in hardware, storing them directly into RAM. This is highly precise and completely eliminates interrupt latency variations caused by the RTOS.

**Option B: Interrupt-Driven (GPIO ISR) + Timer**
Capture edge transitions using a GPIO interrupt attached to a high-resolution hardware timer (e.g., `esp_timer_get_time()`). You can push these delta-times into a FreeRTOS lock-free queue or ring buffer for processing in a dedicated worker task.

**Getting Started with Phase 1**
To kick things off smoothly, let's establish the baseline structure for your PlatformIO project
1. `platformio.ini` **Configuration**

Ensure your environment is explicitly targeting the ESP32-S3 and enabling C++17 compilation flags:

[env:esp32-s3-wroom-1]
platform = espressif32
board = esp32-s3-devkitc-1 ; Match your specific WROOM-1 dev board variant
framework = arduino
monitor_speed = 115200

build_unflags = -std=gnu++11
build_flags = 
    -std=gnu++17
    -D CORE_DEBUG_LEVEL=3 ; Enable Info log levels

2. **Conceptual Phase 1 Software Architecture**   

For a clean, maintainable C++17 design, we can abstract the protocol into a dedicated decoder class. Here is a high-level architectural layout of how we can structure the read operation using an ISR/Task topology:

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <vector>
#include <optional>

// Configuration
constexpr uint8_t PROG_PIN = 47; // Choose an appropriate GPIO
constexpr size_t QUEUE_SIZE = 128;

// Thread-safe queue to pass edge timings (in microseconds) from ISR to Decoder Task
static QueueHandle_t edgeQueue = nullptr;

// Interrupt Service Routine capturing edge intervals
void IRAM_ATTR handleProgEdge() {
    static uint64_t lastTime = 0;
    uint64_t currentTime = esp_timer_get_time();
    uint32_t deltaTime = static_cast<uint32_t>(currentTime - lastTime);
    lastTime = currentTime;

    // Read current state to know if it was a rising or falling edge
    uint32_t pinState = digitalRead(PROG_PIN);
    
    // Pack state and timing (Bit 31 = state, Bits 0-30 = delta time)
    uint32_t packet = (pinState << 31) | (deltaTime & 0x7FFFFFFF);
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(edgeQueue, &packet, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

class ManchesterDecoder {
public:
    void processEdge(uint32_t state, uint32_t deltaTimeUs) {
        // TODO: Implement ACS37610 Manchester decoding state machine
        // Identify preamble, sync bits, data bits, and error checking (CRC/Parity)
    }
    
    std::optional<uint32_t> getLatestRegisterValue() {
        // Return decoded register data if available
        return std::nullopt;
    }
};

void manchesterDecoderTask(void* pvParameters) {
    ManchesterDecoder decoder;
    uint32_t receivedPacket = 0;

    while (true) {
        if (xQueueReceive(edgeQueue, &receivedPacket, portMAX_DELAY) == pdTRUE) {
            uint32_t state = receivedPacket >> 31;
            uint32_t deltaTimeUs = receivedPacket & 0x7FFFFFFF;

            decoder.processEdge(state, deltaTimeUs);
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    edgeQueue = xQueueCreate(QUEUE_SIZE, sizeof(uint32_t));
    
    pinMode(PROG_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PROG_PIN), handleProgEdge, CHANGE);

    xTaskCreatePinnedToCore(
        manchesterDecoderTask,
        "Manchester_Decoder",
        4096,
        nullptr,
        3, // High priority task
        nullptr,
        1  // Run on Core 1
    );
}

void loop() {
    // Phase 3 will handle UI, for now we can print status updates here
    vTaskDelay(pdMS_TO_TICKS(1000));
}

`Gemini`: Which decoding approach would you prefer to implement first for Phase 1: using the hardware RMT peripheral for ultra-precise sampling, or continuing down the GPIO ISR + FreeRTOS queue path outlined above?

`ME`: Just to confirm that the device has an internal charge pump to generate the EEPROM pulses. It means we do not need voltage levels more than 3.3V on PROG to complete write into EEPROM. I'd like to use the hardware RTM peripheral for decoding.

That is fantastic news regarding the internal charge pump! Not needing a high-voltage external programming supply simplifies your hardware design immensely. Since the ACS37610 handles the EEPROM programming voltage internally, a standard 3.3V open-drain or bidirectional configuration directly from the ESP32-S3 is all you need to protect the lines.

Choosing the `RMT (Remote Control) peripheral` for decoding is an excellent engineering decision. It completely bypasses CPU interrupt latency by using a dedicated hardware sub-system to sample and record the duration of high and low states directly into RAM.

**Phase 1: Understanding the RMT as a Receiver**
In the ESP32-S3, the RMT peripheral clocks incoming transitions into a 32-bit data structure (`rmt_item32_t`). Each item contains the duration of a pulse and its logical level.

For Manchester decoding, where timing between transitions defines the clock and data (e.g., a transition in the middle of a bit period), the RMT allows you to read a whole frame into memory and then decode the entire sequence deterministically in a worker task.

For Manchester decoding, where timing between transitions defines the clock and data (e.g., a transition in the middle of a bit period), the RMT allows you to read a whole frame into memory and then decode the entire sequence deterministically in a worker task.

**ESP-IDF RMT Driver Setup (C++17)**
The ESP32-S3 uses the newer ESP-IDF v5.x RMT driver, which is structured around a Receive Channel configuration. Here is how we configure the RMT to listen on the `PROG` pin, filter out glitches, and prepare for Phase 1.

1. **Source Implementation**
Add this to your src/main.cpp to initialize the RMT receiver and pass the captured pulse trains to a processing task.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/rmt_rx.h>
#include <vector>
#include <optional>

// Configuration
constexpr int PROG_PIN = 47; 
constexpr uint32_t RMT_CLK_RES_HZ = 1000000; // 1 MHz resolution = 1 tick per microsecond
constexpr size_t RX_BUFFER_SIZE = 256;       // Number of rmt_symbol_word_t structures

static rmt_channel_handle_t rx_channel = nullptr;
static QueueHandle_t rmtEventQueue = nullptr;

// Structure to pass RMT data to our decoder task
struct RmtEvent {
    std::vector<rmt_symbol_word_t> symbols;
};

// Callback triggered when RMT finishes receiving a packet (e.g., after a timeout/idle period)
static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* edata, void* user_ctx) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Create an event container
    RmtEvent event;
    event.symbols.assign(edata->received_symbols, edata->received_symbols + edata->num_symbols);
    
    // Send to processing task
    xQueueSendFromISR(rmtEventQueue, &event, &xHigherPriorityTaskWoken);
    
    return xHigherPriorityTaskWoken == pdTRUE;
}

class ManchesterDecoder {
public:
    void decodeSymbols(const std::vector<rmt_symbol_word_t>& symbols) {
        if (symbols.empty()) return;

        Serial.printf("Captured %d RMT symbols:\n", symbols.size());
        
        for (const auto& symbol : symbols) {
            // rmt_symbol_word_t contains: .duration (15-bit) and .level (1-bit)
            uint32_t durationUs = symbol.duration;
            uint32_t level = symbol.level;
            
            // Print raw timings to Serial Monitor for logic analysis
            Serial.printf("L: %d, T: %d us | ", level, durationUs);
        }
        Serial.println("\n--- End of Frame ---");

        // TODO: Implement the specific ACS37610 state machine here
        // 1. Identify preamble / sync sequence durations
        // 2. Parse bit boundaries based on expected Manchester bit-rate
    }
};

void rmtProcessingTask(void* pvParameters) {
    ManchesterDecoder decoder;
    RmtEvent event;

    while (true) {
        if (xQueueReceive(rmtEventQueue, &event, portMAX_DELAY) == pdTRUE) {
            decoder.decodeSymbols(event.symbols);
        }
    }
}

void initRmtReceiver() {
    // 1. Configure the RX channel
    rmt_rx_channel_config_t rx_chan_config = {
        .gpio_num = static_cast<gpio_num_t>(PROG_PIN),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_CLK_RES_HZ,
        .mem_block_symbols = 64, // Amount of dedicated hardware RAM
        .flags = {
            .with_dma = false, // Not needed for small EEPROM register packets
            .io_loop_back = false,
            .io_invert_input = false,
        }
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_channel));

    // 2. Configure thresholds (In Manchester, missing edges mean transmission ended)
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_register_rx_event_callbacks(rx_channel, &cbs, nullptr));

    // Set idle timeout: If line stays quiet for say, 500us, consider the transmission complete
    rmt_receive_config_t rx_config = {
        .signal_range_min_ns = 1000,     // Filter out glitches shorter than 1us
        .signal_range_max_ns = 500000,   // Timeout frame if line holds state > 500us
    };
    
    // Enable and start the receiver
    ESP_ERROR_CHECK(rmt_enable(rx_channel));
    ESP_ERROR_CHECK(rmt_receive(rx_channel, nullptr, 0, &rx_config)); // Put into continuous receive mode
}

void setup() {
    Serial.begin(115200);
    while(!Serial);

    rmtEventQueue = xQueueCreate(10, sizeof(RmtEvent));

    xTaskCreatePinnedToCore(
        rmtProcessingTask,
        "RmtProcessor",
        8192,
        nullptr,
        3,
        nullptr,
        1
    );

    initRmtReceiver();
    Serial.println("RMT Receiver Initialized. Awaiting ACS37610 Manchester data...");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}


**Next Steps for Phase 1**

With this skeleton code running on your dev board:
- It will initialize the hardware RMT, wait for transitions on GPIO47, and automatically group them into single-frame bursts when the communication stops.

- It dumps the pulse durations and logic levels straight to your Serial Monitor.

To build out the specific algorithm inside decodeSymbols, what is the expected data bit rate (or bit-period duration in microseconds) specified in the ACS37610 datasheet for Mode 2 communication?




`By Claude`
Here's the updated `v2.0 plan`. Here's a summary of every change made from your B1–B6 feedback:
- *B1 — CRC (§2.3a, new dedicated section)*
    g(x) = x³+x+1, init = 0b111, SYNC excluded from calculation. CRC coverage spelled out per command type (write vs. read request vs. device response).
- *B2 — ECC (§2.5.2 and firmware modules)*
    6-bit Hamming code over DATA[25:0] → DATA[31:26]. Referenced consistently in the write table, EEPROM write section, firmware module list, and the EEPROM write state machine.
- *B3 — Register map (§2.7, new full section)*
    All 6 registers documented with addresses, R/W access, and full bit-field tables: EE_CUST0 (0x09), SH_CUST0 (0x19), EE_CUST1 (0x0A), SH_CUST1 (0x1A), EE_CUST2 (0x0B), FAULT_STATUS (0x20). Added a ⚠️ warning on WRITE_LOCK bit [25].
- *B4 — Access code (§2.4 and all references)*
    Corrected to 0x2C413736 (from 0x2C413737) with a note that VOUT remains enabled. Updated in the command table, EEPROM state machine, and Appendix A.
- *B5 — Hardware & Firmware*
    Controller updated to ESP32-S3-DEVKITC-1N16R8V throughout. Device noted as custom eval board. Connection is 50 mm 3-wire cable — no custom PCB for this phase. Firmware stack updated to VS Code + PlatformIO + Arduino framework everywhere (replaced ESP-IDF/FreeRTOS/TinyUSB references).
- *B6 — Turnaround timing (§2.2 and Phase 3)*
    No turnaround delay required — device responds immediately. 74 µs inter-command delay confirmed and added to the timing table and Phase 3 objectives.




`Qustion by Me`
*Hi Claude. I need to clarify the communication  port to be used for the host on which we'll run user GUI. The ESP32-S3-WROOM-1 has 2 USB ports.  UST-OTG and USB to UART. Can we use USB to UART for low level debugging as a terminal and use native USB-OTG for host communication. Please advise.*



`Replay by Claude`
Yes, your proposed split is exactly the right approach, and the DevKitC-1 hardware is purpose-built to support it. Here's the full picture:

**The two USB ports on the DevKitC-1**
The board has two distinct USB connections. The USB-to-UART port (Micro-USB) is used for power supply, flashing firmware, and communication via the on-board USB-to-UART bridge chip (Silicon Labs CP210x). The ESP32-S3 USB port (also Micro-USB) connects directly to the ESP32-S3's native USB OTG peripheral on GPIO19/20 — no bridge chip in between.

Because the DevKitC also includes a Silicon Labs CP210x USB-to-UART bridge accessible via a separate Micro-USB port, you can power, program, and monitor the output of the ESP32-S3 while its USB peripheral is connected to another device simultaneously. This is precisely your use case.


**Recommended port assignment**
`USB-to-UART (CP210x)` Micro-USBLow-level debug terminal — Serial output, ESP-IDF monitor, PlatformIO serial monitor, flashing
`ESP32-S3 USB (OTG/CDC)` Micro-USBHost GUI communication — the Python programmer GUI connects here