# Final Project Report: Smart Irrigation System using ESP32 and Firebase

**Team:** Chill Out  
**Course:** Final Project - Internet of Things  
**Institution:** VNUK Institute for Research and Executive Education - The University of Danang  
**Academic Year:** 2025-2026

## Team Members

| Name | Student ID | Role |
| --- | --- | --- |
| Le Tiep Tuyen | 22020015 | Team Lead & Developer |
| Mai Thieu Tin | 22020003 | Developer |
| Doan Hong Ngoc | 22020010 | Developer |

## Abstract

This project presents an IoT-based smart irrigation and plant monitoring system built with an ESP32 microcontroller, environmental sensors, a relay-controlled water pump, Firebase Realtime Database, and a responsive web dashboard. The system measures soil moisture, water level, rain intensity, soil temperature, air temperature, and air humidity. It supports automatic irrigation based on configured thresholds and manual control through the dashboard. The prototype demonstrates a complete IoT workflow from physical sensing and local actuation to cloud synchronization and remote user interaction.

## 1. Introduction

Manual plant watering is often inconsistent because users may not know the actual soil condition or surrounding environmental values. Over-watering, under-watering, and delayed response can affect plant health and waste water. An IoT-based irrigation system can address this problem by continuously collecting sensor data, uploading it to a cloud database, and allowing automated or remote pump control.

The implemented system focuses on a small-scale smart irrigation prototype suitable for an IoT final project. It combines embedded firmware, sensor data acquisition, relay-based actuation, Firebase cloud synchronization, and a browser-based dashboard.

## 2. Project Objectives

The main objective is to design and implement a working IoT irrigation prototype that can monitor plant-related environmental conditions and control a water pump locally or remotely.

Specific objectives:

- Read soil moisture, water level, rain intensity, DS18B20 temperature, DHT11 air temperature, and DHT11 air humidity.
- Convert raw analog values into percentage-based sensor readings where applicable.
- Display local status through an OLED screen.
- Control a water pump through a relay module.
- Upload telemetry and device state to Firebase Realtime Database.
- Provide a web dashboard for real-time monitoring and manual control.
- Implement automatic pump logic with safety conditions such as low water, rain detection, invalid DS18B20 reading, runtime limit, and cooldown.

## 3. System Architecture

The system follows a four-layer IoT architecture:

1. **Sensing and Actuation Layer:** sensors, relay module, and water pump.
2. **Edge Device Layer:** ESP32 firmware for sensor reading, control logic, OLED display, Wi-Fi, and Firebase synchronization.
3. **Cloud Data Layer:** Firebase Realtime Database for telemetry, state, configuration, and control commands.
4. **Application Layer:** responsive web dashboard for monitoring and pump control.

![System architecture diagram](diagrams/rendered/system-architecture.png)

_Source: `docs/diagrams/system-architecture.mmd`._

## 4. Hardware Components and Pin Mapping

| Component | ESP32 Pin | Purpose |
| --- | --- | --- |
| Soil moisture sensor | GPIO34 | Analog soil moisture reading |
| Water level sensor | GPIO35 | Analog water level reading |
| Rain sensor | GPIO32 | Analog rain intensity reading |
| DS18B20 | GPIO19 | Temperature reading |
| DHT11 | GPIO4 | Air temperature and humidity |
| Relay module | GPIO26 | Pump control |
| OLED SDA | GPIO21 | I2C data line |
| OLED SCL | GPIO22 | I2C clock line |

The relay is used to isolate and control the DC water pump. The pump should be powered from a suitable external power source instead of being powered directly from the ESP32.

**Figure 2. Circuit wiring map based on the current firmware pin configuration.**

![Circuit wiring map](diagrams/rendered/circuit-wiring-map.png)

_Source: `docs/diagrams/circuit-wiring-map.mmd`._

## 5. Software Stack

