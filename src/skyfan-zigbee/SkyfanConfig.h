/*
 * Skyfan Configuration - Centralized configuration constants and enums
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

#ifndef SKYFAN_CONFIG_H
#define SKYFAN_CONFIG_H

#include <Arduino.h>

// === Hardware Configuration ===
#define FACTORY_RESET_BUTTON_PIN   BOOT_PIN
#define DEBUG_SERIAL_RX_PIN        21
#define DEBUG_SERIAL_TX_PIN        20
#define DEBUG_SERIAL_BAUD_RATE     115200
#define MCU_SERIAL_BAUD_RATE       115200

// === Zigbee Configuration ===
#define ZIGBEE_FAN_CONTROL_ENDPOINT    1
#define ZIGBEE_LIGHT_CONTROL_ENDPOINT  2
#define ZIGBEE_DEVICE_MANUFACTURER     "Ventair"
#define ZIGBEE_FAN_MODEL_NAME          "Skyfan"
#define ZIGBEE_LIGHT_MODEL_NAME        "Skyfan Light"

// === Timing Configuration ===
#define TUYA_HEARTBEAT_INTERVAL_MS     10000  // 10 seconds
#define TUYA_CONNECTION_TIMEOUT_MS     30000  // 30 seconds
#define TUYA_RESPONSE_TIMEOUT_MS       1000   // 1 second
#define TUYA_COMMAND_TIMEOUT_MS        500    // 0.5 seconds
#define FACTORY_RESET_HOLD_TIME_MS     3000   // 3 seconds
#define BUTTON_DEBOUNCE_DELAY_MS       100    // 100ms
#define BUTTON_POLL_DELAY_MS           50     // 50ms
#define MAIN_LOOP_DELAY_MS             100    // 100ms
#define ZIGBEE_CONNECTION_POLL_MS      100    // 100ms
#define FACTORY_RESET_DELAY_MS         1000   // 1 second

// === Colour Temperature Configuration ===
// Kelvin values for each temperature setting
#define COLOUR_TEMP_WARM_KELVIN         3000
#define COLOUR_TEMP_NATURAL_KELVIN      4200
#define COLOUR_TEMP_COOL_KELVIN         6500

// Corresponding mired values (1,000,000 / Kelvin)
#define COLOUR_TEMP_WARM_MIRED          333    // 1000000/3000
#define COLOUR_TEMP_NATURAL_MIRED       238    // 1000000/4200
#define COLOUR_TEMP_COOL_MIRED          154    // 1000000/6500

// Mired range for Zigbee colour temperature capability
#define ZIGBEE_COLOUR_TEMP_MIN_MIRED    COLOUR_TEMP_COOL_MIRED
#define ZIGBEE_COLOUR_TEMP_MAX_MIRED    COLOUR_TEMP_WARM_MIRED

// === Range Configuration ===
#define TUYA_BRIGHTNESS_MIN            0
#define TUYA_BRIGHTNESS_MAX            5
#define TUYA_FAN_SPEED_MIN             0      // Integer range for MCU
#define TUYA_FAN_SPEED_MAX             5      // Integer range for MCU
#define ZIGBEE_BRIGHTNESS_MIN          0
#define ZIGBEE_BRIGHTNESS_MAX          254

// === Fan Speed Mapping (Integer values for Tuya MCU) ===
#define FAN_SPEED_LOW_TUYA             1      // Integer value sent to MCU
#define FAN_SPEED_MEDIUM_TUYA          3      // Integer value sent to MCU
#define FAN_SPEED_HIGH_TUYA            5      // Integer value sent to MCU

// === Buffer Configuration ===
#define TUYA_BUFFER_SIZE               256
#define TUYA_RX_BUFFER_SIZE            256

// === Enhanced Enums ===

// Colour temperature levels with clear naming
enum class ColourTempLevel : uint8_t {
  WARM = 0,      // 3000K
  NATURAL = 1,   // 4200K
  COOL = 2       // 6500K
};

// Fan mode for MCU-specific operations (enum values sent to MCU)
enum class TuyaFanMode : uint8_t {
  NORMAL = 0,
  ECO = 1,
  SLEEP = 2
};

// Fan direction (enum values sent to MCU)
enum class FanDirection : uint8_t {
  FORWARD = 0,
  REVERSE = 1
};

// Protocol states for better state machine readability
enum class TuyaProtocolState : uint8_t {
  WAIT_HEADER_1 = 0,
  WAIT_HEADER_2 = 1,
  WAIT_VERSION = 2,
  WAIT_COMMAND = 3,
  WAIT_LENGTH_HIGH = 4,
  WAIT_LENGTH_LOW = 5,
  WAIT_DATA_AND_CHECKSUM = 6
};

// === Utility Functions ===

// Convert Kelvin to Mired
inline constexpr uint16_t kelvinToMired(uint16_t kelvin) {
  return (kelvin > 0) ? (1000000 / kelvin) : 0;
}

// Convert Mired to Kelvin
inline constexpr uint16_t miredToKelvin(uint16_t mired) {
  return (mired > 0) ? (1000000 / mired) : 0;
}

// Check if value is within range
template<typename T>
inline constexpr bool isInRange(T value, T min, T max) {
  return (value >= min) && (value <= max);
}

// Clamp value to range
template<typename T>
inline constexpr T clamp(T value, T min, T max) {
  return (value < min) ? min : ((value > max) ? max : value);
}

// Validate Tuya fan speed (0-5 integer range)
inline bool isValidTuyaFanSpeed(uint8_t speed) {
  return isInRange(speed, static_cast<uint8_t>(TUYA_FAN_SPEED_MIN), static_cast<uint8_t>(TUYA_FAN_SPEED_MAX));
}

// Validate Tuya brightness (0-5 integer range)
inline bool isValidTuyaBrightness(uint8_t brightness) {
  return isInRange(brightness, static_cast<uint8_t>(TUYA_BRIGHTNESS_MIN), static_cast<uint8_t>(TUYA_BRIGHTNESS_MAX));
}

// === Colour Temperature Conversion Functions ===

// Convert Kelvin to appropriate Tuya colour temperature enum
inline ColourTempLevel kelvinToTuyaColourTemp(uint16_t kelvin) {
  if (kelvin <= (COLOUR_TEMP_WARM_KELVIN + COLOUR_TEMP_NATURAL_KELVIN) / 2) {
    return ColourTempLevel::WARM;
  } else if (kelvin <= (COLOUR_TEMP_NATURAL_KELVIN + COLOUR_TEMP_COOL_KELVIN) / 2) {
    return ColourTempLevel::NATURAL;
  } else {
    return ColourTempLevel::COOL;
  }
}

// Convert Mired to appropriate Tuya colour temperature enum  
inline ColourTempLevel miredToTuyaColourTemp(uint16_t mired) {
  return kelvinToTuyaColourTemp(miredToKelvin(mired));
}

// Convert Tuya colour temperature enum to Mired
inline uint16_t tuyaColourTempToMired(ColourTempLevel colourTemp) {
  switch (colourTemp) {
    case ColourTempLevel::WARM:
      return COLOUR_TEMP_WARM_MIRED;
    case ColourTempLevel::NATURAL:
      return COLOUR_TEMP_NATURAL_MIRED;
    case ColourTempLevel::COOL:
      return COLOUR_TEMP_COOL_MIRED;
    default:
      return COLOUR_TEMP_WARM_MIRED;  // Default to warm
  }
}

// Convert Tuya colour temperature enum to Kelvin
inline uint16_t tuyaColourTempToKelvin(ColourTempLevel colourTemp) {
  switch (colourTemp) {
    case ColourTempLevel::WARM:
      return COLOUR_TEMP_WARM_KELVIN;
    case ColourTempLevel::NATURAL:
      return COLOUR_TEMP_NATURAL_KELVIN;
    case ColourTempLevel::COOL:
      return COLOUR_TEMP_COOL_KELVIN;
    default:
      return COLOUR_TEMP_WARM_KELVIN;  // Default to warm
  }
}

// === Range Mapping Functions ===

// Map Zigbee brightness to Tuya brightness with validation
inline uint8_t zigbeeBrightnessToTuya(uint8_t zigbeeBrightness) {
  uint8_t clamped = clamp(zigbeeBrightness, static_cast<uint8_t>(ZIGBEE_BRIGHTNESS_MIN), static_cast<uint8_t>(ZIGBEE_BRIGHTNESS_MAX));
  return map(clamped, ZIGBEE_BRIGHTNESS_MIN, ZIGBEE_BRIGHTNESS_MAX, TUYA_BRIGHTNESS_MIN, TUYA_BRIGHTNESS_MAX);
}

// Map Tuya brightness to Zigbee brightness with validation
inline uint8_t tuyaBrightnessToZigbee(uint8_t tuyaBrightness) {
  uint8_t clamped = clamp(tuyaBrightness, static_cast<uint8_t>(TUYA_BRIGHTNESS_MIN), static_cast<uint8_t>(TUYA_BRIGHTNESS_MAX));
  return map(clamped, TUYA_BRIGHTNESS_MIN, TUYA_BRIGHTNESS_MAX, ZIGBEE_BRIGHTNESS_MIN, ZIGBEE_BRIGHTNESS_MAX);
}

// === Non-blocking Button Debounce Class ===

class DebouncedButton {
private:
  uint8_t pin;
  unsigned long lastStateChange;
  unsigned long lastPressTime;
  bool lastState;
  bool currentState;
  bool pressed;
  bool longPressed;
  unsigned long debounceDelay;
  unsigned long longPressDelay;

public:
  DebouncedButton(uint8_t buttonPin, unsigned long debounceMs = BUTTON_DEBOUNCE_DELAY_MS, unsigned long longPressMs = FACTORY_RESET_HOLD_TIME_MS) 
    : pin(buttonPin), lastStateChange(0), lastPressTime(0), lastState(HIGH), currentState(HIGH), 
      pressed(false), longPressed(false), debounceDelay(debounceMs), longPressDelay(longPressMs) {
    pinMode(pin, INPUT_PULLUP);
  }

  // Call this in loop() to update button state
  void update() {
    bool reading = digitalRead(pin);
    
    // Reset debouncing timer if state changed
    if (reading != lastState) {
      lastStateChange = millis();
    }
    
    // State has been stable long enough to be considered valid
    if ((millis() - lastStateChange) > debounceDelay) {
      // State has actually changed
      if (reading != currentState) {
        currentState = reading;
        
        if (currentState == LOW) {  // Button pressed (active low with pullup)
          lastPressTime = millis();
          pressed = true;
          longPressed = false;
        }
      }
      
      // Check for long press while button is held
      if (currentState == LOW && !longPressed) {
        if ((millis() - lastPressTime) > longPressDelay) {
          longPressed = true;
        }
      }
      
      // Reset pressed flag when button released
      if (currentState == HIGH && pressed) {
        pressed = false;
      }
    }
    
    lastState = reading;
  }
  
  // Check if button was just pressed (single shot)
  bool wasPressed() {
    if (pressed && currentState == HIGH) {  // Just released after being pressed
      pressed = false;
      return !longPressed;  // Only return true if it wasn't a long press
    }
    return false;
  }
  
  // Check if button was long pressed (single shot)
  bool wasLongPressed() {
    if (longPressed && currentState == HIGH) {  // Just released after long press
      longPressed = false;
      return true;
    }
    return false;
  }
  
  // Check if button is currently being long pressed
  bool isLongPressed() const {
    return longPressed && currentState == LOW;
  }
  
  // Check if button is currently pressed
  bool isPressed() const {
    return currentState == LOW;
  }
};

#endif // SKYFAN_CONFIG_H