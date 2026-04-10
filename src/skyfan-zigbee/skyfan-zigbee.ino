
/*
 * Skyfan Zigbee Controller - Zigbee 3.0 controller for Ventair Skyfan ceiling fans with integrated lighting
 * Copyright (C) 2025 Rhys Frederick at Front Left Speaker
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include "SkyfanConfig.h"
#include "TuyaProtocol.h"
#include "PersistedProperties.h"
#include "SkyfanZigbee.h"
#include "SkyfanZigbeeLight.h"
#include "LedIndicator.h"
#include "ButtonHandler.h"
#include "Logger.h"
#include <HardwareSerial.h>

#ifdef RGB_BUILTIN
uint8_t led = RGB_BUILTIN;
#else
uint8_t led = 2;
#endif

DebouncedButton factoryResetButton(FACTORY_RESET_BUTTON_PIN);
LedStatusIndicator statusLed(led);

// Hardware UART for Tuya MCU communication
HardwareSerial& tuyaSerial = Serial0;

SkyfanZigbeeFanControl zbFanControl = SkyfanZigbeeFanControl(ZIGBEE_FAN_CONTROL_ENDPOINT);
#ifdef WITH_LIGHT
SkyfanZigbeeLight zbLight = SkyfanZigbeeLight(ZIGBEE_LIGHT_CONTROL_ENDPOINT);
#endif
TuyaProtocol tuya(&tuyaSerial);
PersistedProperties props;

// OTA state tracking
volatile bool otaRunning = false;

/********************* fan control callback functions **************************/
void setFan(ZigbeeFanMode mode) {
  Log::debug("Write Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
             ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, (uint32_t)mode);
  statusLed.flashCommand();

  // Rollback callback - TuyaProtocol handles re-entry guard and MCU responding check
  auto rollback = []() { zbFanControl.rollbackFanMode(); };

  switch (mode) {
    case FAN_MODE_OFF:
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 0, rollback);
      Log::info("Fan mode set to OFF (0) by Zigbee");
      break;
    case FAN_MODE_LOW:
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, rollback, false);  // Untracked but has rollback
      tuya.queueCommand(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_LOW_TUYA, rollback);
      Log::info("Fan mode set to LOW (1) by Zigbee");
      break;
    case FAN_MODE_MEDIUM:
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, rollback, false);  // Untracked but has rollback
      tuya.queueCommand(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_MEDIUM_TUYA, rollback);
      Log::info("Fan mode set to MEDIUM (3) by Zigbee");
      break;
    case FAN_MODE_HIGH:
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, rollback, false);  // Untracked but has rollback
      tuya.queueCommand(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_HIGH_TUYA, rollback);
      Log::info("Fan mode set to HIGH (5) by Zigbee");
      break;
    case FAN_MODE_ON:
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, rollback);
      Log::info("Fan mode set to ON (4) by Zigbee");
      break;
    default:
      Log::error("Unhandled fan mode: %d", mode);
      return;
  }
}

