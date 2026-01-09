
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
#include "SkyfanZigbeeLight.h"
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
SkyfanZigbeeLight zbLight = SkyfanZigbeeLight(ZIGBEE_LIGHT_CONTROL_ENDPOINT);
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

// OTA state tracking
volatile bool otaRunning = false;

// Rollback state tracking - prevents callback re-triggering during rollback
bool isRollingBack = false;

// USB Serial (Serial) is used for debug output

/********************* light command queue (before functions for Arduino preprocessor) **************************/
#ifdef WITH_LIGHT
// Light command structure for queue
struct LightCommand {
  bool on;
  uint8_t level;
  uint16_t colourTempMired;
};

// Light command queue (circular buffer)
#define LIGHT_COMMAND_QUEUE_SIZE 8
static LightCommand lightCommandQueue[LIGHT_COMMAND_QUEUE_SIZE];
static uint8_t lightQueueHead = 0;  // Next position to write
static uint8_t lightQueueTail = 0;  // Next position to read
static uint8_t lightQueueCount = 0; // Number of items in queue

// Pending light state for debounced processing (before queuing)
static LightCommand pendingLightCmd = {true, 127, COLOUR_TEMP_WARM_MIRED};
static unsigned long pendingLightTime = 0;
static bool hasPendingLight = false;
static bool lightCallbackInitialized = false;  // Skip first callback from initialization
static bool isProcessingLightQueue = false;    // Prevent re-entry during blocking sends
#endif

/********************* fan control callback functions **************************/
void setFan(ZigbeeFanMode mode) {
  // Skip if we're rolling back - prevents infinite loop
  if (isRollingBack) {
    return;
  }

  // Zigbee state already updated when this callback is called
#ifdef __DEBUG__
  debugLogZigbeeMessage("Write", ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, mode);
#endif
  statusLed.flashCommand();

  // Check if MCU is responding - if not, log command and immediately roll back
  if (!tuya.isMcuResponding()) {
    // Log what was requested
    const char* modeName;
    switch (mode) {
      case FAN_MODE_OFF: modeName = "OFF (0)"; break;
      case FAN_MODE_LOW: modeName = "LOW (1)"; break;
      case FAN_MODE_MEDIUM: modeName = "MEDIUM (3)"; break;
      case FAN_MODE_HIGH: modeName = "HIGH (5)"; break;
      case FAN_MODE_ON: modeName = "ON (4)"; break;
      default: modeName = "UNKNOWN"; break;
    }
    Serial.printf("INFO: Fan mode set to %s by Zigbee\n", modeName);

    // Immediately roll back
    Serial.printf("ERROR: MCU not responding - rolling back fan mode to %d\n", lastConfirmedFanMode);
    isRollingBack = true;
    zbFanControl.setFanMode(lastConfirmedFanMode);
    isRollingBack = false;
    zbFanControl.reportFanMode();
    return;
  }

  bool ackReceived = false;

  switch (mode) {
    case FAN_MODE_OFF:
      ackReceived = tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 0, CommandType::FAN_SWITCH);
      Serial.println("INFO: Fan mode set to OFF (0) by Zigbee");
      break;
    case FAN_MODE_LOW:
      // Send switch without tracking (speed command tracks the overall operation)
      ackReceived = tuya.sendDataPoint(DP_FAN_SWITCH, DP_TYPE_BOOL, 1);
      if (ackReceived) {
        ackReceived = tuya.sendDataPointWithTracking(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_LOW_TUYA, CommandType::FAN_SPEED);
      }
      Serial.println("INFO: Fan mode set to LOW (1) by Zigbee");
      break;
    case FAN_MODE_MEDIUM:
      ackReceived = tuya.sendDataPoint(DP_FAN_SWITCH, DP_TYPE_BOOL, 1);
      if (ackReceived) {
        ackReceived = tuya.sendDataPointWithTracking(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_MEDIUM_TUYA, CommandType::FAN_SPEED);
      }
      Serial.println("INFO: Fan mode set to MEDIUM (3) by Zigbee");
      break;
    case FAN_MODE_HIGH:
      ackReceived = tuya.sendDataPoint(DP_FAN_SWITCH, DP_TYPE_BOOL, 1);
      if (ackReceived) {
        ackReceived = tuya.sendDataPointWithTracking(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_HIGH_TUYA, CommandType::FAN_SPEED);
      }
      Serial.println("INFO: Fan mode set to HIGH (5) by Zigbee");
      break;
    case FAN_MODE_ON:
      ackReceived = tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, CommandType::FAN_SWITCH);
      Serial.println("INFO: Fan mode set to ON (4) by Zigbee");
      break;
    default:
      Serial.printf("ERROR: Unhandled fan mode: %d\n", mode);
      return;
  }

  if (!ackReceived) {
    Serial.printf("ERROR: MCU ACK timeout - rolling back fan mode to %d\n", lastConfirmedFanMode);
    isRollingBack = true;
    zbFanControl.setFanMode(lastConfirmedFanMode);
    isRollingBack = false;
    zbFanControl.reportFanMode();
  }
}

