#include <Arduino.h>
#include <DHT.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "app_config.h"
#include "firebase_sync.h"
#include "system_state.h"

namespace {

constexpr uint8_t DHT_TYPE = DHT11;
constexpr uint8_t ANALOG_SAMPLE_COUNT = 10;

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
OneWire oneWire(AppConfig::DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DHT dht(AppConfig::DHT_PIN, DHT_TYPE);

SensorSnapshot sensors;
bool hasSensorSample = false;
bool ds18b20ConversionStarted = false;
bool pumpState = false;
const char* pumpReason = "STARTUP OFF";

uint32_t pumpStartedAt = 0;
uint32_t pumpStoppedAt = 0;
uint32_t lastSensorRead = 0;
uint32_t lastOLEDUpdate = 0;

int readAnalogAverage(uint8_t pin) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < ANALOG_SAMPLE_COUNT; ++i) {
    total += analogRead(pin);
    delayMicroseconds(250);
  }
  return total / ANALOG_SAMPLE_COUNT;
}

int mapConstrained(int value, int fromLow, int fromHigh, int toLow,
                   int toHigh) {
  const int result = map(value, fromLow, fromHigh, toLow, toHigh);
  return constrain(result, min(toLow, toHigh), max(toLow, toHigh));
}

bool ds18b20ReadingIsValid(float value) {
  return value != DEVICE_DISCONNECTED_C && isfinite(value) && value >= -55.0F &&
         value <= 125.0F;
}

bool dhtReadingIsValid(float temperature, float humidity) {
  return isfinite(temperature) && isfinite(humidity) && temperature >= -40.0F &&
         temperature <= 80.0F && humidity >= 0.0F && humidity <= 100.0F;
}

void pulseRelay(bool turnOn);

void writeRelay(bool turnOn) {
  if (turnOn) {
    // **PULSE MODE for ON**: Toggle relay to wake up stuck relays
    pulseRelay(true);
  } else {
    // **SIMPLE MODE for OFF**: Just set low
    const bool outputHigh =
        AppConfig::RELAY_ACTIVE_LOW ? !turnOn : turnOn;
    const int outValue = outputHigh ? HIGH : LOW;
    digitalWrite(AppConfig::RELAY_PIN, outValue);
    const int readBack = digitalRead(AppConfig::RELAY_PIN);
    Serial.printf("[Relay] pin=%d, requested=%s, output=%d, readBack=%d\n",
                  AppConfig::RELAY_PIN, "OFF", outValue, readBack);
  }
}

// Alternative: pulse the relay pin (for latching relays or stuck state recovery).
void pulseRelay(bool turnOn) {
  const bool outputHigh =
      AppConfig::RELAY_ACTIVE_LOW ? !turnOn : turnOn;
  const int outValue = outputHigh ? HIGH : LOW;
  const int offValue = outputHigh ? LOW : HIGH;
  
  digitalWrite(AppConfig::RELAY_PIN, outValue);
  Serial.printf("[Relay] pulse: set pin to %d\n", outValue);
  delayMicroseconds(500);  // Brief pulse
  digitalWrite(AppConfig::RELAY_PIN, offValue);
  Serial.printf("[Relay] pulse: set pin back to %d\n", offValue);
  delayMicroseconds(100);
  // Re-assert the desired level
  digitalWrite(AppConfig::RELAY_PIN, outValue);
  Serial.printf("[Relay] pulse: re-asserted to %d\n", outValue);
}

void setPump(bool turnOn, const char* reason) {
  pumpReason = reason;

  Serial.printf("[Pump] setPump requested: turnOn=%s, reason=%s, prev=%s\n",
                turnOn ? "YES" : "NO", reason, pumpState ? "ON" : "OFF");

  if (pumpState == turnOn) {
    // Reassert the relay level so a disturbed or missed control signal can
    // recover without changing the logical pump runtime state.
    writeRelay(turnOn);
    Serial.println("[Pump] state unchanged, relay level reasserted");
    return;
  }

  pumpState = turnOn;
  writeRelay(turnOn);

  FirebaseSync::publishStateNow(pumpState, pumpReason);

  if (turnOn) {
    pumpStartedAt = millis();
  } else {
    pumpStoppedAt = millis();
  }
}