// Fan direction control callback function
void setFanDirection(uint8_t direction) {
  Log::debug("Write Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
             ZIGBEE_FAN_CONTROL_ENDPOINT, VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, (uint32_t)direction);
  statusLed.flashCommand();

  // Rollback callback - TuyaProtocol handles re-entry guard and MCU responding check
  auto rollback = []() { zbFanControl.rollbackFanDirection(); };
  tuya.queueCommand(DP_FAN_DIRECTION, DP_TYPE_ENUM, direction, rollback);

  Log::info("Fan direction set to %s (%d) by Zigbee",
    (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE", direction);
}

/********************* light control callback functions **************************/
#ifdef WITH_LIGHT
static bool lightCallbackInitialized = false;  // Skip first callback from initialization

void setLight(bool on, uint8_t level, uint16_t colourTempMired) {
  // Skip the first callback triggered by initialisation
  // (setLightColorTemperature() call in setup triggers this with stale values)
  if (!lightCallbackInitialized) {
    lightCallbackInitialized = true;
    Log::debug("setLight() skipped - first init callback");
    return;
  }

  // Suppress callback when MCU is reporting status to avoid echo
  if (zbLight.isCallbackSuppressed()) return;

  Log::debug("Write Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
             ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, on ? 1UL : 0UL);
  Log::debug("Write Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
             ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, (uint32_t)level);
  Log::debug("Write Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
             ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, (uint32_t)colourTempMired);
  statusLed.flashCommand();

  // Rollback callback - TuyaProtocol handles re-entry guard and MCU responding check
  auto rollback = []() { zbLight.rollback(); };

  // Queue switch command with rollback
  tuya.queueCommand(DP_LIGHT_SWITCH, DP_TYPE_BOOL, on ? 1 : 0, rollback);

  if (on) {
    // Convert Zigbee brightness (0-254) to Tuya brightness (0-5)
    uint8_t tuyaBrightness = zigbeeBrightnessToTuya(level);
    // Convert mired to Tuya colour temp values
    ColourTempLevel tuyaColourTemp = miredToTuyaColourTemp(colourTempMired);

    // Queue brightness and colour temp without tracking (switch tracks the operation)
    tuya.queueCommand(DP_LIGHT_DIMMER, DP_TYPE_VALUE, tuyaBrightness, nullptr, false);
    tuya.queueCommand(DP_LIGHT_COLOUR_TEMP, DP_TYPE_ENUM, static_cast<uint8_t>(tuyaColourTemp), nullptr, false);
  }

  Log::info("Light set to %s, level %d, temp %d mired (%dK) by Zigbee",
    on ? "ON" : "OFF", level, colourTempMired, miredToKelvin(colourTempMired));
}
#endif

/********************* individual device status handlers **************************/

// Handle fan switch status updates from MCU
void handleFanSwitchStatus(uint32_t value) {
  bool fanOn = (value != 0);
  ZigbeeFanMode mode = fanOn ? FAN_MODE_ON : FAN_MODE_OFF;

  if (!zbFanControl.setFanState(fanOn)) {
    Log::error("Failed to update Zigbee fan switch status: %s", fanOn ? "ON" : "OFF");
  } else {
    Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
               ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, (uint32_t)mode);
  }

  // Update confirmed state in Zigbee class
  zbFanControl.confirmFanMode(mode);

  Log::info("Fan switch set to %s (%d) by Skyfan", fanOn ? "ON" : "OFF", fanOn ? 1 : 0);
}

// Handle fan speed status updates from MCU
void handleFanSpeedStatus(uint32_t value) {
  uint8_t speed = static_cast<uint8_t>(value);
  if (isValidTuyaFanSpeed(speed)) {
    // Calculate the Zigbee fan mode based on speed
    ZigbeeFanMode mode;
    switch (speed) {
      case TUYA_FAN_SPEED_MIN: mode = FAN_MODE_OFF; break;
      case FAN_SPEED_LOW_TUYA:
      case FAN_SPEED_LOW_TUYA + 1: mode = FAN_MODE_LOW; break;
      case FAN_SPEED_MEDIUM_TUYA:
      case FAN_SPEED_MEDIUM_TUYA + 1: mode = FAN_MODE_MEDIUM; break;
      case FAN_SPEED_HIGH_TUYA: mode = FAN_MODE_HIGH; break;
      default: mode = FAN_MODE_ON; break;
    }

    if (!zbFanControl.setFanSpeed(speed)) {
      Log::error("Failed to update Zigbee fan speed status: %d", speed);
    } else {
      Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                 ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, (uint32_t)mode);
    }

    // Update confirmed state in Zigbee class
    zbFanControl.confirmFanMode(mode);

    Log::info("Fan speed set to %d by Skyfan", speed);
  } else {
    Log::error("Invalid fan speed status received: %d", speed);
  }
}

