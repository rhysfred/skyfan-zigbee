
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
SkyfanZigbeeLight zbLight = SkyfanZigbeeLight(ZIGBEE_LIGHT_CONTROL_ENDPOINT);
TuyaProtocol tuya(&tuyaSerial);
PersistedProperties props;

bool hasLight = true;
bool mcuBreakout = false;

// Breakout check - called during MCU connect/negotiate loops
// Returns true if 'b' received on debug serial, triggering breakout mode
bool checkSerialBreakout() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b' || c == 'B') {
      Log::info("Breakout command received — skipping MCU communication");
      mcuBreakout = true;
      return true;
    }
  }
  return false;
}

const char* getResetReasonString() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_SW:        return "Software reset";
    case ESP_RST_PANIC:     return "Exception/panic";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "Unknown";
  }
}

// OTA state tracking
volatile bool otaRunning = false;

/********************* fan control callback functions **************************/
void setFan(ZigbeeFanMode mode) {
  if (mcuBreakout) {
    Log::error("MCU breakout active — rejecting fan mode change, rolling back");
    zbFanControl.rollbackFanMode();
    return;
  }
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
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, rollback);
      tuya.queueCommand(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_LOW_TUYA, rollback);
      Log::info("Fan mode set to LOW (1) by Zigbee");
      break;
    case FAN_MODE_MEDIUM:
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, rollback);
      tuya.queueCommand(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_MEDIUM_TUYA, rollback);
      Log::info("Fan mode set to MEDIUM (3) by Zigbee");
      break;
    case FAN_MODE_HIGH:
      tuya.queueCommand(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, rollback);
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
  if (mcuBreakout) {
    Log::error("MCU breakout active — rejecting fan direction change, rolling back");
    zbFanControl.rollbackFanDirection();
    return;
  }
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
static bool lightCallbackInitialized = false;  // Skip first callback from initialization

void setLight(bool on, uint8_t level, uint16_t colourTempMired) {
  if (mcuBreakout) {
    Log::error("MCU breakout active — rejecting light change, rolling back");
    zbLight.rollback();
    return;
  }

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

  if (on) {
    // Convert Zigbee brightness (0-254) to Tuya brightness (0-5)
    uint8_t tuyaBrightness = zigbeeBrightnessToTuya(level);
    // Convert mired to Tuya colour temp values
    ColourTempLevel tuyaColourTemp = miredToTuyaColourTemp(colourTempMired);

    // Switch on first so light responds immediately; adjust values after
    tuya.queueCommand(DP_LIGHT_SWITCH, DP_TYPE_BOOL, 1, rollback);
    tuya.queueCommand(DP_LIGHT_COLOUR_TEMP, DP_TYPE_ENUM, static_cast<uint8_t>(tuyaColourTemp));
    tuya.queueCommand(DP_LIGHT_DIMMER, DP_TYPE_VALUE, tuyaBrightness);
  } else {
    tuya.queueCommand(DP_LIGHT_SWITCH, DP_TYPE_BOOL, 0, rollback);
  }

  Log::info("Light set to %s, level %d, temp %d mired (%dK) by Zigbee",
    on ? "ON" : "OFF", level, colourTempMired, miredToKelvin(colourTempMired));
}