void readAllSensors() {
  sensors.soilRaw = readAnalogAverage(AppConfig::SOIL_PIN);
  sensors.soilPercent =
      mapConstrained(sensors.soilRaw, AppConfig::SOIL_DRY_VALUE,
                     AppConfig::SOIL_WET_VALUE, 0, 100);
  if (AppConfig::FORCE_SOIL_PERCENT_FOR_TEST) {
    sensors.soilPercent = AppConfig::TEST_SOIL_PERCENT;
  }

  sensors.waterRaw = readAnalogAverage(AppConfig::WATER_PIN);
  sensors.waterPercent =
      mapConstrained(sensors.waterRaw, AppConfig::WATER_EMPTY_VALUE,
                     AppConfig::WATER_FULL_VALUE, 0, 100);

  sensors.rainRaw = readAnalogAverage(AppConfig::RAIN_PIN);
  sensors.rainPercent =
      mapConstrained(sensors.rainRaw, AppConfig::RAIN_DRY_VALUE,
                     AppConfig::RAIN_WET_VALUE, 0, 100);

  // Read the previous conversion, then start the next one without blocking.
  if (ds18b20ConversionStarted) {
    sensors.ds18b20TempC = ds18b20.getTempCByIndex(0);
    sensors.ds18b20Valid = ds18b20ReadingIsValid(sensors.ds18b20TempC);
  }
  ds18b20.requestTemperatures();
  ds18b20ConversionStarted = true;

  sensors.dhtTempC = dht.readTemperature();
  sensors.dhtHumidity = dht.readHumidity();
  sensors.dhtValid =
      dhtReadingIsValid(sensors.dhtTempC, sensors.dhtHumidity);
  sensors.sampledAtMs = millis();
  hasSensorSample = true;
}

void updatePumpLogic() {
  if (!hasSensorSample) {
    setPump(false, "NO SENSOR DATA");
    return;
  }

  const bool manualOverrideActive = FirebaseSync::manualMode() &&
                                    FirebaseSync::manualPumpRequested();

  // In manual mode with pump request, bypass ALL safety checks and turn on pump.
  if (FirebaseSync::manualMode()) {
    if (!FirebaseSync::manualPumpRequested()) {
      setPump(false, "MANUAL OFF");
    } else {
      // Manual request is ON: bypass all safety checks (water, soil, rain, sensors).
      // Pump runs freely until user turns off the request (no timeout).
      setPump(true, pumpState ? "MANUAL PUMPING" : "MANUAL ON");
    }
    return;
  }

  // Auto mode logic below (all safety checks apply here).
  const int effectiveSoilPercent = sensors.soilPercent;
  const bool soilIsDry = effectiveSoilPercent > 0 &&
                         effectiveSoilPercent < AppConfig::SOIL_PUMP_ON_PERCENT;
  const bool soilIsWetEnough =
      effectiveSoilPercent > AppConfig::SOIL_PUMP_OFF_PERCENT;
  const bool waterIsEnough =
      sensors.waterPercent > AppConfig::MIN_WATER_PERCENT;
  const bool rainDetected =
      sensors.rainPercent >= AppConfig::RAIN_DETECTED_PERCENT;
  const bool pumpTimeout =
      AppConfig::ENABLE_PUMP_RUNTIME_LIMIT && pumpState &&
      millis() - pumpStartedAt >= AppConfig::MAX_PUMP_RUNTIME_MS;
  const bool cooldownDone =
      millis() - pumpStoppedAt >= AppConfig::PUMP_COOLDOWN_MS;

  if (!waterIsEnough) {
    setPump(false, "LOW WATER");
    return;
  }
  if (effectiveSoilPercent <= 0) {
    setPump(false, "SOIL ZERO");
    return;
  }
  if (rainDetected) {
    setPump(false, "RAINING");
    return;
  }
  if (!sensors.ds18b20Valid) {
    setPump(false, "DS18B20 ERROR");
    return;
  }
  if (pumpTimeout) {
    setPump(false, "MAX RUNTIME");
    return;
  }

  if (!AppConfig::ENABLE_AUTO_PUMP) {
    setPump(false, "AUTO DISABLED");
    return;
  }

  if (!pumpState) {
    if (soilIsDry && cooldownDone) {
      setPump(true, "SOIL DRY");
    } else if (soilIsDry) {
      setPump(false, "COOLDOWN");
    } else {
      setPump(false, "SOIL OK");
    }
    return;
  }

  if (soilIsWetEnough) {
    setPump(false, "SOIL WET");
  } else {
    setPump(true, "PUMPING");
  }
}