// Handle fan mode status updates from MCU (MCU-only, not exposed to Zigbee)
void handleFanModeStatus(uint32_t value) {
  uint8_t mode = static_cast<uint8_t>(value);
  if (mode <= static_cast<uint8_t>(TuyaFanMode::SLEEP)) {
    const char* modeName = (mode == static_cast<uint8_t>(TuyaFanMode::NORMAL)) ? "NORMAL" :
                           (mode == static_cast<uint8_t>(TuyaFanMode::ECO)) ? "ECO" : "SLEEP";
    Log::info("Fan MCU mode set to %s (%d) by Skyfan", modeName, mode);
  } else {
    Log::error("Invalid fan mode status received: %d", mode);
  }
}

// Handle fan direction status updates from MCU
void handleFanDirectionStatus(uint32_t value) {
  uint8_t direction = static_cast<uint8_t>(value);
  if (direction <= static_cast<uint8_t>(FanDirection::REVERSE)) {
    // Update custom manufacturer attribute for fan direction
    if (!zbFanControl.setFanDirection(direction)) {
      Log::error("Failed to update Zigbee fan direction status: %d", direction);
    } else {
      Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                 ZIGBEE_FAN_CONTROL_ENDPOINT, VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, (uint32_t)direction);
    }

    // Update confirmed state in Zigbee class
    zbFanControl.confirmFanDirection(direction);

    Log::info("Fan direction set to %s (%d) by Skyfan",
      (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE", direction);
  } else {
    Log::error("Invalid fan direction status received: %d", direction);
  }
}

#ifdef WITH_LIGHT
// Handle light switch status updates from MCU
void handleLightSwitchStatus(uint32_t value) {
  bool lightOn = (value != 0);
  if (!zbLight.setLightStateDirect(lightOn)) {
    Log::error("Failed to update Zigbee light switch status: %s", lightOn ? "ON" : "OFF");
  } else {
    Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
               ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, lightOn ? 1UL : 0UL);
  }

  // Update confirmed state in Zigbee class
  zbLight.confirmLightState(lightOn);

  Log::info("Light switch set to %s (%d) by Skyfan", lightOn ? "ON" : "OFF", lightOn ? 1 : 0);
}

// Handle light brightness status updates from MCU
void handleLightBrightnessStatus(uint32_t value) {
  uint8_t tuyaBrightness = static_cast<uint8_t>(value);

  if (isValidTuyaBrightness(tuyaBrightness)) {
    uint8_t zigbeeBrightness = tuyaBrightnessToZigbee(tuyaBrightness);
    if (!zbLight.setLightLevelDirect(zigbeeBrightness)) {
      Log::error("Failed to update Zigbee light brightness: %d", zigbeeBrightness);
    } else {
      Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                 ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, (uint32_t)zigbeeBrightness);
    }

    // Update confirmed state in Zigbee class
    zbLight.confirmLightLevel(zigbeeBrightness);

    Log::info("Light brightness set to %d (Zigbee: %d) by Skyfan", tuyaBrightness, zigbeeBrightness);
  } else {
    Log::error("Invalid light brightness status received: %d", tuyaBrightness);
  }
}

// Handle light colour temperature status updates from MCU
void handleLightColourTempStatus(uint32_t value) {
  uint8_t colourTempValue = static_cast<uint8_t>(value);

  if (colourTempValue <= static_cast<uint8_t>(ColourTempLevel::COOL)) {
    ColourTempLevel colourLevel = static_cast<ColourTempLevel>(colourTempValue);
    uint16_t colourTempMired = tuyaColourTempToMired(colourLevel);

    if (!zbLight.setLightColorTemperatureDirect(colourTempMired)) {
      Log::error("Failed to update Zigbee light colour temperature: %d mired", colourTempMired);
    } else {
      Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                 ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, (uint32_t)colourTempMired);
    }

    // Update confirmed state in Zigbee class
    zbLight.confirmColorTemp(colourTempMired);
    Log::info("Light colour temp set to %d mired (%dK) by Skyfan", colourTempMired, miredToKelvin(colourTempMired));
  } else {
    Log::error("Invalid light colour temperature status received: %d", colourTempValue);
  }
}
#endif

