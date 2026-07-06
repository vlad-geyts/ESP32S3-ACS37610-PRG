#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "unused_gpio.h"
#include "manchester.h"
#include "acs37610_cmd.h"
#include "cmd_parser.h"

#define FW_VERSION "1.0.0"

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

    // Manchester TX (bit-bang) and RX (RMT_CHANNEL_4) — 30 kbps, T=33 µs
    manchester_tx_init(33);
    manchester_rx_init(33);

    // DUT rail stays OFF at boot; the host enables it with PWRON (GUI plan §3).
    acs_init(Config::PwrEn);

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
    digitalWrite(Config::PwrEn, HIGH); // 3.3V LDO off — DUT unpowered until PWRON
}

// Programmer task — Core 1, priority 5.
// ASCII command loop (GUI plan §3/§4): reads '\n'-terminated lines from
// Serial, dispatches through cmd_parser, answers exactly one line per command.
// All device sequencing (PWRON → AUTH → READ/WRAM/WEEP) is host-driven.
void programmerTask(void *pvParameters) {
    static const CmdHandlers handlers = {
        acs_power,
        acs_power_state,
        acs_port_open,
        acs_auth,
        acs_read,
        acs_write_ram,
        acs_write_eeprom,
    };
    cmd_parser_init(&handlers, FW_VERSION);

    char   line[96];
    size_t len      = 0;
    bool   overflow = false;
    char   resp[128];

    for (;;) {
        while (Serial.available() > 0) {
            const char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (overflow) {
                    Serial.println("ERR ARG");
                } else if (len > 0) {
                    line[len] = '\0';
                    cmd_parser_process(line, resp, sizeof(resp));
                    Serial.println(resp);
                }
                len = 0;
                overflow = false;
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            } else {
                overflow = true;   // line too long — answer ERR ARG at terminator
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
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