// Fan direction control callback function
void setFanDirection(uint8_t direction) {
  // Skip if we're rolling back - prevents infinite loop
  if (isRollingBack) {
    return;
  }

  // Zigbee state already updated when this callback is called
#ifdef __DEBUG__
  debugLogZigbeeMessage("Write", ZIGBEE_FAN_CONTROL_ENDPOINT, VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, direction);
#endif
  statusLed.flashCommand();

  // Check if MCU is responding - if not, log command and immediately roll back
  if (!tuya.isMcuResponding()) {
    Serial.printf("INFO: Fan direction set to %s (%d) by Zigbee\n",
      (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE", direction);

    Serial.printf("ERROR: MCU not responding - rolling back fan direction to %s (%d)\n",
      (lastConfirmedFanDirection == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE",
      lastConfirmedFanDirection);
    isRollingBack = true;
    zbFanControl.setFanDirection(lastConfirmedFanDirection);
    isRollingBack = false;
    zbFanControl.reportFanDirection();
    return;
  }

  // Send command with tracking
  bool ackReceived = tuya.sendDataPointWithTracking(DP_FAN_DIRECTION, DP_TYPE_ENUM, direction, CommandType::FAN_DIRECTION);

  Serial.printf("INFO: Fan direction set to %s (%d) by Zigbee\n",
    (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE", direction);

  if (!ackReceived) {
    Serial.printf("ERROR: MCU ACK timeout - rolling back fan direction to %s\n",
      (lastConfirmedFanDirection == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE");
    isRollingBack = true;
    zbFanControl.setFanDirection(lastConfirmedFanDirection);
    isRollingBack = false;
    zbFanControl.reportFanDirection();
  }
}

/********************* light control callback functions **************************/
#ifdef WITH_LIGHT
// Queue a light command (thread-safe from Zigbee callback)
bool queueLightCommand(const LightCommand& cmd) {
  if (lightQueueCount >= LIGHT_COMMAND_QUEUE_SIZE) {
    Serial.println("ERROR: Light command queue full - command dropped");
    return false;
  }
  lightCommandQueue[lightQueueHead] = cmd;
  lightQueueHead = (lightQueueHead + 1) % LIGHT_COMMAND_QUEUE_SIZE;
  lightQueueCount++;
  return true;
}

// Dequeue a light command
bool dequeueLightCommand(LightCommand& cmd) {
  if (lightQueueCount == 0) {
    return false;
  }
  cmd = lightCommandQueue[lightQueueTail];
  lightQueueTail = (lightQueueTail + 1) % LIGHT_COMMAND_QUEUE_SIZE;
  lightQueueCount--;
  return true;
}

// Clear the light command queue (used on rollback when MCU is unresponsive)
void clearLightCommandQueue() {
  lightQueueHead = 0;
  lightQueueTail = 0;
  lightQueueCount = 0;
}

// Check if pending command should be queued (debounce expired)
void checkPendingLightDebounce() {
  if (!hasPendingLight || isRollingBack) {
    return;
  }

  // Wait for debounce period (50ms) to let all attribute updates from one action settle
  if (millis() - pendingLightTime < 50) {
    return;
  }

  // Debounce complete - queue this command
#ifdef __DEBUG__
  Serial.printf("DEBUG: Debounce complete, queueing command (on=%d, level=%d, temp=%d)\n",
    pendingLightCmd.on, pendingLightCmd.level, pendingLightCmd.colourTempMired);
#endif
  queueLightCommand(pendingLightCmd);
  hasPendingLight = false;
}

// Process the next light command from queue (called from loop)
void processPendingLightCommand() {
  // First, check if any pending command has debounced and should be queued
  checkPendingLightDebounce();

  // Don't process queue if already processing (prevents re-entry) or rolling back
  if (isProcessingLightQueue || isRollingBack) {
    return;
  }

  // Get next command from queue
  LightCommand cmd;
  if (!dequeueLightCommand(cmd)) {
    return;  // Queue empty
  }

#ifdef __DEBUG__
  Serial.printf("DEBUG: Processing queued command (on=%d, level=%d, temp=%d), remaining=%d\n",
    cmd.on, cmd.level, cmd.colourTempMired, lightQueueCount);
#endif

  isProcessingLightQueue = true;
  statusLed.flashCommand();

  // Check if MCU is responding - if not, log command and immediately roll back
  if (!tuya.isMcuResponding()) {
    // Log the command that was received
    Serial.printf("INFO: Light set to %s, level %d, temp %d mired (%dK) by Zigbee\n",
      cmd.on ? "ON" : "OFF", cmd.level, cmd.colourTempMired, miredToKelvin(cmd.colourTempMired));

    // Immediately roll back to last confirmed state
    Serial.printf("ERROR: MCU not responding - rolling back light to %s, level %d, temp %d mired\n",
      lastConfirmedLightState ? "ON" : "OFF", lastConfirmedLightBrightness, lastConfirmedLightColorTemp);

    isRollingBack = true;
    zbLight.setLightState(lastConfirmedLightState);
    zbLight.setLightLevel(lastConfirmedLightBrightness);
    zbLight.setLightColorTemperature(lastConfirmedLightColorTemp);
    isRollingBack = false;
    zbLight.reportAllAttributes();

    // Clear the queue - no point sending more commands to unresponsive MCU
    clearLightCommandQueue();
    isProcessingLightQueue = false;
    return;
  }

#ifdef __DEBUG__
  debugLogZigbeeMessage("Write", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, cmd.on ? 1 : 0);
  debugLogZigbeeMessage("Write", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, cmd.level);
  debugLogZigbeeMessage("Write", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, cmd.colourTempMired);
#endif

#ifdef __DEBUG__
  Serial.println("DEBUG: Sending DP_LIGHT_SWITCH...");
#endif
  // Send switch command - only track this one command for rollback purposes
  bool ackReceived = tuya.sendDataPointWithTracking(DP_LIGHT_SWITCH, DP_TYPE_BOOL, cmd.on ? 1 : 0, CommandType::LIGHT_SWITCH);
#ifdef __DEBUG__
  Serial.printf("DEBUG: DP_LIGHT_SWITCH %s\n", ackReceived ? "ACK received" : "ACK timeout");
#endif

  // If ACK failed, log, rollback Zigbee state, and return
  // mcuNotResponding flag is now set - next command will see it
  if (!ackReceived) {
    Serial.printf("INFO: Light set to %s, level %d, temp %d mired (%dK) by Zigbee\n",
      cmd.on ? "ON" : "OFF", cmd.level, cmd.colourTempMired, miredToKelvin(cmd.colourTempMired));
    Serial.printf("ERROR: MCU ACK timeout - rolling back light to %s, level %d, temp %d mired\n",
      lastConfirmedLightState ? "ON" : "OFF", lastConfirmedLightBrightness, lastConfirmedLightColorTemp);

    // Rollback Zigbee state to inform coordinator
    isRollingBack = true;
    zbLight.setLightState(lastConfirmedLightState);
    zbLight.setLightLevel(lastConfirmedLightBrightness);
    zbLight.setLightColorTemperature(lastConfirmedLightColorTemp);
    isRollingBack = false;
    zbLight.reportAllAttributes();

    // Clear the queue - no point sending more commands to unresponsive MCU
    clearLightCommandQueue();
    isProcessingLightQueue = false;
    return;
  }

  if (cmd.on) {
    // Convert Zigbee brightness (0-254) to Tuya brightness (0-5)
    uint8_t tuyaBrightness = zigbeeBrightnessToTuya(cmd.level);
#ifdef __DEBUG__
    Serial.printf("DEBUG: Sending DP_LIGHT_DIMMER (tuya=%d)...\n", tuyaBrightness);
#endif
    // Send brightness without tracking (switch command tracks the overall operation)
    if (!tuya.sendDataPoint(DP_LIGHT_DIMMER, DP_TYPE_VALUE, tuyaBrightness)) {
      // ACK failed for brightness - log, rollback, and stop
      Serial.printf("INFO: Light set to %s, level %d, temp %d mired (%dK) by Zigbee\n",
        cmd.on ? "ON" : "OFF", cmd.level, cmd.colourTempMired, miredToKelvin(cmd.colourTempMired));
      Serial.printf("ERROR: MCU ACK timeout - rolling back light to %s, level %d, temp %d mired\n",
        lastConfirmedLightState ? "ON" : "OFF", lastConfirmedLightBrightness, lastConfirmedLightColorTemp);

      isRollingBack = true;
      zbLight.setLightState(lastConfirmedLightState);
      zbLight.setLightLevel(lastConfirmedLightBrightness);
      zbLight.setLightColorTemperature(lastConfirmedLightColorTemp);
      isRollingBack = false;
      zbLight.reportAllAttributes();

      clearLightCommandQueue();
      isProcessingLightQueue = false;
      return;
    }
#ifdef __DEBUG__
    Serial.println("DEBUG: DP_LIGHT_DIMMER ACK received");
#endif

    // Convert mired to Tuya colour temp values
    ColourTempLevel tuyaColourTemp = miredToTuyaColourTemp(cmd.colourTempMired);
#ifdef __DEBUG__
    Serial.printf("DEBUG: Sending DP_LIGHT_COLOUR_TEMP (tuya=%d)...\n", static_cast<uint8_t>(tuyaColourTemp));
#endif
    if (!tuya.sendDataPoint(DP_LIGHT_COLOUR_TEMP, DP_TYPE_ENUM, static_cast<uint8_t>(tuyaColourTemp))) {
      // ACK failed for colour temp - log, rollback, and stop
      Serial.printf("INFO: Light set to %s, level %d, temp %d mired (%dK) by Zigbee\n",
        cmd.on ? "ON" : "OFF", cmd.level, cmd.colourTempMired, miredToKelvin(cmd.colourTempMired));
      Serial.printf("ERROR: MCU ACK timeout - rolling back light to %s, level %d, temp %d mired\n",
        lastConfirmedLightState ? "ON" : "OFF", lastConfirmedLightBrightness, lastConfirmedLightColorTemp);

      isRollingBack = true;
      zbLight.setLightState(lastConfirmedLightState);
      zbLight.setLightLevel(lastConfirmedLightBrightness);
      zbLight.setLightColorTemperature(lastConfirmedLightColorTemp);
      isRollingBack = false;
      zbLight.reportAllAttributes();

      clearLightCommandQueue();
      isProcessingLightQueue = false;
      return;
    }
#ifdef __DEBUG__
    Serial.println("DEBUG: DP_LIGHT_COLOUR_TEMP ACK received");
#endif
  }

  Serial.printf("INFO: Light set to %s, level %d, temp %d mired (%dK) by Zigbee\n",
    cmd.on ? "ON" : "OFF", cmd.level, cmd.colourTempMired, miredToKelvin(cmd.colourTempMired));

  isProcessingLightQueue = false;
}

void setLight(bool on, uint8_t level, uint16_t colourTempMired) {
  // Skip if we're rolling back - prevents infinite loop
  if (isRollingBack) {
#ifdef __DEBUG__
    Serial.println("DEBUG: setLight() skipped - rolling back");
#endif
    return;
  }

  // Skip the first callback which is triggered by initialization
  // (setLightColorTemperature() call in setup triggers this with stale values)
  if (!lightCallbackInitialized) {
    lightCallbackInitialized = true;
#ifdef __DEBUG__
    Serial.println("DEBUG: setLight() skipped - first init callback");
#endif
    return;
  }

#ifdef __DEBUG__
  Serial.printf("DEBUG: setLight(on=%d, level=%d, temp=%d) called\n", on, level, colourTempMired);
#endif

  unsigned long now = millis();

  // If we have a pending command and it's been more than 50ms since the last update,
  // the previous action has finished its callback burst - queue it before starting new one
  if (hasPendingLight && (now - pendingLightTime > 50)) {
#ifdef __DEBUG__
    Serial.println("DEBUG: Queueing previous pending command (>50ms elapsed)");
#endif
    queueLightCommand(pendingLightCmd);
    hasPendingLight = false;
  }

  // Update pending command (coalesces callbacks from same action within 50ms window)
  pendingLightCmd.on = on;
  pendingLightCmd.level = level;
  pendingLightCmd.colourTempMired = colourTempMired;
  pendingLightTime = now;
  hasPendingLight = true;
#ifdef __DEBUG__
  Serial.printf("DEBUG: Pending command updated, hasPendingLight=%d, queueCount=%d\n", hasPendingLight, lightQueueCount);
#endif
}
#endif

/********************* individual device status handlers **************************/

// Handle fan switch status updates from MCU
void handleFanSwitchStatus(uint32_t value) {
  bool fanOn = (value != 0);
  if (!zbFanControl.setFanState(fanOn)) {
    Serial.printf("ERROR: Failed to update Zigbee fan switch status: %s\n", fanOn ? "ON" : "OFF");
  } else {
#ifdef __DEBUG__
    debugLogZigbeeMessage("Read", ZIGBEE_FAN_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, fanOn ? FAN_MODE_ON : FAN_MODE_OFF);
#endif
  }

  // Update confirmed state
  lastConfirmedFanMode = fanOn ? FAN_MODE_ON : FAN_MODE_OFF;

  Serial.printf("INFO: Fan switch set to %s (%d) by Skyfan\n", fanOn ? "ON" : "OFF", fanOn ? 1 : 0);
}

// Handle fan speed status updates from MCU
void handleFanSpeedStatus(uint32_t value) {
  uint8_t speed = static_cast<uint8_t>(value);
  if (isValidTuyaFanSpeed(speed)) {
    if (!zbFanControl.setFanSpeed(speed)) {
      Serial.printf("ERROR: Failed to update Zigbee fan speed status: %d\n", speed);
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

    Serial.printf("INFO: Fan speed set to %d by Skyfan\n", speed);
  } else {
    Serial.printf("ERROR: Invalid fan speed status received: %d\n", speed);
  }
}

// Handle fan mode status updates from MCU (MCU-only, not exposed to Zigbee)
void handleFanModeStatus(uint32_t value) {
  uint8_t mode = static_cast<uint8_t>(value);
  if (mode <= static_cast<uint8_t>(TuyaFanMode::SLEEP)) {
    const char* modeName = (mode == static_cast<uint8_t>(TuyaFanMode::NORMAL)) ? "NORMAL" :
                           (mode == static_cast<uint8_t>(TuyaFanMode::ECO)) ? "ECO" : "SLEEP";
    Serial.printf("INFO: Fan MCU mode set to %s (%d) by Skyfan\n", modeName, mode);
  } else {
    Serial.printf("ERROR: Invalid fan mode status received: %d\n", mode);
  }
}

// Handle fan direction status updates from MCU
void handleFanDirectionStatus(uint32_t value) {
  uint8_t direction = static_cast<uint8_t>(value);
  if (direction <= static_cast<uint8_t>(FanDirection::REVERSE)) {
    // Update custom manufacturer attribute for fan direction
    if (!zbFanControl.setFanDirection(direction)) {
      Serial.printf("ERROR: Failed to update Zigbee fan direction status: %d\n", direction);
    } else {
#ifdef __DEBUG__
      debugLogZigbeeMessage("Read", ZIGBEE_FAN_CONTROL_ENDPOINT, VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, direction);
#endif
    }

    // Update confirmed state
    lastConfirmedFanDirection = direction;

    Serial.printf("INFO: Fan direction set to %s (%d) by Skyfan\n",
      (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE", direction);
  } else {
    Serial.printf("ERROR: Invalid fan direction status received: %d\n", direction);
  }
}

#ifdef WITH_LIGHT
// Handle light switch status updates from MCU
void handleLightSwitchStatus(uint32_t value) {
  bool lightOn = (value != 0);
  if (!zbLight.setLightState(lightOn)) {
    Serial.printf("ERROR: Failed to update Zigbee light switch status: %s\n", lightOn ? "ON" : "OFF");
  } else {
#ifdef __DEBUG__
    debugLogZigbeeMessage("Read", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, lightOn ? 1 : 0);
#endif
  }

  // Update confirmed state
  lastConfirmedLightState = lightOn;

  Serial.printf("INFO: Light switch set to %s (%d) by Skyfan\n", lightOn ? "ON" : "OFF", lightOn ? 1 : 0);
}

// Handle light brightness status updates from MCU
void handleLightBrightnessStatus(uint32_t value) {
  uint8_t tuyaBrightness = static_cast<uint8_t>(value);

  if (isValidTuyaBrightness(tuyaBrightness)) {
    uint8_t zigbeeBrightness = tuyaBrightnessToZigbee(tuyaBrightness);
    if (!zbLight.setLightLevel(zigbeeBrightness)) {
      Serial.printf("ERROR: Failed to update Zigbee light brightness: %d\n", zigbeeBrightness);
    } else {
#ifdef __DEBUG__
      debugLogZigbeeMessage("Read", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, zigbeeBrightness);
#endif
    }

    // Update confirmed state
    lastConfirmedLightBrightness = zigbeeBrightness;

    Serial.printf("INFO: Light brightness set to %d (Zigbee: %d) by Skyfan\n", tuyaBrightness, zigbeeBrightness);
  } else {
    Serial.printf("ERROR: Invalid light brightness status received: %d\n", tuyaBrightness);
  }
}

// Handle light colour temperature status updates from MCU
void handleLightColourTempStatus(uint32_t value) {
  uint8_t colourTempValue = static_cast<uint8_t>(value);

  if (colourTempValue <= static_cast<uint8_t>(ColourTempLevel::COOL)) {
    ColourTempLevel colourLevel = static_cast<ColourTempLevel>(colourTempValue);
    uint16_t colourTempMired = tuyaColourTempToMired(colourLevel);

    if (!zbLight.setLightColorTemperature(colourTempMired)) {
      Serial.printf("ERROR: Failed to update Zigbee light colour temperature: %d mired\n", colourTempMired);
    } else {
#ifdef __DEBUG__
      debugLogZigbeeMessage("Read", ZIGBEE_LIGHT_CONTROL_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, colourTempMired);
#endif
    }

    // Update confirmed state
    lastConfirmedLightColorTemp = colourTempMired;

    Serial.printf("INFO: Light colour temp set to %d mired (%dK) by Skyfan\n", colourTempMired, miredToKelvin(colourTempMired));
  } else {
    Serial.printf("ERROR: Invalid light colour temperature status received: %d\n", colourTempValue);
  }
}
#endif

// Handle unknown/unsupported status updates from MCU
void handleUnknownStatus(uint8_t dpid, uint32_t value) {
#ifdef __DEBUG__
  Serial.printf("DEBUG: Unknown status update - DPID: %d, Value: %lu\n", dpid, value);
#endif
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
  // Set flag to prevent callback re-triggering during rollback
  isRollingBack = true;

  switch (type) {
    case CommandType::FAN_SWITCH:
    case CommandType::FAN_SPEED:
      Serial.printf("ERROR: Failed to set fan mode by Zigbee - no response from Skyfan. Rolling back to %d\n", lastConfirmedFanMode);
      zbFanControl.setFanMode(lastConfirmedFanMode);
      isRollingBack = false;
      zbFanControl.reportFanMode();
      return;

    case CommandType::FAN_DIRECTION:
      Serial.printf("ERROR: Failed to set fan direction by Zigbee - no response from Skyfan. Rolling back to %s (%d)\n",
        (lastConfirmedFanDirection == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE", lastConfirmedFanDirection);
      zbFanControl.setFanDirection(lastConfirmedFanDirection);
      isRollingBack = false;
      zbFanControl.reportFanDirection();
      return;

#ifdef WITH_LIGHT
    case CommandType::LIGHT_SWITCH:
      // Clear the command queue - MCU isn't responding, queued commands will also fail
      // But keep hasPendingLight - if a new command arrives after rollback, we want it
      clearLightCommandQueue();
      // Rollback all light attributes to last confirmed state
      Serial.printf("ERROR: Failed to set light by Zigbee - no response from Skyfan. Rolling back to %s, level %d, temp %d mired\n",
        lastConfirmedLightState ? "ON" : "OFF", lastConfirmedLightBrightness, lastConfirmedLightColorTemp);
      zbLight.setLightState(lastConfirmedLightState);
      zbLight.setLightLevel(lastConfirmedLightBrightness);
      zbLight.setLightColorTemperature(lastConfirmedLightColorTemp);
      isRollingBack = false;
      zbLight.reportAllAttributes();
      return;

    // These cases are no longer used (only LIGHT_SWITCH is tracked) but kept for safety
    case CommandType::LIGHT_BRIGHTNESS:
    case CommandType::LIGHT_COLOR_TEMP:
      break;
#endif
  }

  isRollingBack = false;
}

/********************* OTA callback functions **************************/
void otaStateCallback(bool otaActive) {
  otaRunning = otaActive;
  if (otaActive) {
    Serial.println("INFO: OTA update started - do not power off device");
    statusLed.setStatus(LedStatus::INITIALISING);  // Use blinking LED during OTA
  } else {
    Serial.println("INFO: OTA update finished - device will reboot");
  }
}

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUD_RATE);  // USB Serial for debug output
  tuya.begin(MCU_SERIAL_BAUD_RATE);
  tuya.setDeviceStatusCallback(onDeviceStatus);
  tuya.setRollbackCallback(onCommandRollback);
  Serial.println("INFO: Skyfan Zigbee Controller starting...");

  // Factory reset button is initialized in constructor

  // Set Zigbee device name and model
  zbFanControl.setManufacturerAndModel(ZIGBEE_DEVICE_MANUFACTURER, ZIGBEE_MODEL_NAME);

  // Configure device power source as mains-powered (not battery)
  zbFanControl.setPowerSource(ZB_POWER_SOURCE_MAINS);
  //zbLight.setPowerSource(ZB_POWER_SOURCE_MAINS);
#ifdef __DEBUG__
  Serial.println("DEBUG: Device configured as mains-powered");
#endif

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
    Serial.println("ERROR: Failed to create custom cluster - rebooting");
    cleanupAllResources();
    ESP.restart();
  }

  // Enable attribute reporting for fan mode and fan direction
  // This must be done after clusters are created but before Zigbee.begin()
  if (!zbFanControl.enableAttributeReporting()) {
    Serial.println("ERROR: Some attributes may not support reporting");
  }

  // Add OTA client to fan endpoint for over-the-air firmware updates
  if (zbFanControl.addOTAClient(OTA_FILE_VERSION, OTA_DOWNLOADED_FILE_VERSION, OTA_HW_VERSION,
                                 OTA_MANUFACTURER_CODE, OTA_IMAGE_TYPE)) {
#ifdef __DEBUG__
    Serial.printf("DEBUG: OTA client added (version: 0x%08lX, manufacturer: 0x%04X, image type: 0x%04X)\n",
                  (unsigned long)OTA_FILE_VERSION, OTA_MANUFACTURER_CODE, OTA_IMAGE_TYPE);
#endif
    zbFanControl.onOTAStateChange(otaStateCallback);
  } else {
    Serial.println("ERROR: Failed to add OTA client - OTA updates will not be available");
  }

  // Add endpoints to Zigbee Core
#ifdef __DEBUG__
  Serial.println("DEBUG: Adding ZigbeeFanControl endpoint to Zigbee Core");
#endif
  Zigbee.addEndpoint(&zbFanControl);
#ifdef WITH_LIGHT
#ifdef __DEBUG__
  Serial.println("DEBUG: Adding ZigbeeLight endpoint to Zigbee Core");
#endif
  Zigbee.addEndpoint(&zbLight);
#endif

  // When all EPs are registered, start Zigbee in ROUTER mode
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Serial.println("ERROR: Zigbee failed to start - rebooting");
    cleanupAllResources();
    ESP.restart();
  }

  Serial.println("INFO: Connecting to network (hold BOOT 3s to factory reset)");
  while (!Zigbee.connected()) {
    Serial.print(".");
    delay(ZIGBEE_CONNECTION_POLL_MS);

    // Check for factory reset during connection attempt
    factoryResetButton.update();
    if (factoryResetButton.wasLongPressed()) {
      Serial.println("\nINFO: Factory reset requested during connection");
      cleanupAllResources();
      delay(FACTORY_RESET_DELAY_MS);
      Zigbee.factoryReset();
    }
  }
  Serial.println();
  Serial.println("INFO: Zigbee connected successfully");

#ifdef WITH_LIGHT
  // Initialize color temperature to set color mode to TEMPERATURE
  // This ensures the temp callback is called for on/off and level changes
  zbLight.setLightColorTemperature(lastConfirmedLightColorTemp);
#endif

  // Start OTA client query - first request is within a minute, then hourly automatically
  zbFanControl.requestOTAUpdate();
#ifdef __DEBUG__
  Serial.println("DEBUG: OTA update check scheduled");
#endif
}

void loop() {
  // Update Tuya protocol (handles responses, heartbeat, connection status)
  tuya.update(Zigbee.connected());

  // Check for command timeouts (1.5s status response timeout)
  tuya.checkPendingCommandTimeouts();

#ifdef WITH_LIGHT
  // Process debounced light commands
  processPendingLightCommand();
#endif
  
  // Update button state (non-blocking)
  factoryResetButton.update();
  
  // Update LED status based on Zigbee state
  updateLedStatus();
  statusLed.update();
  
  // Check for factory reset long press (blocked during OTA)
  if (factoryResetButton.wasLongPressed()) {
    if (otaRunning) {
      Serial.println("ERROR: OTA in progress - factory reset blocked");
    } else {
      Serial.println("INFO: Factory reset requested - rebooting in 1s");
      cleanupAllResources();
      delay(FACTORY_RESET_DELAY_MS);
      Zigbee.factoryReset();
    }
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

/********************* utility functions **************************/
// Global cleanup function for all allocated resources
void cleanupAllResources() {
#ifdef __DEBUG__
  Serial.println("DEBUG: Cleaning up all allocated resources");
#endif
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
