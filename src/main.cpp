#include <Arduino.h>
#include <math.h>
#include <Preferences.h> // Include the NVS wrapper
#include <string>         // C++ Standard String library
#include <string_view>    // C++17 header for high-performance string handling
                          // Just to data members: a pointer and a length. 
                          // Does not created a copy in memory. Read-only.
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include "unused_gpio.h"

// WS2812 Configuration
#define WS2812_PIN      48
#define NUM_LEDS        1
#define LED_BRIGHTNESS  8  // 0-255 (adjust to your preference)
#define LED_TYPE        (NEO_GRB + NEO_KHZ800)

//C++: Namespaces & Constexpr --- 
namespace Config {

    // === Debug signals ===
    constexpr int StrobPin  = 21;

    // === HMI Navigation Buttons ===
    constexpr int BtnUp    = 4;
    constexpr int BtnDown  = 5;
    constexpr int BtnEnter = 6;
    constexpr int BtnPanic = 47;

    // === ADC onfiguration ===
    constexpr int BAT_ADC_PIN = 7;  // ADC1_CH6
    constexpr int REF_ADC_PIN = 8;  // ADC1_CH7
    constexpr int OFFSET_ADC_PIN = 9;  // ADC1_CH8 -> Tied to GND

    // Precision reference voltage (adjust if your source differs slightly)
    constexpr float REF_VOLTAGE = 2.50f;

    // Voltage divider ratio: R_bottom / (R_top + R_bottom)
    // R_top=120k, R_bottom=75k -> 10.5/(32.4+10.5) = 0.244755
    constexpr float DIVIDER_RATIO = 0.244755f; 

    // 2cells LiPo boundaries (adjust to your chemistry/protection cutoff)
    constexpr float BAT_FULL_V   = 8.40f; // 4.20V * 2
    constexpr float BAT_CUTOFF_V = 6.60f; // 3.30V * 2 (safe discharge limit)

    // === OLED SPI Pins (FSPI Hardware Primary) ===
    constexpr int OLED_CS   = 10;
    constexpr int OLED_MOSI = 11;
    constexpr int OLED_SCLK = 12;
    constexpr int OLED_DC   = 13;
    constexpr int OLED_RST  = 14;

    constexpr int ScreenWidth  = 128;
    constexpr int ScreenHeight = 128;
    
     // === RGB Led ===
    constexpr int LedPin = WS2812_PIN;

     // ===Color definitions ===
    constexpr int TFT_WHITE = 0xFFFF;
    constexpr int TFT_BLUE = 0x001F;
    constexpr int TFT_GREEN = 0x07E0;
    constexpr int TFT_CYAN = 0x07FF;
    constexpr int TFT_MAGENTA = 0x07FF;
    constexpr int TFT_RED = 0xF800;
    constexpr int TFT_YELLOW = 0xFFE0;
    constexpr int TFT_PINK = 0xFE19;
    constexpr int TFT_ORANGE = 0xFDA0;
    constexpr int TFT_BLACK = 0x0000;

  // ================= CALIBRATION & FILTERING =================
    float adc_offset_counts = 0.0f;
    float adc_gain_v_per_count = 0.0f;
    bool is_calibrated = false;
    float bat_voltage_filtered = 0.0f;

    constexpr int CAL_SAMPLES = 500;   // More samples for stable offset/gain
    constexpr int READ_SAMPLES = 32;
}

// Global Objects
Preferences prefs;
SemaphoreHandle_t panicSemaphore;
QueueHandle_t displayQueue;
Adafruit_SSD1351 tft = Adafruit_SSD1351(Config::ScreenWidth, Config::ScreenHeight, &SPI, Config::OLED_CS, Config::OLED_DC, Config::OLED_RST);
Adafruit_NeoPixel ws2812(NUM_LEDS, WS2812_PIN, LED_TYPE);

int LineNumber = 0;
char MsgBuf[30];        // ! only 21 characters per line can be displayed on OLED

struct DisplayMsg {
    char text[32];
    uint16_t color;
};