/********************* device status callback function **************************/
void onDeviceStatus(uint8_t dpid, uint32_t value) {
  switch (dpid) {
    case DP_FAN_SWITCH:
    case DP_FAN_SPEED:
    case DP_FAN_MODE:
    case DP_FAN_DIRECTION:
      zbFanControl.handleStatusUpdate(dpid, value);
      break;

    case DP_LIGHT_SWITCH:
    case DP_LIGHT_DIMMER:
    case DP_LIGHT_COLOUR_TEMP:
      if (hasLight) {
        zbLight.handleStatusUpdate(dpid, value);
      } else {
        Log::debug("Ignoring light DPID %d on fan-only model", dpid);
      }
      break;

    default:
      Log::error("Unknown status update - DPID: %d, Value: %lu from Skyfan", dpid, value);
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
  Log::info("Reset reason: %s", getResetReasonString());

  // Load persisted properties from NVS
  props.begin();

  // Register heartbeat callback before connect() so heartbeats during loops are captured
  tuya.setHeartbeatCallback([](bool isRestart) {
    props.onMcuHeartbeat(isRestart);
  });

  // Connect to MCU - use persisted baud rate if available, otherwise negotiate
  bool mcuConnected = false;
  uint32_t storedRate = props.getMcuBaudRate();
  if (storedRate > 0) {
    mcuConnected = tuya.connect(storedRate, checkSerialBreakout);
  } else {
    uint8_t cycles = 0;
    int32_t result = tuya.negotiateBaud(checkSerialBreakout, cycles);
    if (result > 0) {
      mcuConnected = true;
      props.setMcuBaudRate(result);
      props.setBaudNegotiationCycles(cycles);
    }
  }

#ifdef __BOOT_LOG__
  if (Log::getBootLog()[0] != '\0') {
    props.writeBootLog(Log::getBootLog());
    Log::clearBootLog();
  }
#endif

  // Query product info from MCU (dedicated step with own timeout)
  if (mcuConnected && !mcuBreakout) {
    delay(50);
    tuya.queryProductInfo();
    tuya.waitForProductInfo(TUYA_COMMAND_TIMEOUT_MS);
  }

  // Resolve product ID: NVS cache takes precedence, MCU value used to populate or detect mismatch
  const char* storedProductId = props.getProductId();
  const char* mcuProductId = tuya.getProductId();

  if (storedProductId[0] == '\0' && mcuProductId[0] != '\0') {
    // First boot with this MCU — persist its product ID
    props.setProductId(mcuProductId);
    storedProductId = props.getProductId();
    Log::info("Product ID: %s (from MCU, persisted)", storedProductId);
  } else if (storedProductId[0] != '\0' && mcuProductId[0] != '\0'
             && strcmp(storedProductId, mcuProductId) != 0) {
    // MCU product ID differs from persisted — log warning, persist mismatch for diagnostics
    props.setProductIdMismatch(mcuProductId);
    Log::error("Product ID mismatch! NVS: %s, MCU: %s (using NVS value)", storedProductId, mcuProductId);
  } else if (storedProductId[0] != '\0') {
    Log::info("Product ID: %s (from NVS)", storedProductId);
  } else if (mcuProductId[0] != '\0') {
    Log::info("Product ID: %s (from MCU)", mcuProductId);
  } else {
    Log::info("Product ID: not available (defaulting to fan+light)");
  }

  hasLight = isLightModel(storedProductId);
  const char* modelName = hasLight ? ZIGBEE_MODEL_NAME_FAN_LIGHT : ZIGBEE_MODEL_NAME_FAN_ONLY;
  Log::info("Model: %s (light: %s)", modelName, hasLight ? "yes" : "no");

  // Set Zigbee device name and model
  zbFanControl.setManufacturerAndModel(ZIGBEE_DEVICE_MANUFACTURER, modelName);

  // Configure device power source as mains-powered (not battery)
  zbFanControl.setPowerSource(ZB_POWER_SOURCE_MAINS);
  Log::debug("Device configured as mains-powered");

  if (hasLight) {
    // Configure light colour capabilities to support colour temperature
    zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);

    // Set colour temperature range (154-333 mired = 6500K-3000K)
    zbLight.setLightColorTemperatureRange(ZIGBEE_COLOUR_TEMP_MIN_MIRED, ZIGBEE_COLOUR_TEMP_MAX_MIRED);
  }

  // Set the fan mode sequence to LOW_MED_HIGH
  zbFanControl.setFanModeSequence(FAN_MODE_SEQUENCE_LOW_MED_HIGH);

  // Set callback functions for fan and light control
  zbFanControl.onFanModeChange(setFan);
  zbFanControl.onFanDirectionChange(setFanDirection);
  if (hasLight) {
    zbLight.onLightChangeTemp(setLight);
  }

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
  if (hasLight) {
    Log::debug("Adding ZigbeeLight endpoint to Zigbee Core");
    Zigbee.addEndpoint(&zbLight);
  }

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
      props.clearAll();
      cleanupAllResources();
      delay(FACTORY_RESET_DELAY_MS);
      Zigbee.factoryReset();
    }
  }
  Serial.println();
  Log::info("Zigbee connected successfully");

  // Post-connect: register status callback, sync state to coordinator
  if (!mcuBreakout) {
    tuya.setDeviceStatusCallback(onDeviceStatus);

    if (hasLight) {
      // Initialise colour temperature to set colour mode to TEMPERATURE
      // This ensures the temp callback is called for on/off and level changes
      zbLight.setLightColorTemperature(COLOUR_TEMP_WARM_MIRED);
    }

    // Single DP query now that Zigbee is connected — status reports reach coordinator
    tuya.sendDatapointQuery();
    unsigned long syncStart = millis();
    while (millis() - syncStart < 500) {
      tuya.processResponse();
      delay(5);
    }
    Log::info("Initial state synced to coordinator");
  }

  // Start OTA client query - first request is within a minute, then hourly automatically
  zbFanControl.requestOTAUpdate();
  Log::debug("OTA update check scheduled");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      props.dumpAll();
    } else if (c == 'c' || c == 'C') {
      props.clearAll();
      Log::info("All persisted data cleared");
    } else if (c == 'l' || c == 'L') {
#ifdef __BOOT_LOG__
      props.dumpBootLogs();
#else
      Log::info("Boot logging not enabled (define __BOOT_LOG__ to enable)");
#endif
    } else if (c == 'r' || c == 'R') {
      Log::info("Factory reset requested via serial");
      props.clearAll();
      cleanupAllResources();
      delay(FACTORY_RESET_DELAY_MS);
      Zigbee.factoryReset();
    }
  }

  // Update Tuya protocol (handles responses, heartbeat, connection status, and queue processing)
  if (!mcuBreakout) {
    tuya.setRadioConnected(Zigbee.connected());
    tuya.update();
  }

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
      props.clearAll();
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