// Handle unknown/unsupported status updates from MCU
void handleUnknownStatus(uint8_t dpid, uint32_t value) {
  Log::error("Unknown status update - DPID: %d, Value: %lu from Skyfan", dpid, value);
}

/********************* main device status callback function **************************/
void onDeviceStatus(uint8_t dpid, uint32_t value) {
  switch (dpid) {
    case DP_FAN_SWITCH:
      handleFanSwitchStatus(value);
      break;
      
    case DP_FAN_SPEED:
      handleFanSpeedStatus(value);
      break;
      
    case DP_FAN_MODE:
      handleFanModeStatus(value);
      break;
      
    case DP_FAN_DIRECTION:
      handleFanDirectionStatus(value);
      break;
      
#ifdef WITH_LIGHT
    case DP_LIGHT_SWITCH:
      handleLightSwitchStatus(value);
      break;
      
    case DP_LIGHT_DIMMER:
      handleLightBrightnessStatus(value);
      break;
      
    case DP_LIGHT_COLOUR_TEMP:
      handleLightColourTempStatus(value);
      break;
#endif
      
    default:
      handleUnknownStatus(dpid, value);
      break;
  }
}

/********************* OTA callback functions **************************/
void otaStateCallback(bool otaActive) {
  otaRunning = otaActive;
  if (otaActive) {
    Log::info("OTA update started - do not power off device");
    statusLed.setStatus(LedStatus::INITIALISING);  // Use blinking LED during OTA
  } else {
    Log::info("OTA update finished - device will reboot");
  }
}

