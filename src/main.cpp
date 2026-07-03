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

    // === Power enable signal ===
    constexpr int PwrEn  = 5;

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

    // Enable 3.3V power supply 
    digitalWrite(Config::PwrEn, LOW);
    delay(100); // delay to stabilize 3.3V power rail

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
    pinMode(Config::PwrEn, OUTPUT);
//    digitalWrite(Config::PwrEn, HIGH); // turn of 3.3V LDO
//    delay(1000); //delay to set 3.3V rail to 0
}

// Programmer task — Core 1, priority 5.
// Boot sequence: send Access Code to open the device serial port, wait 120 µs settle.
// Then loop every 2 s: send READ FAULT_STATUS request, receive and print 44-bit response.
void programmerTask(void *pvParameters) {

    // Enable 3.3V power supply 
    digitalWrite(Config::PwrEn, LOW);
    delay(3000); // delay to stabilize 3.3V power rail

    // --- Access Code (opens device serial port, mandatory after every power cycle) ---
    // 44-bit write frame: SYNC=00 | R/W=0 | ADDR=0x31 | DATA=0x2C413736 | CRC[3]
    const uint32_t kAccessData = 0x2C413736UL;
    const uint8_t  kAccessAddr = 0x31;
    const uint8_t  ac_crc      = crc3_write(0, kAccessAddr, kAccessData);

    const uint64_t ac_frame    = ((uint64_t)kAccessAddr  << 35) |
                                  ((uint64_t)kAccessData  <<  3) |
                                  ((uint64_t)ac_crc);

    Serial.printf("[AUTH] Sending Access Code  frame=0x%011llX  CRC=%d\n", ac_frame, ac_crc);
    manchester_tx_send(ac_frame, 44, /*start_mark=*/true, /*end_mark=*/true);

    // Wait 120 µs post-Access-Code settle before issuing any Read/Write command.
    esp_rom_delay_us(500);

    Serial.println("[AUTH] Port open — starting Read loop");

    // Cycle several registers so the serial log yields multiple (DATA, CRC)
    // pairs — needed to pin down the exact span the device's response CRC covers.
    const uint8_t rd_addrs[] = {0x20, 0x09, 0x0A};
    size_t rd_idx = 0;

    for (;;) {
        const uint8_t  rd_addr  = rd_addrs[rd_idx];
        rd_idx = (rd_idx + 1) % (sizeof(rd_addrs) / sizeof(rd_addrs[0]));
        const uint8_t  rd_crc   = crc3_read_request(rd_addr);
        const uint64_t rd_frame = ((uint64_t)1       << 9) |
                                   ((uint64_t)rd_addr << 3) |
                                   ((uint64_t)rd_crc);

        Serial.printf("[PROG] READ 0x%02X  frame=0x%03llX  CRC=%d\n", rd_addr, rd_frame, rd_crc);
        // arm_rx=true: RMT is armed inside TX just before PROG is released,
        // so capture starts before the device has a chance to respond.
        manchester_tx_send(rd_frame, 12, /*start_mark=*/false, /*end_mark=*/false, /*arm_rx=*/true);

        // Response frame (plan §2.6): SYNC[2] | DATA[32] | CRC[3] = 37 bits.
        // Hardware shows the leading sync bit(s) may merge into the device's
        // start mark, so accept 35–37 bits; DATA and CRC anchor to the LSB end.
        uint64_t response = 0;
        const uint8_t rx_bits = manchester_rx_receive(&response, 200);

        if (rx_bits >= 35 && rx_bits <= 37) {
            const uint32_t rx_data = (uint32_t)((response >> 3) & 0xFFFFFFFFUL);
            const uint8_t  rx_crc  = (uint8_t)(response & 0x7);
            // Response CRC covers DATA[32] only — hardware-verified 2026-07-03
            // against live captures (including changing TEMP_OUT data in 0x20).
            const bool crc_ok = (crc3_response(rx_data) == rx_crc);
            Serial.printf("[RX]  ADDR=0x%02X DATA=0x%08X CRC=%d %s\n",
                          rd_addr, rx_data, rx_crc, crc_ok ? "OK" : "** CRC FAIL **");
        } else {
            // Distinguish a true timeout (no edges captured) from a decode failure,
            // and dump the raw pulse train for post-mortem (H/L + width in µs).
            uint32_t raw[96];
            const size_t rc = manchester_rx_last_raw(raw, 96);
            if (rc == 0) {
                Serial.println("[RX]  timeout — RMT captured no edges");
            } else {
                Serial.printf("[RX]  decode error: got %d bits (expected 35-37), raw items=%u\n",
                              rx_bits, (unsigned)rc);
                Serial.print("[RX]  pulses:");
                for (size_t i = 0; i < rc; ++i) {
                    const uint32_t d0 = raw[i] & 0x7FFFu;
                    const uint32_t l0 = (raw[i] >> 15) & 1u;
                    const uint32_t d1 = (raw[i] >> 16) & 0x7FFFu;
                    const uint32_t l1 = (raw[i] >> 31) & 1u;
                    if (d0) Serial.printf(" %c%lu", l0 ? 'H' : 'L', d0 / 10u);  // ticks→µs
                    if (d1) Serial.printf(" %c%lu", l1 ? 'H' : 'L', d1 / 10u);
                }
                Serial.println();
            }
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

        vTaskDelay(pdMS_TO_TICKS(600000));  
    }
}