// Function Prototypes
void IRAM_ATTR handleButtonInterrupt();
void panicTask(void *pvParameters);
void heartbeatTask(void *pvParameters);
//void socTask(void *pvParameters);
void initOLED();
void gpioConfig();
void rdPanicCounter();
void displayTask(void* pvParameters);
void logStatus(const char*, uint16_t);
void initADC();
void calibrateADC();
float readBatteryVoltage();

void setup() {
    // Delay to warm up 
    delay(1000);
    Serial.begin(115200);
    delay(3000);

     Serial.println(".................................");

    //Terminate unused GPIOs EARLY (before peripheral init)
    ConfigureUnusedGpios();

    // Configure Hardware using our Namespace
    gpioConfig();

    //Initialioze OLED on start up
    initOLED();
    
    // Initialize ADC
    initADC();

    // Create a Queue for Display only 1 Msg
    displayQueue = xQueueCreate(1, sizeof(DisplayMsg));

    // Standard Heartbeat (Priority: 0) on Core 0
    xTaskCreatePinnedToCore(heartbeatTask, "Heartbeat", 4096, NULL, 0, NULL, 0);

    // Create Display Task (Priority: 1) on Core 0
    xTaskCreatePinnedToCore(displayTask, "OLED_Task", 4096, NULL, 1, NULL, 0); 

    // Creet Battery Monitoring Task (Prioriy: 0) on Core 1
    //xTaskCreatePinnedToCore(heartbeatTask, "SOC", 4096, NULL, 0, NULL, 1);

    // Calibrate ADC
    Serial.println("Running 2-point ADC calibration (Offset + Gain)...");
    calibrateADC();

    if (Config::is_calibrated) {
    Serial.println("✓ Calibration successful. Monitoring battery...");
    } else {
    Serial.println("✗ Calibration failed! Check 2.5V reference connection.");
    while(1) delay(1000); // Halt until fixed
    }

     // Get battery voltage
    float Vbat = readBatteryVoltage();

    // convert message to string and save it to buffer
    // Limted to 20 characters per line @ small font
    sprintf(MsgBuf, "Vbat=%.3f", Vbat);        
    // Send message string from buffer to OLED display
    logStatus(MsgBuf, Config::TFT_CYAN);
}

void loop() {
    // Arduino task is no longer needed
    vTaskDelete(NULL);
}

void initOLED() {
    // Initialize SPI with our custom pins
    SPI.begin(Config::OLED_SCLK, -1, Config::OLED_MOSI, Config::OLED_CS);
    // Reaerve firts 6 lines (0 to 5) for graphics
    //LineNumber = 0;
    LineNumber = 6;

    // Display layout
    // Y from 0 to 63 reserved for graphics
    // Y from 64 to 127 reserved for terminal. 
    // 6 lines (11 pix hight) using Small size 5x7. 4 pix separation between lines

    // Initialize OLED display
    tft.begin(20000000); // Force SPI clik=20MHz;  20MHz is max)
    tft.fillScreen(0x0000); // Clear to black
    tft.setTextSize(1); // 1-Small size; 2-M; 3-Large; 4-XL
    //  Small size 5x7 [6x8] (21 charaters per line)
    //  Medium size 10x14 [12x16] (10 charters per line)
    tft.setCursor(15, LineNumber * 11);
    tft.setTextColor(Config::TFT_YELLOW); // Yellow color 
    tft.print("S3 MONITOR ACTIVE");    
    LineNumber++;
}

