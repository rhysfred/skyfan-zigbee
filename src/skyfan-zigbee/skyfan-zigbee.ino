
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
#include "SkyfanZigbee.h"
#include "LedIndicator.h"
#include "ButtonHandler.h"
#include <HardwareSerial.h>

#ifdef RGB_BUILTIN
uint8_t led = RGB_BUILTIN;
#else
uint8_t led = 2;
#endif

DebouncedButton factoryResetButton(FACTORY_RESET_BUTTON_PIN);
LedStatusIndicator statusLed(led);

// Hardware UART for Tuya MCU communication
HardwareSerial tuyaSerial(0);

SkyfanZigbeeFanControl zbFanControl = SkyfanZigbeeFanControl(ZIGBEE_FAN_CONTROL_ENDPOINT);
#ifdef WITH_LIGHT
ZigbeeColorDimmableLight zbLight = ZigbeeColorDimmableLight(ZIGBEE_LIGHT_CONTROL_ENDPOINT);
#endif
TuyaProtocol tuya(&tuyaSerial);

// State tracking variables for rollback (representing MCU confirmed state)
ZigbeeFanMode lastConfirmedFanMode = FAN_MODE_OFF;  // Default: fan off
uint8_t lastConfirmedFanDirection = static_cast<uint8_t>(FanDirection::FORWARD);  // Default: forward
#ifdef WITH_LIGHT
bool lastConfirmedLightState = true;  // Default: light on
uint8_t lastConfirmedLightBrightness = 127;  // Default: middle brightness (0-254 scale)
uint16_t lastConfirmedLightColorTemp = COLOUR_TEMP_WARM_MIRED;  // Default: warm
#endif

// USB Serial (Serial) is used for debug output

/********************* fan control callback functions **************************/
void setFan(ZigbeeFanMode mode) {
  // Zigbee state already updated when this callback is called
#ifdef __DEBUG__
  debugLogZigbeeMessage("Write", ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, mode);
#endif
  statusLed.flashCommand();
  
  switch (mode) {
    case FAN_MODE_OFF:
      tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 0, CommandType::FAN_SWITCH);
      Serial.println("Fan mode: OFF");
      break;
    case FAN_MODE_LOW:
      tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, CommandType::FAN_SWITCH);
      tuya.sendDataPointWithTracking(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_LOW_TUYA, CommandType::FAN_SPEED);
      Serial.println("Fan mode: LOW");
      break;
    case FAN_MODE_MEDIUM:
      tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, CommandType::FAN_SWITCH);
      tuya.sendDataPointWithTracking(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_MEDIUM_TUYA, CommandType::FAN_SPEED);
      Serial.println("Fan mode: MEDIUM");
      break;
    case FAN_MODE_HIGH:
      tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, CommandType::FAN_SWITCH);
      tuya.sendDataPointWithTracking(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_HIGH_TUYA, CommandType::FAN_SPEED);
      Serial.println("Fan mode: HIGH");
      break;
    case FAN_MODE_ON:
      tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, CommandType::FAN_SWITCH);
      Serial.println("Fan mode: ON");
      break;
    default: 
      Serial.printf("Unhandled fan mode: %d\n", mode); 
      break;
  }
}