void printDebug() {
  Serial.println("========== SMART IRRIGATION ==========");
  Serial.printf("Soil:  raw=%d, moisture=%d%%\n", sensors.soilRaw,
                sensors.soilPercent);
  Serial.printf("Water: raw=%d, level=%d%%\n", sensors.waterRaw,
                sensors.waterPercent);
  Serial.printf("Rain:  raw=%d, level=%d%%\n", sensors.rainRaw,
                sensors.rainPercent);

  if (sensors.ds18b20Valid) {
    Serial.printf("DS18B20: %.1f C\n", sensors.ds18b20TempC);
  } else {
    Serial.println("DS18B20: ERROR");
  }

  if (sensors.dhtValid) {
    Serial.printf("DHT11: %.1f C, %.1f%%\n", sensors.dhtTempC,
                  sensors.dhtHumidity);
  } else {
    Serial.println("DHT11: ERROR");
  }

  Serial.printf("Pump: %s, reason=%s\n", pumpState ? "ON" : "OFF",
                pumpReason);
  const bool soilForcedActive = AppConfig::FORCE_SOIL_PERCENT_FOR_TEST;
  const bool runtimeLimitActive = AppConfig::ENABLE_PUMP_RUNTIME_LIMIT;
  Serial.printf("Pump test: soil forced=%s (%d%%), runtime limit=%s\n",
                soilForcedActive ? "YES" : "NO",
                soilForcedActive ? AppConfig::TEST_SOIL_PERCENT : sensors.soilPercent,
                runtimeLimitActive ? "ON" : "OFF");
  Serial.printf("Control: %s, manual request=%s\n",
                FirebaseSync::controlModeName(),
                FirebaseSync::manualPumpRequested() ? "ON" : "OFF");
  Serial.printf("Firebase: %s\n\n", FirebaseSync::ready() ? "READY" : "OFFLINE");
}

void drawHeader(const char* title) {
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 10, title);
  display.drawLine(0, 13, 128, 13);
}

void drawSummaryPage() {
  display.clearBuffer();
  drawHeader("System Summary");
  display.setCursor(0, 25);
  display.printf("S:%d%% W:%d%%", sensors.soilPercent, sensors.waterPercent);
  display.setCursor(0, 40);
  display.printf("R:%d%% P:%s", sensors.rainPercent,
                 pumpState ? "ON" : "OFF");
  display.setCursor(0, 57);
  if (sensors.dhtValid) {
    display.printf("Air %.1fC %.0f%%", sensors.dhtTempC, sensors.dhtHumidity);
  } else {
    display.print("DHT11 Error");
  }
  display.sendBuffer();
}

void drawSoilPage() {
  display.clearBuffer();
  drawHeader("Soil Moisture");
  display.setCursor(0, 30);
  display.printf("Raw: %d", sensors.soilRaw);
  display.setCursor(0, 45);
  display.printf("Moisture: %d%%", sensors.soilPercent);
  display.setCursor(0, 60);
  display.print(sensors.soilPercent < AppConfig::SOIL_PUMP_ON_PERCENT
                    ? "Status: DRY"
                    : "Status: OK");
  display.sendBuffer();
}

void drawWaterPage() {
  display.clearBuffer();
  drawHeader("Water Level");
  display.setCursor(0, 30);
  display.printf("Raw: %d", sensors.waterRaw);
  display.setCursor(0, 45);
  display.printf("Level: %d%%", sensors.waterPercent);
  display.setCursor(0, 60);
  display.print(sensors.waterPercent <= AppConfig::MIN_WATER_PERCENT
                    ? "Status: LOW"
                    : "Status: OK");
  display.sendBuffer();
}

void drawRainPage() {
  display.clearBuffer();
  drawHeader("Rain Sensor");
  display.setCursor(0, 30);
  display.printf("Raw: %d", sensors.rainRaw);
  display.setCursor(0, 45);
  display.printf("Rain: %d%%", sensors.rainPercent);
  display.setCursor(0, 60);
  display.print(sensors.rainPercent >= AppConfig::RAIN_DETECTED_PERCENT
                    ? "Status: RAIN"
                    : "Status: DRY");
  display.sendBuffer();
}

void drawTemperaturePage() {
  display.clearBuffer();
  drawHeader("Temperature");
  display.setCursor(0, 30);
  if (sensors.ds18b20Valid) {
    display.printf("DS18B20: %.1fC", sensors.ds18b20TempC);
  } else {
    display.print("DS18B20: ERR");
  }
  display.setCursor(0, 48);
  if (sensors.dhtValid) {
    display.printf("DHT11: %.1fC", sensors.dhtTempC);
  } else {
    display.print("DHT11: ERR");
  }
  display.sendBuffer();
}

