// unused_gpio.h
#pragma once
#include <Arduino.h>
#include <array>

// ESP32-S3 WROOM-1 valid GPIO range: 0-48.
// GPIO_PIN_COUNT = 49. Any pin >= 49 will trigger HAL errors.

// C++17: constexpr std::array with aggregate initialization
// Excludes: Used pins (2,4,5,6,10-14,21,47,48), Strapping (0,3,45,46), 
// USB (19,20), Internal Flash (26-32), PSRAM (33-37), UART0 (43,44)
constexpr std::array<int, 8> kUnusedGpios = {
    1, 15, 16, 17, 18, 38, 39, 40
};

// Compile-time safety check: ensures no pin exceeds hardware limit
static_assert(kUnusedGpios.back() < 49, "Error: Pin number exceeds ESP32-S3 WROOM limit (max 48)");

// C++17: inline + noexcept for zero-overhead runtime init
inline void ConfigureUnusedGpios() noexcept {
    for (const auto& pin : kUnusedGpios) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
}