// Fan direction control callback function
void setFanDirection(uint8_t direction) {
  // Zigbee state already updated when this callback is called
#ifdef __DEBUG__
  debugLogZigbeeMessage("Write", ZIGBEE_FAN_CONTROL_ENDPOINT, VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, direction);
#endif
  statusLed.flashCommand();
  
  // Send command with tracking
  tuya.sendDataPointWithTracking(DP_FAN_DIRECTION, DP_TYPE_ENUM, direction, CommandType::FAN_DIRECTION);
  
  Serial.printf("Fan direction set to: %d (%s)\n", direction,
    (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE");
}

/********************* light control callback functions **************************/
#ifdef WITH_LIGHT
void setLight(bool on, uint8_t level, uint16_t colourTempMired) {
  // Zigbee state already updated when this callback is called
#ifdef __DEBUG__
  // Note: This callback may receive multiple attribute changes, but we can only log them individually
  debugLogZigbeeMessage("Write", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, on ? 1 : 0);
  debugLogZigbeeMessage("Write", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, level);
  debugLogZigbeeMessage("Write", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, colourTempMired);
#endif
  statusLed.flashCommand();
  
  // Send switch command
  tuya.sendDataPointWithTracking(DP_LIGHT_SWITCH, DP_TYPE_BOOL, on ? 1 : 0, CommandType::LIGHT_SWITCH);
  
  if (on) {
    // Convert Zigbee brightness (0-254) to Tuya brightness (0-5)
    uint8_t tuyaBrightness = zigbeeBrightnessToTuya(level);
    tuya.sendDataPointWithTracking(DP_LIGHT_DIMMER, DP_TYPE_VALUE, tuyaBrightness, CommandType::LIGHT_BRIGHTNESS);
    
    // Convert mired to Tuya colour temp values
    ColourTempLevel tuyaColourTemp = miredToTuyaColourTemp(colourTempMired);
    tuya.sendDataPointWithTracking(DP_LIGHT_COLOUR_TEMP, DP_TYPE_ENUM, static_cast<uint8_t>(tuyaColourTemp), CommandType::LIGHT_COLOR_TEMP);
  }
  
  Serial.printf("Light: %s, Level: %d, Temp: %d mired (%dK)\n", on ? "ON" : "OFF", level, colourTempMired, miredToKelvin(colourTempMired));
}
#endif

/********************* individual device status handlers **************************/

// Handle fan switch status updates from MCU
void handleFanSwitchStatus(uint32_t value) {
  bool fanOn = (value != 0);
  if (!zbFanControl.setFanState(fanOn)) {
    Serial.printf("Failed to update Zigbee fan switch status: %s\n", fanOn ? "ON" : "OFF");
  } else {
#ifdef __DEBUG__
    debugLogZigbeeMessage("Read", ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, fanOn ? FAN_MODE_ON : FAN_MODE_OFF);
#endif
  }
  
  // Update confirmed state
  lastConfirmedFanMode = fanOn ? FAN_MODE_ON : FAN_MODE_OFF;
  
  Serial.printf("Fan switch status: %s\n", fanOn ? "ON" : "OFF");
}

// Handle fan speed status updates from MCU
void handleFanSpeedStatus(uint32_t value) {
  uint8_t speed = static_cast<uint8_t>(value);
  if (isValidTuyaFanSpeed(speed)) {
    if (!zbFanControl.setFanSpeed(speed)) {
      Serial.printf("Failed to update Zigbee fan speed status: %d\n", speed);
    } else {
#ifdef __DEBUG__
      // Log the fan mode change that results from the speed change
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
      debugLogZigbeeMessage("Read", ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, mode);
#endif
    }
    
    // Update confirmed state based on speed
    switch (speed) {
      case TUYA_FAN_SPEED_MIN:
        lastConfirmedFanMode = FAN_MODE_OFF;
        break;
      case FAN_SPEED_LOW_TUYA:
      case FAN_SPEED_LOW_TUYA + 1:
        lastConfirmedFanMode = FAN_MODE_LOW;
        break;
      case FAN_SPEED_MEDIUM_TUYA:
      case FAN_SPEED_MEDIUM_TUYA + 1:
        lastConfirmedFanMode = FAN_MODE_MEDIUM;
        break;
      case FAN_SPEED_HIGH_TUYA:
        lastConfirmedFanMode = FAN_MODE_HIGH;
        break;
      default:
        lastConfirmedFanMode = FAN_MODE_ON;
        break;
    }
    
    Serial.printf("Fan speed status: %d\n", speed);
  } else {
    Serial.printf("Invalid fan speed status received: %d\n", speed);
  }
}

// Handle fan mode status updates from MCU (MCU-only, not exposed to Zigbee)
void handleFanModeStatus(uint32_t value) {
  uint8_t mode = static_cast<uint8_t>(value);
  if (mode <= static_cast<uint8_t>(TuyaFanMode::SLEEP)) {
    Serial.printf("Fan mode status: %d (%s)\n", mode, 
      (mode == static_cast<uint8_t>(TuyaFanMode::NORMAL)) ? "NORMAL" :
      (mode == static_cast<uint8_t>(TuyaFanMode::ECO)) ? "ECO" : "SLEEP");
  } else {
    Serial.printf("Invalid fan mode status received: %d\n", mode);
  }
}

// Handle fan direction status updates from MCU
void handleFanDirectionStatus(uint32_t value) {
  uint8_t direction = static_cast<uint8_t>(value);
  if (direction <= static_cast<uint8_t>(FanDirection::REVERSE)) {
    // Update custom manufacturer attribute for fan direction
    if (!zbFanControl.setFanDirection(direction)) {
      Serial.printf("Failed to update Zigbee fan direction status: %d\n", direction);
    } else {
#ifdef __DEBUG__
      debugLogZigbeeMessage("Read", ZIGBEE_FAN_CONTROL_ENDPOINT, VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, direction);
#endif
    }
    
    // Update confirmed state
    lastConfirmedFanDirection = direction;
    
    Serial.printf("Fan direction status: %d (%s)\n", direction, 
      (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE");
  } else {
    Serial.printf("Invalid fan direction status received: %d\n", direction);
  }
}

#ifdef WITH_LIGHT
// Handle light switch status updates from MCU
void handleLightSwitchStatus(uint32_t value) {
  bool lightOn = (value != 0);
  if (!zbLight.setLightState(lightOn)) {
    Serial.printf("Failed to update Zigbee light switch status: %s\n", lightOn ? "ON" : "OFF");
  } else {
#ifdef __DEBUG__
    debugLogZigbeeMessage("Read", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, lightOn ? 1 : 0);
#endif
  }
  
  // Update confirmed state
  lastConfirmedLightState = lightOn;
  
  Serial.printf("Light switch status: %s\n", lightOn ? "ON" : "OFF");
}

// Handle light brightness status updates from MCU
void handleLightBrightnessStatus(uint32_t value) {
  uint8_t tuyaBrightness = static_cast<uint8_t>(value);
  
  if (isValidTuyaBrightness(tuyaBrightness)) {
    uint8_t zigbeeBrightness = tuyaBrightnessToZigbee(tuyaBrightness);
    if (!zbLight.setLightLevel(zigbeeBrightness)) {
      Serial.printf("Failed to update Zigbee light brightness: %d\n", zigbeeBrightness);
    } else {
#ifdef __DEBUG__
      debugLogZigbeeMessage("Read", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, zigbeeBrightness);
#endif
    }
    
    // Update confirmed state
    lastConfirmedLightBrightness = zigbeeBrightness;
    
    Serial.printf("Light brightness status: %d (Zigbee: %d)\n", tuyaBrightness, zigbeeBrightness);
  } else {
    Serial.printf("Invalid light brightness status received: %d\n", tuyaBrightness);
  }
}

// Handle light colour temperature status updates from MCU
void handleLightColourTempStatus(uint32_t value) {
  uint8_t colourTempValue = static_cast<uint8_t>(value);
  
  if (colourTempValue <= static_cast<uint8_t>(ColourTempLevel::COOL)) {
    ColourTempLevel colourLevel = static_cast<ColourTempLevel>(colourTempValue);
    uint16_t colourTempMired = tuyaColourTempToMired(colourLevel);
    
    if (!zbLight.setLightColorTemperature(colourTempMired)) {
      Serial.printf("Failed to update Zigbee light colour temperature: %d mired\n", colourTempMired);
    } else {
#ifdef __DEBUG__
      debugLogZigbeeMessage("Read", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, colourTempMired);
#endif
    }
    
    // Update confirmed state
    lastConfirmedLightColorTemp = colourTempMired;
    
    Serial.printf("Light colour temp status: %d (%d mired, %dK)\n", 
      colourTempValue, colourTempMired, miredToKelvin(colourTempMired));
  } else {
    Serial.printf("Invalid light colour temperature status received: %d\n", colourTempValue);
  }
}
#endif

// Handle unknown/unsupported status updates from MCU
void handleUnknownStatus(uint8_t dpid, uint32_t value) {
  Serial.printf("Unknown status update - DPID: %d, Value: %d\n", dpid, value);
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

/********************* rollback functions **************************/
// Handle command rollback when ACK or status timeout occurs
void onCommandRollback(CommandType type) {
  Serial.printf("Rolling back command type %d due to timeout\n", static_cast<int>(type));
  
  switch (type) {
    case CommandType::FAN_SWITCH:
    case CommandType::FAN_SPEED:
      zbFanControl.setFanMode(lastConfirmedFanMode);
      Serial.printf("Rolled back fan mode to %d\n", lastConfirmedFanMode);
      break;
      
    case CommandType::FAN_DIRECTION:
      zbFanControl.setFanDirection(lastConfirmedFanDirection);
      Serial.printf("Rolled back fan direction to %d\n", lastConfirmedFanDirection);
      break;
      
#ifdef WITH_LIGHT
    case CommandType::LIGHT_SWITCH:
      zbLight.setLightState(lastConfirmedLightState);
      Serial.printf("Rolled back light state to %s\n", lastConfirmedLightState ? "ON" : "OFF");
      break;
      
    case CommandType::LIGHT_BRIGHTNESS:
      zbLight.setLightLevel(lastConfirmedLightBrightness);
      Serial.printf("Rolled back light brightness to %d\n", lastConfirmedLightBrightness);
      break;
      
    case CommandType::LIGHT_COLOR_TEMP:
      zbLight.setLightColorTemperature(lastConfirmedLightColorTemp);
      Serial.printf("Rolled back light color temp to %d\n", lastConfirmedLightColorTemp);
      break;
#endif
  }
}

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUD_RATE);  // USB Serial for debug output
  tuya.begin(MCU_SERIAL_BAUD_RATE);
  tuya.setDeviceStatusCallback(onDeviceStatus);
  tuya.setRollbackCallback(onCommandRollback);
  Serial.println("Skyfan Zigbee Controller Starting...");

  // Factory reset button is initialized in constructor

  // Set Zigbee device name and model
  zbFanControl.setManufacturerAndModel(ZIGBEE_DEVICE_MANUFACTURER, ZIGBEE_MODEL_NAME);

  // Configure device power source as mains-powered (not battery)
  zbFanControl.setPowerSource(ZB_POWER_SOURCE_MAINS);
  //zbLight.setPowerSource(ZB_POWER_SOURCE_MAINS);
  Serial.println("Device configured as mains-powered");

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
    Serial.println("Failed to create custom cluster!");
    // Clean up any partially allocated resources
    cleanupAllResources();
    Serial.println("Rebooting...");
    ESP.restart();
  }

  //Add endpoints to Zigbee Core
  Serial.println("Adding ZigbeeFanControl endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbFanControl);
#ifdef WITH_LIGHT
  Serial.println("Adding ZigbeeLight endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbLight);
#endif

  // When all EPs are registered, start Zigbee in ROUTER mode
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Serial.println("Zigbee failed to start!");
    // Clean up allocated resources before restart
    cleanupAllResources();
    Serial.println("Rebooting...");
    ESP.restart();
  }
  
  // Register custom cluster handlers after Zigbee begins
  if (!registerCustomClusterHandlers()) {
    Serial.println("Failed to register custom cluster handlers!");
    // Clean up allocated resources before restart
    cleanupAllResources();
    Serial.println("Rebooting...");
    ESP.restart();
  }
  
  Serial.println("Connecting to network");
  while (!Zigbee.connected()) {
    Serial.print(".");
    delay(ZIGBEE_CONNECTION_POLL_MS);
  }
  Serial.println();
  Serial.println("Zigbee connected successfully!");
}

void loop() {
  // Update Tuya protocol (handles responses, heartbeat, connection status)
  tuya.update(Zigbee.connected());
  
  // Check for command timeouts (1.5s status response timeout)
  tuya.checkPendingCommandTimeouts();
  
  // Update button state (non-blocking)
  factoryResetButton.update();
  
  // Update LED status based on Zigbee state
  updateLedStatus();
  statusLed.update();
  
  // Check for factory reset long press
  if (factoryResetButton.wasLongPressed()) {
    Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
    // Clean up allocated resources before factory reset
    cleanupAllResources();
    delay(FACTORY_RESET_DELAY_MS);
    Zigbee.factoryReset();
  }
}

/********************* debug functions **************************/
#ifdef __DEBUG__
// Helper function to log Zigbee attribute changes
void debugLogZigbeeMessage(const char* direction, uint8_t endpoint, uint16_t cluster_id, uint16_t attr_id, uint32_t value) {
  Serial.printf("DEBUG: %s Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'\n", 
                direction, endpoint, cluster_id, attr_id, value);
}
#endif

/********************* custom cluster handlers **************************/
// Manufacturer-specific cluster attribute write handler
void custom_attr_write_handler(uint8_t endpoint, uint16_t attr_id, uint8_t *new_value, uint16_t manuf_code) {
  if (attr_id == CUSTOM_ATTR_FAN_DIRECTION && manuf_code == FLS_MANUFACTURER_CODE) {
#ifdef __DEBUG__
    debugLogZigbeeMessage("Read", endpoint, VENTAIR_CUSTOM_CLUSTER_ID, attr_id, *new_value);
#endif
    statusLed.flashCommand();
    zbFanControl.handleCustomClusterAttributeChange(VENTAIR_CUSTOM_CLUSTER_ID, attr_id, new_value);
  }
}

// Global cleanup function for all allocated resources
void cleanupAllResources() {
  Serial.println("Cleaning up all allocated resources...");
  zbFanControl.cleanupCustomCluster();
  // Add other cleanup here as needed in future
}

// Register custom cluster handlers
bool registerCustomClusterHandlers() {
  if (!zbFanControl.isCustomClusterRegistered()) {
    Serial.println("Custom cluster not registered, skipping handler registration");
    return true; // Not an error if cluster isn't registered
  }
  
  esp_zb_zcl_custom_cluster_handlers_t handlers;
  handlers.cluster_id = VENTAIR_CUSTOM_CLUSTER_ID;
  handlers.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
  handlers.check_value_cb = NULL;  // No value validation needed
  handlers.write_attr_cb = custom_attr_write_handler;
  
  esp_err_t ret = esp_zb_zcl_custom_cluster_handlers_update(handlers);
  if (ret == ESP_OK) {
    Serial.println("Custom cluster handlers registered successfully");
    return true;
  } else {
    Serial.printf("Failed to register custom cluster handlers: %d\\n", ret);
    return false;
  }
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
