
/*
 * Skyfan Zigbee Controller - Zigbee 3.0 controller for Ventair Skyfan ceiling fans with integrated lighting
 * Copyright (C) 2025 Rhys Frederick at Front Left Speaker
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
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
#include "TuyaProtocol.h"
#include "SkyfanZigbee.h"

/* Zigbee configuration */
#define ZIGBEE_FAN_CONTROL_ENDPOINT 1
#define ZIGBEE_LIGHT_ENDPOINT 2

#ifdef RGB_BUILTIN
uint8_t led = RGB_BUILTIN;  // To demonstrate the current fan control mode
#else
uint8_t led = 2;
#endif

uint8_t button = BOOT_PIN;

SkyfanZigbeeFanControl zbFanControl = SkyfanZigbeeFanControl(ZIGBEE_FAN_CONTROL_ENDPOINT);
ZigbeeColorDimmableLight zbLight = ZigbeeColorDimmableLight(ZIGBEE_LIGHT_ENDPOINT);
TuyaProtocol tuya;

/********************* fan control callback function **************************/
void setFan(ZigbeeFanMode mode) {
  switch (mode) {
    case FAN_MODE_OFF:
      tuya.setFanSwitch(false);
      // Serial.println("Fan mode: OFF");
      break;
    case FAN_MODE_LOW:
      tuya.setFanSwitch(true);
      tuya.setFanSpeed(1);
      // Serial.println("Fan mode: LOW");
      break;
    case FAN_MODE_MEDIUM:
      tuya.setFanSwitch(true);
      tuya.setFanSpeed(3);
      // Serial.println("Fan mode: MEDIUM");
      break;
    case FAN_MODE_HIGH:
      tuya.setFanSwitch(true);
      tuya.setFanSpeed(5);
      // Serial.println("Fan mode: HIGH");
      break;
    case FAN_MODE_ON:
      tuya.setFanSwitch(true);
      // Serial.println("Fan mode: ON");
      break;
    default: /* Serial.printf("Unhandled fan mode: %d\n", mode); */ break;
  }
}

/********************* light control callback functions **************************/
void setLight(bool on, uint8_t level, uint16_t colorTempMired) {
  // Light callback - handle all light changes (on/off, brightness, color temp)
  tuya.setLightSwitch(on);
  
  if (on) {
    // Convert Zigbee brightness (0-254) to Tuya brightness (0-5)
    uint8_t tuyaBrightness = map(level, 0, 254, 0, 5);
    tuya.setLightBrightness(tuyaBrightness);
    
    // Convert mired to Tuya colour temp values
    uint32_t kelvin = 1000000 / colorTempMired;
    
    uint8_t tuyaColorTemp;
    if (kelvin <= 3600) {
      tuyaColorTemp = COLOUR_TEMP_WARM;    // 3000K
    } else if (kelvin <= 5350) {
      tuyaColorTemp = COLOUR_TEMP_NATURAL; // 4200K
    } else {
      tuyaColorTemp = COLOUR_TEMP_COOL;    // 6500K
    }
    
    tuya.setLightColourTemp(tuyaColorTemp);
  }
  
  // Serial.printf("Light: %s, Level: %d, Temp: %d mired (%dK)\n", on ? "ON" : "OFF", level, colorTempMired, 1000000/colorTempMired);
}

