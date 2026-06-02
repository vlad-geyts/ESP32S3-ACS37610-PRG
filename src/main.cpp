#include <Arduino.h>
//#include <math.h>
//#include <Preferences.h> // Include the NVS wrapper
//#include <string>         // C++ Standard String library
//#include <string_view>    // C++17 header for high-performance string handling
                          // Just to data members: a pointer and a length. 
                          // Does not created a copy in memory. Read-only.
//#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1351.h>
#include <Adafruit_NeoPixel.h>
//#include <SPI.h>
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

    // === HMI Navigation Buttons ===
  //  constexpr int BtnUp    = 4;
  //  constexpr int BtnDown  = 5;
  //  constexpr int BtnEnter = 6;
  //  constexpr int BtnPanic = 47;

    // === ADC onfiguration ===
  //  constexpr int BAT_ADC_PIN = 7;  // ADC1_CH6
  //  constexpr int REF_ADC_PIN = 8;  // ADC1_CH7
  //  constexpr int OFFSET_ADC_PIN = 9;  // ADC1_CH8 -> Tied to GND

    // Precision reference voltage (adjust if your source differs slightly)
  //  constexpr float REF_VOLTAGE = 2.111f;

    // Voltage divider ratio: R_bottom / (R_top + R_bottom)
  //  constexpr float DIVIDER_RATIO = 0.273154281f; 

    // 2cells LiPo boundaries (adjust to your chemistry/protection cutoff)
  //  constexpr float BAT_FULL_V   = 8.40f; // 4.20V * 2
   // constexpr float BAT_CUTOFF_V = 6.60f; // 3.30V * 2 (safe discharge limit)

    // === OLED SPI Pins (FSPI Hardware Primary) ===
  //  constexpr int OLED_CS   = 10;
  //  constexpr int OLED_MOSI = 11;
  //  constexpr int OLED_SCLK = 12;
  //  constexpr int OLED_DC   = 13;
  //  constexpr int OLED_RST  = 14;

  //  constexpr int ScreenWidth  = 128;
  //  constexpr int ScreenHeight = 128;
    
     // === RGB Led ===
    constexpr int LedPin = WS2812_PIN;

     // ===Color definitions ===
  //  constexpr int TFT_WHITE = 0xFFFF;
  //  constexpr int TFT_BLUE = 0x001F;
  //  constexpr int TFT_GREEN = 0x07E0;
  //  constexpr int TFT_CYAN = 0x07FF;
  //  constexpr int TFT_MAGENTA = 0x07FF;
  //  constexpr int TFT_RED = 0xF800;
  //  constexpr int TFT_YELLOW = 0xFFE0;
  //  constexpr int TFT_PINK = 0xFE19;
  //  constexpr int TFT_ORANGE = 0xFDA0;
  //  constexpr int TFT_BLACK = 0x0000;

  // ================= CALIBRATION & FILTERING =================
  //  float adc_offset_counts = 0.0f;
  //  float adc_gain_v_per_count = 0.0f;
  //  bool is_calibrated = false;
  //  float bat_voltage_filtered = 0.0f;
  //  constexpr float alpha = 0.15;

  //  constexpr int CAL_SAMPLES = 500;   // More samples for stable offset/gain
  //  constexpr int READ_SAMPLES = 32; //8192 - 1sec;   // initial was 32
  //  constexpr int SEAD_INTERRATIONS =64;
}

// Global Objects
//Preferences prefs;
//SemaphoreHandle_t panicSemaphore;
//QueueHandle_t displayQueue;
//Adafruit_SSD1351 tft = Adafruit_SSD1351(Config::ScreenWidth, Config::ScreenHeight, &SPI, Config::OLED_CS, Config::OLED_DC, Config::OLED_RST);
Adafruit_NeoPixel ws2812(NUM_LEDS, WS2812_PIN, LED_TYPE);

//int LineNumber = 0;
//char MsgBuf[30];        // ! only 21 characters per line can be displayed on OLED

//struct DisplayMsg {
//    char text[32];
//    uint16_t color;
//};

// Function Prototypes
//void IRAM_ATTR handleButtonInterrupt();
//void panicTask(void *pvParameters);
void heartbeatTask(void *pvParameters);
//void socTask(void *pvParameters);
//void initOLED();
void gpioConfig();
//void rdPanicCounter();
//void displayTask(void* pvParameters);
//void logStatus(const char*, uint16_t);
//void initADC();
//void calibrateADC();
//float readBatteryVoltage();

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