void gpioConfig() {
   // Configure Hardware using our Namespace
    pinMode(Config::LedPin, OUTPUT);
    pinMode(Config::BtnPanic, INPUT_PULLUP);
    pinMode(Config::StrobPin, OUTPUT);
    digitalWrite(Config::StrobPin, LOW);
}

 void logStatus(const char* info, uint16_t color = 0xFFFF) {
    //Serial.println(info);
    
    DisplayMsg msg;
    // Safe string copy with guaranteed null-termination
    strncpy(msg.text, info, sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';  // Adding null-termination
    msg.color = color;
    
    // Block briefly if queue is full (10ms max), drop otherwise
    if (xQueueSend(displayQueue, &msg, pdMS_TO_TICKS(10)) != pdPASS) {
        Serial.println(F("[WARN] Display queue full, message dropped"));
    }
}

void initADC() {
  // Configure ADC resolution & attenuation
  analogReadResolution(12); // 12-bit (0-4095)
  
  // Set attenuation to ~0-3.3V range
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    analogSetAttenuation(ADC_ATTEN_DB_11); // Arduino-ESP32 v3.x
  #else
    analogSetPinAttenuation(Config::BAT_ADC_PIN, ADC_11db); // Arduino-ESP32 v2.x
    analogSetPinAttenuation(Config::REF_ADC_PIN, ADC_11db);
    analogSetPinAttenuation(Config::OFFSET_ADC_PIN, ADC_11db);
  #endif
}

void calibrateADC() {
uint32_t offset_sum = 0;
  for (int i = 0; i < Config::CAL_SAMPLES; i++) offset_sum += analogRead(Config::OFFSET_ADC_PIN);
 //   Serial.printf("Offset sum @ 500 samples = : %d \n", offset_sum);

  uint32_t ref_sum = 0;
  for (int i = 0; i < Config::CAL_SAMPLES; i++) ref_sum += analogRead(Config::REF_ADC_PIN);
 //   Serial.printf("Ref sum @ 500 samples = : %d \n", ref_sum);

  float offset_avg = offset_sum / (float)Config::CAL_SAMPLES;
  float ref_avg    = ref_sum / (float)Config::CAL_SAMPLES;

  // Sanity check: DC offset schould be <= 150 counts
  if (offset_avg > 150) {
    Serial.printf("⚠ Offset too high: %.1f counts. Check GND connection.\n", offset_avg);
    return;
  }

// Sanity check: 2.5V at 11dB atten should read ~3000-3200 on 12-bit ADC
  if (ref_avg < 2900 || ref_avg > 3100) {
    Serial.printf("⚠ Ref ADC out of range: %.1f (expected ~3000)\n", ref_avg);
    return;
  }

 // 2-point linear calibration: V = (Raw - Offset) * Gain
  Config::adc_offset_counts = offset_avg;
  Config::adc_gain_v_per_count = Config::REF_VOLTAGE / (ref_avg - offset_avg);
  Config::is_calibrated = true;

  Serial.printf("Offset: %.6f | Ref Raw: %.3f | Gain: %.9f V/count\n", 
                Config::adc_offset_counts, ref_avg, Config::adc_gain_v_per_count);
  Serial.printf("is_calibrated =: %b \n", Config::is_calibrated);              
}

// For debuuging only
float readBatteryVoltage() {

}

// --- Core 0 Tasks ---
void displayTask(void* pvParameters) {
    DisplayMsg msg;
    while (true) {  
        if (xQueueReceive(displayQueue, &msg, portMAX_DELAY) == pdPASS) {
            // xQueueReceive(...): The FreeRTOS API function that attempts to read and remove an item from a queue.
            // displayQueue: The handle to the queue being read.
            // &msg: A pointer to the buffer where the received data will be copied
            // portMAX_DELAY: The timeout value. Because this is set, the task will block (enter the Blocked state) and wait forever for data to become available
            // Alternatives: If you want to check the queue and continue without waiting, use a timeout of 0 (polling) instead of portMAX_DELAY
            // == pdPASS: The check to confirm that data was received successfully (it returns pdTRUE/pdPASS if data was received, otherwise errQUEUE_EMPTY if a timeout occurred). 
            if(LineNumber > 11) { 
                tft.fillScreen(0x0000); 
                // Reserve first 6 lines (0 to 5) for graphics
                //LineNumber = 0; 
                LineNumber = 5; 
            }
            tft.setCursor(0, LineNumber * 11);
            tft.setTextColor(msg.color);
            tft.println(msg.text);
            LineNumber++;
            // Optional: prevent I2C/SPI bus saturation
            vTaskDelay(pdMS_TO_TICKS(5)); 
        }    
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

    //  Serial.printf("[Core 0] Normal Heartbeat... (Uptime: %lu s)\n", millis() / 1000);
        vTaskDelay(pdMS_TO_TICKS(1000));  
    }
}

// --- Core 1 Tasks ---
//void socTask(void *pvParameters) {
//
 //   vTaskDelay(pdMS_TO_TICKS(1000));
//}



