#define ENABLE_DATABASE

#include "firebase_sync.h"

#include <FirebaseClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "app_config.h"
#include "secrets.h"

namespace {

WiFiClientSecure sslClient;
AsyncClientClass firebaseClient(sslClient);
NoAuth noAuth;
FirebaseApp firebaseApp;
RealtimeDatabase database;

bool firebaseInitialized = false;
bool metadataUploaded = false;
bool configurationValid = false;
uint32_t lastWifiAttempt = 0;
uint32_t lastLatestUpload = 0;
uint32_t lastHistoryUpload = 0;
uint32_t lastControlRead = 0;
String deviceRoot;
bool controlManualMode = false;
bool controlManualPumpOn = false;

bool containsPlaceholder(const char* value) {
  return value == nullptr || value[0] == '\0' ||
         std::strstr(value, "YOUR_") != nullptr;
}

void processFirebaseResult(AsyncResult& result) {
  if (!result.isResult()) {
    return;
  }

  if (result.isError()) {
    Firebase.printf("[Firebase] %s: %s (%d)\n", result.uid().c_str(),
                    result.error().message().c_str(), result.error().code());
  }
}

void processControlResult(AsyncResult& result) {
  processFirebaseResult(result);
  if (!result.available()) {
    return;
  }

  const String uid = result.uid();
  const String payload = result.c_str();

  if (uid == "control-mode") {
    controlManualMode = payload == "\"manual\"";
    return;
  }

  if (uid == "control-pump") {
    controlManualPumpOn = payload == "true";
  }
}

void appendBool(String& json, bool value) {
  json += value ? "true" : "false";
}

void appendFloatOrNull(String& json, float value, bool valid,
                       unsigned int decimals = 1) {
  if (valid && isfinite(value)) {
    json += String(value, decimals);
  } else {
    json += "null";
  }
}

String buildTelemetryPayload(const SensorSnapshot& sensors, bool pumpOn,
                             const char* pumpReason) {
  String json;
  json.reserve(700);

  json += "{\"timestamp\":{\".sv\":\"timestamp\"},\"uptimeMs\":";
  json += sensors.sampledAtMs;

  json += ",\"soil\":{\"raw\":";
  json += sensors.soilRaw;
  json += ",\"percent\":";
  json += sensors.soilPercent;
  json += "},\"water\":{\"raw\":";
  json += sensors.waterRaw;
  json += ",\"percent\":";
  json += sensors.waterPercent;
  json += "},\"rain\":{\"raw\":";
  json += sensors.rainRaw;
  json += ",\"percent\":";
  json += sensors.rainPercent;

  json += "},\"temperature\":{\"ds18b20\":{\"celsius\":";
  appendFloatOrNull(json, sensors.ds18b20TempC, sensors.ds18b20Valid);
  json += ",\"valid\":";
  appendBool(json, sensors.ds18b20Valid);
  json += "},\"dht11\":{\"celsius\":";
  appendFloatOrNull(json, sensors.dhtTempC, sensors.dhtValid);
  json += ",\"valid\":";
  appendBool(json, sensors.dhtValid);

  json += "}},\"humidity\":{\"dht11\":{\"percent\":";
  appendFloatOrNull(json, sensors.dhtHumidity, sensors.dhtValid);
  json += ",\"valid\":";
  appendBool(json, sensors.dhtValid);

  json += "}},\"pump\":{\"on\":";
  appendBool(json, pumpOn);
  json += ",\"reason\":\"";
  json += pumpReason;
  json += "\"}}";

  return json;
}

String buildStatePayload(bool pumpOn, const char* pumpReason) {
  String json;
  json.reserve(240);
  json += "{\"online\":true,\"lastSeen\":{\".sv\":\"timestamp\"},";
  json += "\"uptimeMs\":";
  json += millis();
  json += ",\"wifiRssi\":";
  json += WiFi.RSSI();
  json += ",\"pumpOn\":";
  appendBool(json, pumpOn);
  json += ",\"pumpReason\":\"";
  json += pumpReason;
  json += "\",\"autoPumpEnabled\":";
  appendBool(json, AppConfig::ENABLE_AUTO_PUMP && !controlManualMode);
  json += ",\"controlMode\":\"";
  json += controlManualMode ? "manual" : "auto";
  json += "\",\"manualPumpRequested\":";
  appendBool(json, controlManualPumpOn);
  json += "}";
  return json;
}

String buildRuntimeMetadataPayload() {
  String json;
  json.reserve(180);
  json += "{\"firmwareVersion\":\"";
  json += AppConfig::FIRMWARE_VERSION;
  json += "\",\"board\":\"ESP32\",\"bootedAt\":{\".sv\":\"timestamp\"}}";
  return json;
}

String buildEffectiveConfigPayload() {
  String json;
  json.reserve(600);

  json += "{\"autoPumpEnabled\":";
  appendBool(json, AppConfig::ENABLE_AUTO_PUMP);
  json += ",\"calibration\":{\"soil\":{\"dryRaw\":";
  json += AppConfig::SOIL_DRY_VALUE;
  json += ",\"wetRaw\":";
  json += AppConfig::SOIL_WET_VALUE;
  json += "},\"water\":{\"emptyRaw\":";
  json += AppConfig::WATER_EMPTY_VALUE;
  json += ",\"fullRaw\":";
  json += AppConfig::WATER_FULL_VALUE;
  json += "},\"rain\":{\"dryRaw\":";
  json += AppConfig::RAIN_DRY_VALUE;
  json += ",\"wetRaw\":";
  json += AppConfig::RAIN_WET_VALUE;

  json += "}},\"thresholds\":{\"soilPumpOnPercent\":";
  json += AppConfig::SOIL_PUMP_ON_PERCENT;
  json += ",\"soilPumpOffPercent\":";
  json += AppConfig::SOIL_PUMP_OFF_PERCENT;
  json += ",\"minimumWaterPercent\":";
  json += AppConfig::MIN_WATER_PERCENT;
  json += ",\"rainDetectedPercent\":";
  json += AppConfig::RAIN_DETECTED_PERCENT;
  json += ",\"maximumPumpRuntimeMs\":";
  json += AppConfig::ENABLE_PUMP_RUNTIME_LIMIT
              ? AppConfig::MAX_PUMP_RUNTIME_MS
              : 0;
  json += ",\"pumpCooldownMs\":";
  json += AppConfig::PUMP_COOLDOWN_MS;

  json += "},\"uploadIntervals\":{\"latestMs\":";
  json += AppConfig::FIREBASE_LATEST_INTERVAL_MS;
  json += ",\"historyMs\":";
  json += AppConfig::FIREBASE_HISTORY_INTERVAL_MS;
  json += "},\"testMode\":{\"forceSoilPercent\":";
  appendBool(json, AppConfig::FORCE_SOIL_PERCENT_FOR_TEST);
  json += ",\"soilPercent\":";
  json += AppConfig::TEST_SOIL_PERCENT;
  json += ",\"pumpRuntimeLimitEnabled\":";
  appendBool(json, AppConfig::ENABLE_PUMP_RUNTIME_LIMIT);
  json += "}}";
  return json;
}

void startFirebase() {
  if (firebaseInitialized || WiFi.status() != WL_CONNECTED) {
    return;
  }

  sslClient.setInsecure();
  sslClient.setHandshakeTimeout(5);

  initializeApp(firebaseClient, firebaseApp, getAuth(noAuth),
                processFirebaseResult, "no-auth");
  firebaseApp.getApp<RealtimeDatabase>(database);
  database.url(FIREBASE_DATABASE_URL);
  firebaseInitialized = true;
  Serial.println("[Firebase] Public RTDB connection started");
  Serial.printf("[WiFi] Connected, IP=%s, RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void maintainWifi() {
  if (!configurationValid || WiFi.status() == WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastWifiAttempt < AppConfig::WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastWifiAttempt = now;
  const int status = WiFi.status();
  auto wifiStatusName = [](int s) -> const char* {
    switch (s) {
      case WL_IDLE_STATUS:
        return "WL_IDLE_STATUS";
      case WL_NO_SSID_AVAIL:
        return "WL_NO_SSID_AVAIL";
      case WL_SCAN_COMPLETED:
        return "WL_SCAN_COMPLETED";
      case WL_CONNECTED:
        return "WL_CONNECTED";
      case WL_CONNECT_FAILED:
        return "WL_CONNECT_FAILED";
      case WL_CONNECTION_LOST:
        return "WL_CONNECTION_LOST";
      case WL_DISCONNECTED:
        return "WL_DISCONNECTED";
      default:
        return "WL_UNKNOWN";
    }
  };

  Serial.printf("[WiFi] Reconnecting... status=%d (%s)\n", status,
                wifiStatusName(status));
  // Try reconnect and also call begin again in case automatic reconnect isn't working.
  WiFi.reconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // If SSID not available, do a quick scan to list visible networks (blocking, for debug).
  static uint32_t lastScan = 0;
  if (status == WL_NO_SSID_AVAIL && millis() - lastScan > 30000) {
    lastScan = millis();
    Serial.println("[WiFi] SSID not available - scanning for nearby networks...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
      Serial.println("[WiFi] No networks found");
    } else {
      Serial.printf("[WiFi] %d networks found:\n", n);
      for (int i = 0; i < n; ++i) {
        Serial.printf("  %d: %s (RSSI=%d)\n", i + 1,
                      WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      }
    }
    WiFi.scanDelete();
  }
}

void uploadLatest(const SensorSnapshot& sensors, bool pumpOn,
                  const char* pumpReason) {
  const String telemetry = buildTelemetryPayload(sensors, pumpOn, pumpReason);
  const String state = buildStatePayload(pumpOn, pumpReason);

  database.set<object_t>(firebaseClient, deviceRoot + "/telemetry/latest",
                         object_t(telemetry), processFirebaseResult, "latest");
  database.set<object_t>(firebaseClient, deviceRoot + "/state", object_t(state),
                         processFirebaseResult, "state");
}

void uploadHistory(const SensorSnapshot& sensors, bool pumpOn,
                   const char* pumpReason) {
  const String telemetry = buildTelemetryPayload(sensors, pumpOn, pumpReason);
  database.push<object_t>(firebaseClient, deviceRoot + "/telemetry/history",
                          object_t(telemetry), processFirebaseResult, "history");
}

void readControl() {
  database.get(firebaseClient, deviceRoot + "/control/mode",
               processControlResult, false, "control-mode");
  database.get(firebaseClient, deviceRoot + "/control/manualPumpOn",
               processControlResult, false, "control-pump");
}

}  // namespace

namespace FirebaseSync {

void begin() {
  configurationValid =
      !containsPlaceholder(WIFI_SSID) && !containsPlaceholder(WIFI_PASSWORD) &&
      !containsPlaceholder(FIREBASE_DATABASE_URL);

  if (!configurationValid) {
    Serial.println(
        "[Firebase] Disabled: replace placeholder values in include/secrets.h");
    return;
  }

  deviceRoot = String("/smart_irrigation/devices/") + AppConfig::DEVICE_ID;
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  Serial.println("[WiFi] Connecting...");
}

void loop(const SensorSnapshot& sensors, bool pumpOn, const char* pumpReason) {
  maintainWifi();
  startFirebase();

  if (!firebaseInitialized) {
    return;
  }

  firebaseApp.loop();
  if (!firebaseApp.ready() || sensors.sampledAtMs == 0) {
    return;
  }

  if (!metadataUploaded) {
    const String metadata = buildRuntimeMetadataPayload();
    const String config = buildEffectiveConfigPayload();
    database.set<object_t>(firebaseClient, deviceRoot + "/metadata/runtime",
                           object_t(metadata), processFirebaseResult,
                           "metadata");
    database.set<object_t>(firebaseClient, deviceRoot + "/config",
                           object_t(config), processFirebaseResult, "config");
    metadataUploaded = true;
  }

  const uint32_t now = millis();
  if (now - lastLatestUpload >= AppConfig::FIREBASE_LATEST_INTERVAL_MS) {
    lastLatestUpload = now;
    uploadLatest(sensors, pumpOn, pumpReason);
  }

  if (now - lastHistoryUpload >= AppConfig::FIREBASE_HISTORY_INTERVAL_MS) {
    lastHistoryUpload = now;
    uploadHistory(sensors, pumpOn, pumpReason);
  }

  if (now - lastControlRead >= AppConfig::FIREBASE_CONTROL_INTERVAL_MS) {
    lastControlRead = now;
    readControl();
  }
}

bool ready() {
  return configurationValid && WiFi.status() == WL_CONNECTED &&
         firebaseInitialized && firebaseApp.ready();
}

bool manualMode() {
  return controlManualMode;
}

bool manualPumpRequested() {
  return controlManualPumpOn;
}

const char* controlModeName() {
  return controlManualMode ? "manual" : "auto";
}

}  // namespace FirebaseSync
