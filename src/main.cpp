#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "unused_gpio.h"
#include "manchester.h"
#include "crc3.h"

// WS2812 Configuration
#define WS2812_PIN      48
#define NUM_LEDS        1
#define LED_BRIGHTNESS  8  // 0-255 (adjust to your preference)
#define LED_TYPE        (NEO_GRB + NEO_KHZ800)

//C++: Namespaces & Constexpr --- 
namespace Config {

    // === Debug signals ===
    constexpr int StrobOut  = 21;
  
     // === RGB Led ===
    constexpr int LedPin = WS2812_PIN;
}

// Global Objects
Adafruit_NeoPixel ws2812(NUM_LEDS, WS2812_PIN, LED_TYPE);

// Function Prototypes
void heartbeatTask(void *pvParameters);
void programmerTask(void *pvParameters);
void gpioConfig();

void setup() {
    // Delay to warm up 
    delay(1000);
    Serial.begin(115200);

    //Terminate unused GPIOs EARLY (before peripheral init)
    ConfigureUnusedGpios();

    // Configure Hardware using our Namespace
    gpioConfig();
    ws2812.begin();

    // Manchester TX (bit-bang) and RX (RMT_CHANNEL_1) — 30 kbps, T=33 µs
    manchester_tx_init(33);
    manchester_rx_init(33);

    // Programmer task on Core 1, priority 5 (timing-critical RMT work)
    xTaskCreatePinnedToCore(programmerTask, "Programmer", 8192, NULL, 5, NULL, 1);

    // Standard Heartbeat (Priority: 0) on Core 0
    xTaskCreatePinnedToCore(heartbeatTask, "Heartbeat", 4096, NULL, 0, NULL, 0);
}

void loop() {
    // Arduino task is no longer needed
    vTaskDelete(NULL);
}

void gpioConfig() {
   // Configure Hardware using our Namespace
    pinMode(Config::LedPin, OUTPUT);
    pinMode(Config::StrobOut, OUTPUT);
    digitalWrite(Config::StrobOut, LOW);
}

// Programmer task — Core 1, priority 5.
// Sends a READ request for FAULT_STATUS (0x20) every 2 s, then receives
// the 44-bit device response via RMT Manchester RX and prints the decoded fields.
void programmerTask(void *pvParameters) {
    const uint8_t addr = 0x20;                      // FAULT_STATUS register
    const uint8_t crc  = crc3_read_request(addr);  // = 5 (verified by unit test)

    // 12-bit TX frame, MSB-first:
    // bits[11:10]=SYNC=00, bit[9]=R/W=1, bits[8:3]=ADDR[5:0], bits[2:0]=CRC[2:0]
    const uint64_t tx_frame = ((uint64_t)1    << 9) |
                               ((uint64_t)addr << 3) |
                               ((uint64_t)crc);

    Serial.printf("[PROG] READ FAULT_STATUS  frame=0x%03llX  CRC=%d\n", tx_frame, crc);

    for (;;) {
        manchester_tx_send(tx_frame, 12);

        // Device responds within 74 µs; arm RMT RX and wait up to 100 ms.
        // Response frame: SYNC[2] | R/W[1] | ADDR[6] | DATA[32] | CRC[3] = 44 bits
        uint64_t response = 0;
        const uint8_t rx_bits = manchester_rx_receive(&response, 100);

        if (rx_bits == 44) {
            const uint8_t  rx_sync = (uint8_t)((response >> 42) & 0x3);
            const uint8_t  rx_rw   = (uint8_t)((response >> 41) & 0x1);
            const uint8_t  rx_addr = (uint8_t)((response >> 35) & 0x3F);
            const uint32_t rx_data = (uint32_t)((response >> 3)  & 0xFFFFFFFFUL);
            const uint8_t  rx_crc  = (uint8_t)(response & 0x7);
            Serial.printf("[RX]  SYNC=%d R/W=%d ADDR=0x%02X DATA=0x%08X CRC=%d\n",
                          rx_sync, rx_rw, rx_addr, rx_data, rx_crc);
        } else {
            Serial.printf("[RX]  timeout or decode error (%d bits)\n", rx_bits);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void heartbeatTask(void *pvParameters) {
    bool ledOn = false;

    // Initialize LED to OFF state
    ws2812.setBrightness(LED_BRIGHTNESS);
    ws2812.setPixelColor(0, 0, 0, 0);
    ws2812.show();

    for (;;) {   
        if (ledOn) {
            // Heartbeat ON
            ws2812.setPixelColor(0, ws2812.Color(0, 255, 0));   // GREEN is ON   
        } else {
            // Heartbeat OFF
            ws2812.setPixelColor(0, 0, 0, 0);                   // BLACK is OFF
        }
        
        ws2812.show();          // Push data to the LED
        ledOn = !ledOn;         // Toggle state

        vTaskDelay(pdMS_TO_TICKS(500));  
    }
}