| Layer | Technology |
| --- | --- |
| Firmware framework | Arduino framework on PlatformIO |
| Microcontroller platform | ESP32 Dev Board |
| Display library | U8g2 |
| Temperature libraries | OneWire, DallasTemperature, DHT sensor library |
| Cloud library | FirebaseClient |
| Cloud database | Firebase Realtime Database |
| Dashboard | HTML, CSS, JavaScript |
| Hosting configuration | Firebase Hosting |

## 6. Firmware Design

The firmware is organized around configuration, sensor state, pump control, OLED display updates, and Firebase synchronization.

Main firmware files:

- `include/app_config.h`: device ID, firmware version, pin mapping, calibration values, thresholds, and scheduling intervals.
- `include/system_state.h`: `SensorSnapshot` structure used to store the latest sensor sample.
- `src/main.cpp`: sensor reading, pump logic, OLED rendering, relay handling, and main loop scheduling.
- `src/firebase_sync.cpp`: Wi-Fi connection, Firebase initialization, telemetry upload, state upload, metadata upload, and control command reading.

The main loop uses non-blocking timing based on `millis()`:

- Sensor reading interval: `2000 ms`.
- OLED refresh interval: `500 ms`.
- Firebase latest telemetry/state upload: `2000 ms`.
- Firebase history upload: `60000 ms`.
- Firebase control command read interval: `700 ms`.
- Wi-Fi reconnect interval: `10000 ms`.

## 7. Sensor Processing

The ESP32 reads analog sensors by averaging 10 samples for each analog input. This reduces short-term noise before mapping raw values to percentage values.

Implemented sensor processing:

- Soil moisture raw value is mapped to `0-100%` using dry and wet calibration values.
- Water level raw value is mapped to `0-100%` using empty and full calibration values.
- Rain raw value is mapped to `0-100%` using dry and wet calibration values.
- DS18B20 temperature is validated against the sensor's expected range and disconnection value.
- DHT11 temperature and humidity are validated using reasonable temperature and humidity ranges.

The firmware stores all sensor readings in a `SensorSnapshot` structure before using them for automation, OLED display, and Firebase upload.

## 8. Pump Control Logic

The project supports two control modes: automatic mode and manual mode.

### 8.1 Automatic Mode

In automatic mode, the ESP32 controls the pump based on the latest sensor values and configured thresholds.

The pump can turn ON when:

- Soil moisture is greater than `0%` and below `30%`.
- Water level is greater than `20%`.
- Rain intensity is below `30%`.
- DS18B20 reading is valid.
- Pump cooldown has completed.
- Automatic pump control is enabled.

The pump turns OFF when:

- Soil moisture is above `45%`.
- Water level is too low.
- Rain is detected.
- DS18B20 reading is invalid.
- Soil moisture is `0%`.
- Pump runtime reaches `30000 ms`.
- Automatic pump control is disabled.

The cooldown time after pump shutdown is `5000 ms`.

### 8.2 Manual Mode

In manual mode, the dashboard writes `mode: "manual"` and `manualPumpOn` to Firebase. The ESP32 reads these values and follows the manual pump request. In the current firmware implementation, manual mode bypasses the automatic irrigation checks, so it must be used carefully during demonstrations and testing.

![Manual control flow diagram](diagrams/rendered/manual-control-flow.png)

_Source: `docs/diagrams/manual-control-flow.mmd`._

## 9. Firebase Realtime Database Design

The main Firebase path is:

```text
smart_irrigation/devices/esp32-irrigation-01
```

| Node | Description |
| --- | --- |
| `metadata/runtime` | Firmware version, board name, and boot time metadata |
| `config` | Effective calibration, thresholds, upload intervals, and test mode settings |
| `control` | Dashboard commands, including `mode` and `manualPumpOn` |
| `state` | Online status, pump status, RSSI, control mode, and last seen time |
| `telemetry/latest` | Latest sensor and pump data |
| `telemetry/history` | Historical telemetry samples pushed by Firebase push IDs |

