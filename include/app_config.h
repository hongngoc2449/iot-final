#pragma once

#include <Arduino.h>

namespace AppConfig {

// Device identity
constexpr char DEVICE_ID[] = "esp32-irrigation-01";
constexpr char FIRMWARE_VERSION[] = "2.2.3";

// Pins
constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t WATER_PIN = 35;
constexpr uint8_t RAIN_PIN = 32;
constexpr uint8_t DS18B20_PIN = 19;
constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t RELAY_PIN = 26;
constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;

// Sensor calibration
constexpr int SOIL_DRY_VALUE = 4095;
constexpr int SOIL_WET_VALUE = 300;
constexpr int WATER_EMPTY_VALUE = 0;
constexpr int WATER_FULL_VALUE = 2500;
constexpr int RAIN_DRY_VALUE = 4095;
constexpr int RAIN_WET_VALUE = 800;

// Pump safety and control
constexpr int SOIL_PUMP_ON_PERCENT = 30;
constexpr int SOIL_PUMP_OFF_PERCENT = 45;
constexpr int MIN_WATER_PERCENT = 20;
constexpr int RAIN_DETECTED_PERCENT = 30;
constexpr uint32_t MAX_PUMP_RUNTIME_MS = 30000;
constexpr uint32_t PUMP_COOLDOWN_MS = 5000;
constexpr bool RELAY_ACTIVE_LOW = false;
constexpr bool ENABLE_AUTO_PUMP = true;

// Temporary pump wiring test. Restore both values after testing.
constexpr bool FORCE_SOIL_PERCENT_FOR_TEST = false;
constexpr int TEST_SOIL_PERCENT = 10;
constexpr bool ENABLE_PUMP_RUNTIME_LIMIT = true;

// Scheduling
constexpr uint32_t SENSOR_INTERVAL_MS = 2000;
constexpr uint32_t OLED_REFRESH_MS = 500;
constexpr uint32_t OLED_PAGE_INTERVAL_MS = 3000;
constexpr uint32_t FIREBASE_LATEST_INTERVAL_MS = SENSOR_INTERVAL_MS;
constexpr uint32_t FIREBASE_HISTORY_INTERVAL_MS = 60000;
constexpr uint32_t FIREBASE_CONTROL_INTERVAL_MS = 700;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;

}  // namespace AppConfig