/********************* Arduino functions **************************/
void setup() {
  Log::begin(DEBUG_SERIAL_BAUD_RATE);  // USB Serial for debug output
  Log::info("Skyfan Zigbee Controller starting...");

  // Load persisted properties from NVS
  props.begin();

  // Connect to MCU - use persisted baud rate if available, otherwise negotiate
  uint32_t storedRate = props.getMcuBaudRate();
  if (storedRate > 0) {
    Log::info("Using persisted baud rate: %lu", storedRate);
    tuya.connect(storedRate);
  } else {
    int32_t result = tuya.connect();
    if (result > 0) {
      props.setMcuBaudRate(result);
      Log::info("Persisted negotiated baud rate: %d", result);
    }
  }

#ifdef __BOOT_LOG__
  if (Log::getBootLog()[0] != '\0') {
    props.writeBootLog(Log::getBootLog());
    Log::clearBootLog();
  }
#endif

  tuya.setDeviceStatusCallback(onDeviceStatus);

  // Set Zigbee device name and model
  zbFanControl.setManufacturerAndModel(ZIGBEE_DEVICE_MANUFACTURER, ZIGBEE_MODEL_NAME);

  // Configure device power source as mains-powered (not battery)
  zbFanControl.setPowerSource(ZB_POWER_SOURCE_MAINS);
  Log::debug("Device configured as mains-powered");

#ifdef WITH_LIGHT
  // Configure light colour capabilities to support colour temperature
  zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
  
  // Set colour temperature range (154-333 mired = 6500K-3000K)
  zbLight.setLightColorTemperatureRange(ZIGBEE_COLOUR_TEMP_MIN_MIRED, ZIGBEE_COLOUR_TEMP_MAX_MIRED);
#endif

  // Set the fan mode sequence to LOW_MED_HIGH
  zbFanControl.setFanModeSequence(FAN_MODE_SEQUENCE_LOW_MED_HIGH);

  // Set callback functions for fan and light control
  zbFanControl.onFanModeChange(setFan);
  zbFanControl.onFanDirectionChange(setFanDirection);
#ifdef WITH_LIGHT
  zbLight.onLightChangeTemp(setLight);
#endif

  // Create custom cluster BEFORE endpoint registration
  if (!zbFanControl.createCustomCluster()) {
    Log::error("Failed to create custom cluster - rebooting");
    cleanupAllResources();
    ESP.restart();
  }

  // Add OTA client to fan endpoint for over-the-air firmware updates
  if (zbFanControl.addOTAClient(OTA_FILE_VERSION, OTA_DOWNLOADED_FILE_VERSION, OTA_HW_VERSION,
                                 OTA_MANUFACTURER_CODE, OTA_IMAGE_TYPE)) {
    Log::debug("OTA client added (version: 0x%08lX, manufacturer: 0x%04X, image type: 0x%04X)",
               (unsigned long)OTA_FILE_VERSION, OTA_MANUFACTURER_CODE, OTA_IMAGE_TYPE);
    zbFanControl.onOTAStateChange(otaStateCallback);
  } else {
    Log::error("Failed to add OTA client - OTA updates will not be available");
  }

  // Add endpoints to Zigbee Core
  Log::debug("Adding ZigbeeFanControl endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbFanControl);
#ifdef WITH_LIGHT
  Log::debug("Adding ZigbeeLight endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbLight);
#endif

  // When all EPs are registered, start Zigbee in ROUTER mode
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Log::error("Zigbee failed to start - rebooting");
    cleanupAllResources();
    ESP.restart();
  }

  Log::info("Connecting to network (hold BOOT 3s to factory reset)");
  while (!Zigbee.connected()) {
    Log::raw(".");
    delay(ZIGBEE_CONNECTION_POLL_MS);

    // Check for factory reset during connection attempt
    factoryResetButton.update();
    if (factoryResetButton.wasLongPressed()) {
      Serial.println();
      Log::info("Factory reset requested during connection");
      cleanupAllResources();
      delay(FACTORY_RESET_DELAY_MS);
      Zigbee.factoryReset();
    }
  }
  Serial.println();
  Log::info("Zigbee connected successfully");

#ifdef WITH_LIGHT
  // Initialize color temperature to set color mode to TEMPERATURE
  // This ensures the temp callback is called for on/off and level changes
  zbLight.setLightColorTemperature(COLOUR_TEMP_WARM_MIRED);
#endif

  // Start OTA client query - first request is within a minute, then hourly automatically
  zbFanControl.requestOTAUpdate();
  Log::debug("OTA update check scheduled");
}

void loop() {
#ifdef __BOOT_LOG__
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b' || c == 'B') {
      props.dumpBootLogs();
    }
  }
#endif

  // Update Tuya protocol (handles responses, heartbeat, connection status, and queue processing)
  tuya.setRadioConnected(Zigbee.connected());
  tuya.update();

  // Update button state (non-blocking)
  factoryResetButton.update();
  
  // Update LED status based on Zigbee state
  updateLedStatus();
  statusLed.update();
  
  // Check for factory reset long press (blocked during OTA)
  if (factoryResetButton.wasLongPressed()) {
    if (otaRunning) {
      Log::error("OTA in progress - factory reset blocked");
    } else {
      Log::info("Factory reset requested - rebooting in 1s");
      cleanupAllResources();
      delay(FACTORY_RESET_DELAY_MS);
      Zigbee.factoryReset();
    }
  }
}

/********************* utility functions **************************/
// Global cleanup function for all allocated resources
void cleanupAllResources() {
  Log::debug("Cleaning up all allocated resources");
  zbFanControl.cleanupCustomCluster();
  // Add other cleanup here as needed in future
}

// Update LED status based on current Zigbee network state
void updateLedStatus() {
  if (esp_zb_bdb_is_factory_new()) {
    statusLed.setStatus(LedStatus::FACTORY_NEW);
  } else if (!Zigbee.connected()) {
    statusLed.setStatus(LedStatus::INITIALISING);
  } else {
    statusLed.setStatus(LedStatus::CONNECTED);
  }
}