The firmware builds JSON payloads for telemetry, state, runtime metadata, and effective configuration. It uploads the latest values regularly and pushes history samples at a slower interval.

**Figure 4. Firebase Realtime Database structure used by the prototype.**

![Firebase Realtime Database structure](screenshots/firebase-realtime-database-structure.png)

_Source: `data.json` and the configured Firebase Realtime Database path._

## 10. Web Dashboard Design

The dashboard is implemented as a static web application in the `web` directory.

Main dashboard functions:

- Poll Firebase for device data every `700 ms`.
- Display online/offline status based on `state.lastSeen`.
- Display soil moisture, water level, rain intensity, air temperature, air humidity, and DS18B20 temperature.
- Display pump status and pump reason.
- Show a recent trend chart for soil, water, and rain values.
- Switch between automatic and manual mode.
- Send manual pump commands by patching the Firebase `control` node.
- Use retry logic when sending control commands.

**Figure 5. Web dashboard overview with sensor cards and pump state.**

![Web dashboard overview](screenshots/web-dashboard-overview.png)

**Figure 6. Web dashboard control panel and recent trend chart.**

![Web dashboard controls and trend chart](screenshots/web-dashboard-controls-trend.png)

## 11. Testing Plan

The following test cases are appropriate for validating the prototype:

| Test Case | Scenario | Expected Behavior |
| --- | --- | --- |
| TC-01 | ESP32 starts with valid Wi-Fi credentials | Device attempts Wi-Fi connection and starts Firebase synchronization |
| TC-02 | Soil sensor is read | Raw value and percentage are updated |
| TC-03 | Water sensor is read | Water percentage is updated |
| TC-04 | Rain sensor is read | Rain percentage is updated |
| TC-05 | DHT11 is valid | Air temperature and humidity are uploaded |
| TC-06 | DS18B20 is valid | Temperature is uploaded and auto mode can continue |
| TC-07 | Water level is below threshold in auto mode | Pump remains OFF with `LOW WATER` reason |
| TC-08 | Rain value reaches threshold in auto mode | Pump remains OFF with `RAINING` reason |
| TC-09 | Soil moisture is dry in auto mode and safety conditions pass | Pump turns ON with `SOIL DRY` reason |
| TC-10 | Soil moisture becomes wet enough | Pump turns OFF with `SOIL WET` reason |
| TC-11 | Manual pump command is sent from dashboard | ESP32 follows the dashboard command |
| TC-12 | Firebase connection is temporarily unavailable | Local pump logic continues to operate based on firmware state |

These test cases define the intended validation scenarios. They should be executed on the physical prototype before the final submission.

## 12. Limitations

The current implementation is a prototype for course demonstration and has the following limitations:

- Firebase rules are configured for public read/write access for demonstration convenience.
- The dashboard control messages contain some Vietnamese text that should be reviewed for encoding consistency before public release.
- Manual mode currently follows the pump command directly and bypasses automatic safety checks.
- Calibration values may need adjustment for the actual sensors, soil type, and water tank.
- The system does not include authentication, alert notifications, predictive irrigation, or multi-device management.

## 13. Future Improvements

Potential improvements include:

- Add Firebase Authentication and stricter database rules.
- Add dashboard-based threshold editing if the firmware is extended to read dynamic threshold values.
- Add notification alerts for low water, sensor errors, or long offline periods.
- Add historical analytics for daily or weekly irrigation behavior.
- Add support for multiple ESP32 devices or multiple plant zones.
- Improve manual mode with optional safety constraints.

## 14. Conclusion

The project successfully integrates embedded sensing, local control, cloud synchronization, and web-based monitoring into a complete IoT smart irrigation prototype. The ESP32 collects environmental data, evaluates pump control logic, drives the relay-controlled pump, displays local status on OLED, and synchronizes telemetry and state to Firebase. The web dashboard provides real-time monitoring and remote control. Overall, the system demonstrates the key concepts of an IoT application: sensing, connectivity, cloud data exchange, actuation, automation, and user interaction.