/********************* device status callback function **************************/
void onDeviceStatus(uint8_t dpid, uint32_t value) {
  switch (dpid) {
    case DP_FAN_SWITCH: {
      // Update Zigbee fan switch status
      zbFanControl.setFanState(value ? true : false);
      // Serial.printf("Fan switch status: %s\n", value ? "ON" : "OFF");
      break;
    }
      
    case DP_FAN_SPEED: {
      // Update Zigbee fan speed status
      zbFanControl.setFanSpeedMode(value);
      // Serial.printf("Fan speed status: %d\n", value);
      break;
    }
      
    case DP_FAN_MODE:
      // Fan mode (normal/eco/sleep) - MCU only, not exposed to Zigbee
      // Serial.printf("Fan mode status: %d\n", value);
      break;
      
    case DP_FAN_DIRECTION: {
      // Fan direction status received from MCU
      // Note: Direction is not a standard Zigbee attribute
      // Serial.printf("Fan direction status: %d\n", value);
      break;
    }
      
    case DP_LIGHT_SWITCH: {
      // Update Zigbee light power status
      zbLight.setLightState(value ? true : false);
      // Serial.printf("Light switch status: %s\n", value ? "ON" : "OFF");
      break;
    }
      
    case DP_LIGHT_DIMMER: {
      // Convert Tuya brightness (0-5) to Zigbee brightness (0-254)
      uint8_t zigbeeBrightness = map(value, 0, 5, 0, 254);
      zbLight.setLightLevel(zigbeeBrightness);
      // Serial.printf("Light brightness status: %d (Zigbee: %d)\n", value, zigbeeBrightness);
      break;
    }
      
    case DP_LIGHT_COLOUR_TEMP: {
      // Map Tuya colour temp to Zigbee colour temp in mired
      uint16_t colorTempMired;
      switch (value) {
        case COLOUR_TEMP_WARM:    // 3000K
          colorTempMired = 333;   // 1000000/3000
          break;
        case COLOUR_TEMP_NATURAL: // 4200K
          colorTempMired = 238;   // 1000000/4200
          break;
        case COLOUR_TEMP_COOL:    // 6500K
          colorTempMired = 154;   // 1000000/6500
          break;
        default:
          colorTempMired = 333;   // Default to warm
          break;
      }
      zbLight.setLightColorTemperature(colorTempMired);
      // Serial.printf("Light colour temp status: %d (%d mired)\n", value, colorTempMired);
      break;
    }
      
    default:
      // Serial.printf("Unknown status update - DPID: %d, Value: %d\n", dpid, value);
      break;
  }
}

/********************* Arduino functions **************************/
void setup() {
  tuya.begin(115200);
  tuya.setDeviceStatusCallback(onDeviceStatus);
  // Serial.println("Skyfan Zigbee Controller Starting...");

  // Init button for factory reset
  pinMode(button, INPUT_PULLUP);

  //Optional: set Zigbee device name and model
  zbFanControl.setManufacturerAndModel("Ventair", "Skyfan");
  zbLight.setManufacturerAndModel("Ventair", "Skyfan Light");

  // Configure light color capabilities to support color temperature
  zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
  
  // Set color temperature range (154-333 mired = 6500K-3000K)
  zbLight.setLightColorTemperatureRange(154, 333);

  // Set the fan mode sequence to LOW_MED_HIGH
  zbFanControl.setFanModeSequence(FAN_MODE_SEQUENCE_LOW_MED_HIGH);

  // Set callback functions for fan and light control
  zbFanControl.onFanModeChange(setFan);
  zbLight.onLightChangeTemp(setLight);

  //Add endpoints to Zigbee Core
  // Serial.println("Adding ZigbeeFanControl endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbFanControl);
  // Serial.println("Adding ZigbeeLight endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbLight);

  // When all EPs are registered, start Zigbee in ROUTER mode
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    // Serial.println("Zigbee failed to start!");
    // Serial.println("Rebooting...");
    ESP.restart();
  }
  // Serial.println("Connecting to network");
  while (!Zigbee.connected()) {
    // Serial.print(".");
    delay(100);
  }
  // Serial.println();
  // Serial.println("Zigbee connected successfully!");
}

void loop() {
  // Update Tuya protocol (handles responses, heartbeat, connection status)
  tuya.update(Zigbee.connected());
  
  // Checking button for factory reset
  if (digitalRead(button) == LOW) {  // Push button pressed
    // Key debounce handling
    delay(100);
    int startTime = millis();
    while (digitalRead(button) == LOW) {
      delay(50);
      if ((millis() - startTime) > 3000) {
        // If key pressed for more than 3secs, factory reset Zigbee and reboot
        // Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
        delay(1000);
        Zigbee.factoryReset();
      }
    }
  }
  delay(100);
}
