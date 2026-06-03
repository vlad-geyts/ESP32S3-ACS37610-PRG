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

    // Manchester TX — initialise RMT channel (30 kbps, T=33 µs)
    manchester_tx_init(33);

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
// Phase 3: transmit a known READ request frame every 2 s for scope verification.
// READ FAULT_STATUS (0x20): R/W=1, ADDR=0x20, CRC computed by crc3_read_request().
void programmerTask(void *pvParameters) {
    const uint8_t addr = 0x20;                      // FAULT_STATUS register
    const uint8_t crc  = crc3_read_request(addr);  // = 5  (verified by unit test)

    // Read request frame: SYNC[2]=00 | R/W[1]=1 | ADDR[6] | CRC[3]  = 12 bits
    // Pack MSB first: bit11..0 = 0,0,1, a5..a0, c2..c0
    uint64_t frame = ((uint64_t)1   << 9) |   // R/W = 1
                     ((uint64_t)addr << 3) |   // ADDR[6]
                     ((uint64_t)crc);          // CRC[3]
    // Prepend SYNC (2 zero bits) → shift everything up 2
    frame <<= 2;                               // SYNC[1:0] = 0b00 (already zero)

    Serial.printf("[PROG] READ FAULT_STATUS frame = 0x%03llX  CRC=%d\n", frame >> 2, crc);

    for (;;) {
        manchester_tx_send(frame, 12);
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