void drawHumidityPage() {
  display.clearBuffer();
  drawHeader("Air Humidity");
  display.setCursor(0, 35);
  if (sensors.dhtValid) {
    display.printf("Humidity: %.1f%%", sensors.dhtHumidity);
  } else {
    display.print("DHT11 Error");
  }
  display.sendBuffer();
}

void drawPumpPage() {
  display.clearBuffer();
  drawHeader("Pump / Relay");
  display.setCursor(0, 28);
  display.printf("Pump: %s", pumpState ? "ON" : "OFF");
  display.setCursor(0, 43);
  display.print("Reason:");
  display.setCursor(0, 58);
  display.print(pumpReason);
  display.sendBuffer();
}

void updateOLED() {
  const uint32_t page =
      (millis() / AppConfig::OLED_PAGE_INTERVAL_MS) % 7;

  switch (page) {
    case 0:
      drawSummaryPage();
      break;
    case 1:
      drawSoilPage();
      break;
    case 2:
      drawWaterPage();
      break;
    case 3:
      drawRainPage();
      break;
    case 4:
      drawTemperaturePage();
      break;
    case 5:
      drawHumidityPage();
      break;
    default:
      drawPumpPage();
      break;
  }
}

void initializeRelaySafely() {
  pinMode(AppConfig::RELAY_PIN, OUTPUT);
  writeRelay(false);
  pumpState = false;
  pumpReason = "STARTUP OFF";
  pumpStoppedAt = millis();
}

}  // namespace

void setup() {
  initializeRelaySafely();

  Serial.begin(115200);
  Serial.println("\nSmart Irrigation Controller");
  if (AppConfig::FORCE_SOIL_PERCENT_FOR_TEST ||
      !AppConfig::ENABLE_PUMP_RUNTIME_LIMIT) {
    Serial.println(
        "WARNING: PUMP TEST MODE ACTIVE - SOIL IS FORCED AND TIMEOUT IS OFF");
  }

  Wire.begin(AppConfig::OLED_SDA, AppConfig::OLED_SCL);
  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 18, "Smart Irrigation");
  display.drawStr(0, 35, "Controller v2");
  display.drawStr(0, 52, "Starting...");
  display.sendBuffer();

  analogReadResolution(12);
  analogSetPinAttenuation(AppConfig::SOIL_PIN, ADC_11db);
  analogSetPinAttenuation(AppConfig::WATER_PIN, ADC_11db);
  analogSetPinAttenuation(AppConfig::RAIN_PIN, ADC_11db);

  ds18b20.begin();
  ds18b20.setWaitForConversion(false);
  ds18b20.requestTemperatures();
  ds18b20ConversionStarted = true;
  dht.begin();

  Serial.printf("DS18B20 devices found: %d\n", ds18b20.getDeviceCount());
  FirebaseSync::begin();
}

void loop() {
  const uint32_t now = millis();

  // **FAST PATH**: Check manual pump mode every iteration (no 2s delay!)
  // This allows instant response to dashboard "Bật bơm" button
  if (hasSensorSample && FirebaseSync::manualMode()) {
    const bool manualPumpRequested = FirebaseSync::manualPumpRequested();
    if (manualPumpRequested && !pumpState) {
      setPump(true, "MANUAL ON");
    } else if (!manualPumpRequested && pumpState) {
      setPump(false, "MANUAL OFF");
    } else if (manualPumpRequested) {
      // Manual request is ON: pump runs immediately, bypass all safety
      // Keep the relay level as-is without spamming no-op state changes.
    }
  }

  // **SLOW PATH**: Sensor read & auto mode (every 2 seconds)
  if (now - lastSensorRead >= AppConfig::SENSOR_INTERVAL_MS) {
    lastSensorRead = now;
    readAllSensors();
    // Only run auto mode logic if NOT in manual mode
    if (!FirebaseSync::manualMode()) {
      updatePumpLogic();
    }
    printDebug();
  }

  if (now - lastOLEDUpdate >= AppConfig::OLED_REFRESH_MS) {
    lastOLEDUpdate = now;
    updateOLED();
  }

  FirebaseSync::loop(sensors, pumpState, pumpReason);
}
