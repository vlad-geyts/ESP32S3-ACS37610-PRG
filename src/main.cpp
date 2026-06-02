#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "unused_gpio.h"

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


