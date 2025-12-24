
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
ZigbeeColorDimmableLight zbLight = ZigbeeColorDimmableLight(ZIGBEE_LIGHT_CONTROL_ENDPOINT);
TuyaProtocol tuya(&tuyaSerial);

// USB Serial (Serial) is used for debug output

/********************* fan control callback function **************************/
void setFan(ZigbeeFanMode mode) {
  switch (mode) {
    case FAN_MODE_OFF:
      tuya.setFanSwitch(false);
      Serial.println("Fan mode: OFF");
      break;
    case FAN_MODE_LOW:
      tuya.setFanSwitch(true);
      if (!tuya.setFanSpeed(FAN_SPEED_LOW_TUYA)) {
        Serial.println("Failed to set fan speed: LOW");
      }
      Serial.println("Fan mode: LOW");
      break;
    case FAN_MODE_MEDIUM:
      tuya.setFanSwitch(true);
      if (!tuya.setFanSpeed(FAN_SPEED_MEDIUM_TUYA)) {
        Serial.println("Failed to set fan speed: MEDIUM");
      }
      Serial.println("Fan mode: MEDIUM");
      break;
    case FAN_MODE_HIGH:
      tuya.setFanSwitch(true);
      if (!tuya.setFanSpeed(FAN_SPEED_HIGH_TUYA)) {
        Serial.println("Failed to set fan speed: HIGH");
      }
      Serial.println("Fan mode: HIGH");
      break;
    case FAN_MODE_ON:
      tuya.setFanSwitch(true);
      Serial.println("Fan mode: ON");
      break;
    default: Serial.printf("Unhandled fan mode: %d\n", mode); break;
  }
}

/********************* light control callback functions **************************/
void setLight(bool on, uint8_t level, uint16_t colourTempMired) {
  // Light callback - handle all light changes (on/off, brightness, colour temp)
  tuya.setLightSwitch(on);
  
  if (on) {
    // Convert Zigbee brightness (0-254) to Tuya brightness (0-5)
    uint8_t tuyaBrightness = zigbeeBrightnessToTuya(level);
    if (!tuya.setLightBrightness(tuyaBrightness)) {
      Serial.printf("Failed to set light brightness: %d\n", tuyaBrightness);
    }
    
    // Convert mired to Tuya colour temp values
    ColourTempLevel tuyaColourTemp = miredToTuyaColourTemp(colourTempMired);
    if (!tuya.setLightColourTemp(static_cast<uint8_t>(tuyaColourTemp))) {
      Serial.printf("Failed to set light colour temperature: %d\n", static_cast<uint8_t>(tuyaColourTemp));
    }
  }
  
  Serial.printf("Light: %s, Level: %d, Temp: %d mired (%dK)\n", on ? "ON" : "OFF", level, colourTempMired, miredToKelvin(colourTempMired));
}

/********************* individual device status handlers **************************/

// Handle fan switch status updates from MCU
void handleFanSwitchStatus(uint32_t value) {
  bool fanOn = (value != 0);
  if (!zbFanControl.setFanState(fanOn)) {
    Serial.printf("Failed to update Zigbee fan switch status: %s\n", fanOn ? "ON" : "OFF");
  }
  Serial.printf("Fan switch status: %s\n", fanOn ? "ON" : "OFF");
}

// Handle fan speed status updates from MCU
void handleFanSpeedStatus(uint32_t value) {
  uint8_t speed = static_cast<uint8_t>(value);
  if (isValidTuyaFanSpeed(speed)) {
    if (!zbFanControl.setFanSpeed(speed)) {
      Serial.printf("Failed to update Zigbee fan speed status: %d\n", speed);
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
    // Note: Direction is not a standard Zigbee attribute
    // Could be implemented as a custom manufacturer attribute if needed
    Serial.printf("Fan direction status: %d (%s)\n", direction, 
      (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE");
  } else {
    Serial.printf("Invalid fan direction status received: %d\n", direction);
  }
}

// Handle light switch status updates from MCU
void handleLightSwitchStatus(uint32_t value) {
  bool lightOn = (value != 0);
  if (!zbLight.setLightState(lightOn)) {
    Serial.printf("Failed to update Zigbee light switch status: %s\n", lightOn ? "ON" : "OFF");
  }
  Serial.printf("Light switch status: %s\n", lightOn ? "ON" : "OFF");
}

// Handle light brightness status updates from MCU
void handleLightBrightnessStatus(uint32_t value) {
  uint8_t tuyaBrightness = static_cast<uint8_t>(value);
  
  if (isValidTuyaBrightness(tuyaBrightness)) {
    uint8_t zigbeeBrightness = tuyaBrightnessToZigbee(tuyaBrightness);
    if (!zbLight.setLightLevel(zigbeeBrightness)) {
      Serial.printf("Failed to update Zigbee light brightness: %d\n", zigbeeBrightness);
    }
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
    }
    Serial.printf("Light colour temp status: %d (%d mired, %dK)\n", 
      colourTempValue, colourTempMired, miredToKelvin(colourTempMired));
  } else {
    Serial.printf("Invalid light colour temperature status received: %d\n", colourTempValue);
  }
}

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
      
    case DP_LIGHT_SWITCH:
      handleLightSwitchStatus(value);
      break;
      
    case DP_LIGHT_DIMMER:
      handleLightBrightnessStatus(value);
      break;
      
    case DP_LIGHT_COLOUR_TEMP:
      handleLightColourTempStatus(value);
      break;
      
    default:
      handleUnknownStatus(dpid, value);
      break;
  }
}

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUD_RATE);  // USB Serial for debug output
  tuya.begin(MCU_SERIAL_BAUD_RATE);
  tuya.setDeviceStatusCallback(onDeviceStatus);
  Serial.println("Skyfan Zigbee Controller Starting...");

  // Factory reset button is initialized in constructor

  // Set Zigbee device name and model
  zbFanControl.setManufacturerAndModel(ZIGBEE_DEVICE_MANUFACTURER, ZIGBEE_FAN_MODEL_NAME);
  zbLight.setManufacturerAndModel(ZIGBEE_DEVICE_MANUFACTURER, ZIGBEE_LIGHT_MODEL_NAME);

  // Configure light colour capabilities to support colour temperature
  zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
  
  // Set colour temperature range (154-333 mired = 6500K-3000K)
  zbLight.setLightColorTemperatureRange(ZIGBEE_COLOUR_TEMP_MIN_MIRED, ZIGBEE_COLOUR_TEMP_MAX_MIRED);

  // Set the fan mode sequence to LOW_MED_HIGH
  zbFanControl.setFanModeSequence(FAN_MODE_SEQUENCE_LOW_MED_HIGH);

  // Set callback functions for fan and light control
  zbFanControl.onFanModeChange(setFan);
  zbLight.onLightChangeTemp(setLight);

  //Add endpoints to Zigbee Core
  Serial.println("Adding ZigbeeFanControl endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbFanControl);
  Serial.println("Adding ZigbeeLight endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbLight);

  // When all EPs are registered, start Zigbee in ROUTER mode
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Serial.println("Zigbee failed to start!");
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
  
  // Update button state (non-blocking)
  factoryResetButton.update();
  
  // Update LED status based on Zigbee state
  updateLedStatus();
  statusLed.update();
  
  // Check for factory reset long press
  if (factoryResetButton.wasLongPressed()) {
    Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
    delay(FACTORY_RESET_DELAY_MS);
    Zigbee.factoryReset();
  }
  
  delay(MAIN_LOOP_DELAY_MS);
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
