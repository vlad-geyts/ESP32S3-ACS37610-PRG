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
    //manchester_tx_init(33);
    //manchester_rx_init(33);

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

    // --- Access Code (opens device serial port, mandatory after every power cycle) ---
    // 44-bit write frame: SYNC=00 | R/W=0 | ADDR=0x31 | DATA=0x2C413736 | CRC[3]
//    const uint32_t kAccessData = 0x2C413737UL;
//    const uint8_t  kAccessAddr = 0x31;
//    const uint8_t  ac_crc      = crc3_write(0, kAccessAddr, kAccessData);
//    const uint64_t ac_frame    = ((uint64_t)kAccessAddr  << 35) |
//                                  ((uint64_t)kAccessData  <<  3) |
//                                  ((uint64_t)ac_crc);

//    Serial.printf("[AUTH] Sending Access Code  frame=0x%011llX  CRC=%d\n", ac_frame, ac_crc);
//    manchester_tx_send(ac_frame, 44, /*start_mark=*/true, /*end_mark=*/true);

    // Wait 120 µs post-Access-Code settle before issuing any Read/Write command.
    //esp_rom_delay_us(120);

//    Serial.println("[AUTH] Port open — starting Read loop");

    // --- Build READ FAULT_STATUS frame (12-bit read request, static) ---
//    const uint8_t  rd_addr  = 0x20;   // FAULT_STATUS register
//    const uint8_t  rd_crc   = crc3_read_request(rd_addr);
//    const uint64_t rd_frame = ((uint64_t)1       << 9) |
//                               ((uint64_t)rd_addr << 3) |
//                               ((uint64_t)rd_crc);

//    Serial.printf("[PROG] READ FAULT_STATUS  frame=0x%03llX  CRC=%d\n", rd_frame, rd_crc);

                        uint8_t  TX_CRC = 0;
                        uint8_t  RX_CRC = 0; 
                        bool  ACC_INC = false;

    for (;;) {
//--------------------------------------------------------------------------------------------------------------        
                        // Enable 3.3V power supply 
                        digitalWrite(Config::PwrEn, LOW);
                        delay(20); // delay to stabilize 3.3V power rail

                        // --- Access Code (opens device serial port, mandatory after every power cycle) ---
                        // 44-bit write frame: SYNC=00 | R/W=0 | ADDR=0x31 | DATA=0x2C413736 | CRC[3]
                        const uint32_t kAccessData = 0x2C413737UL;
                        const uint8_t  kAccessAddr = 0x31;

                        if(ACC_INC) {
                            ACC_INC = false;
                            TX_CRC++;
                            if (TX_CRC > 7) {TX_CRC = 0;}
                        }

                        const uint8_t  ac_crc      = TX_CRC;
                        const uint64_t ac_frame    = ((uint64_t)kAccessAddr  << 35) |
                                                         ((uint64_t)kAccessData  <<  3) |
                                                         ((uint64_t)ac_crc);


                        Serial.printf("[AUTH] Sending Access Code  frame=0x%011llX  CRC=%d\n", ac_frame, ac_crc);
                        manchester_tx_send(ac_frame, 44, /*start_mark=*/true, /*end_mark=*/true);
                        esp_rom_delay_us(120);


                        // --- Build READ FAULT_STATUS frame (12-bit read request, static) ---
                        const uint8_t  rd_addr  = 0x20;   // FAULT_STATUS register
                        const uint8_t  rd_crc = RX_CRC;
                        const uint64_t rd_frame = ((uint64_t)1       << 9) |
                                                    ((uint64_t)rd_addr << 3) |
                                                    ((uint64_t)rd_crc);

                        Serial.printf("[PROG] READ FAULT_STATUS  frame=0x%03llX  CRC=%d\n", rd_frame, rd_crc);
                        manchester_tx_send(rd_frame, 12, /*start_mark=*/true, /*end_mark=*/false);

                        RX_CRC++;
                        if(RX_CRC > 7) {
                            RX_CRC = 0;
                            ACC_INC = true;
                        }

                        // Wait for respond from device
                        delay(20);

                        // Disable 3.3V power supply 
                        digitalWrite(Config::PwrEn, HIGH); // turn of 3.3V LDO
                        //delay(1000); //delay to set 3.3V rail to 0
//--------------------------------------------------------------------------------------------------------------                    
//        manchester_tx_send(rd_frame, 12, /*start_mark=*/true, /*end_mark=*/false);

        // Device responds within 74 µs; arm RMT RX and wait up to 100 ms.
        // Response frame: SYNC[2] | R/W[1] | ADDR[6] | DATA[32] | CRC[3] = 44 bits
    //    uint64_t response = 0;
    //    const uint8_t rx_bits = manchester_rx_receive(&response, 100);

    //    if (rx_bits == 44) {
    //        const uint8_t  rx_sync = (uint8_t)((response >> 42) & 0x3);
    //        const uint8_t  rx_rw   = (uint8_t)((response >> 41) & 0x1);
    //        const uint8_t  rx_addr = (uint8_t)((response >> 35) & 0x3F);
    //        const uint32_t rx_data = (uint32_t)((response >> 3)  & 0xFFFFFFFFUL);
    //        const uint8_t  rx_crc  = (uint8_t)(response & 0x7);
    //        Serial.printf("[RX]  SYNC=%d R/W=%d ADDR=0x%02X DATA=0x%08X CRC=%d\n",
    //                      rx_sync, rx_rw, rx_addr, rx_data, rx_crc);
    //    } else {
    //        Serial.printf("[RX]  timeout or decode error (%d bits)\n", rx_bits);
    //    }

        vTaskDelay(pdMS_TO_TICKS(5000));
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


