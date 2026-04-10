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

// === Feature Configuration ===
#define WITH_LIGHT  // Comment out to disable light functionality
// #define __DEBUG__   // Comment out to disable debug logging
#define __BOOT_LOG__  // Comment out to disable boot logging persistence

// === Firmware Version ===
// Injected at build time by CI; fallback for local builds
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev-local"
#endif

// === OTA Configuration ===
// OTA file version format: 0xRRRRRDDD (32-bit)
//   - Upper 20 bits (RRRRR): Tagged releases (>= 0x00001000)
//   - Lower 12 bits (DDD): Dev builds (1-4095)
//
// Release encoding (5 bits each in upper 20 bits):
//   - Major: bits 27-31 (0-31), Minor: bits 22-26 (0-31)
//   - Patch: bits 17-21 (0-31), Prerelease: bits 12-16 (alpha=1-10, beta=12-21, rc=23-30, stable=31)
//
// Examples: dev build 50 = 0x00000032, v0.0.4-alpha.1 = 0x00081000, v1.0.0 = 0x0800F000
#ifndef OTA_FILE_VERSION
#define OTA_FILE_VERSION 0x00000001  // Fallback for local builds (dev build 1)
#endif
#define OTA_DOWNLOADED_FILE_VERSION OTA_FILE_VERSION  // Initially same as running version
#define OTA_HW_VERSION              0x0001            // Hardware revision (increment for breaking HW changes)
#define OTA_MANUFACTURER_CODE       0x1818                 // Front Left Speaker (same as FLS_MANUFACTURER_CODE)
#define OTA_IMAGE_TYPE              0x0001            // Image type identifier

// === Hardware Configuration ===
#define FACTORY_RESET_BUTTON_PIN   BOOT_PIN
#define DEBUG_SERIAL_RX_PIN        21
#define DEBUG_SERIAL_TX_PIN        20
#define DEBUG_SERIAL_BAUD_RATE     115200

// === Zigbee Configuration ===
#define ZIGBEE_FAN_CONTROL_ENDPOINT    1
#ifdef WITH_LIGHT
#define ZIGBEE_LIGHT_CONTROL_ENDPOINT  2
#endif
#define ZIGBEE_DEVICE_MANUFACTURER     "Front Left Speaker"
#ifdef WITH_LIGHT
#define ZIGBEE_MODEL_NAME          "Ventair Skyfan/Light ZB Adaptor"
#else
#define ZIGBEE_MODEL_NAME          "Ventair Skyfan ZB Adaptor"
#endif

// === Custom Cluster Configuration ===
#define VENTAIR_CUSTOM_CLUSTER_ID      0xFC00  // Custom cluster ID. Made up but must be > ESP_ZB_CUSTOM_CLUSTER_ID_MIN_VAL
#define FLS_MANUFACTURER_CODE          0x1818  // Front Left Speaker manufacturer code. Again made up. Meant to be registered
#define CUSTOM_ATTR_FAN_DIRECTION      0x0001  // Custom attribute ID for fan direction

// === ZCL Frame Control Bytes (for raw APS reporting) ===
#define ZCL_FRAME_CTRL_GLOBAL_TO_CLIENT          0x18  // Global cmd, server-to-client, disable default response
#define ZCL_FRAME_CTRL_GLOBAL_TO_CLIENT_MANUF    0x1C  // Same as above + manufacturer specific
#define ZCL_CMD_REPORT_ATTRIBUTES                0x0A  // Report Attributes command ID

// === Timing Configuration ===
#define TUYA_HEARTBEAT_INTERVAL_MS     10000  // 10 seconds
#define TUYA_CONNECTION_TIMEOUT_MS     30000  // 30 seconds
#define TUYA_COMMAND_TIMEOUT_MS        500    // 0.5 seconds - ACK timeout
#define TUYA_STATUS_RESPONSE_TIMEOUT_MS 1500  // 1.5 seconds - Status report timeout per Tuya spec
#define MCU_NOT_RESPONDING_BYPASS_MS   2000   // 2 seconds - bypass Tuya after ACK timeout
#define ZIGBEE_CONNECTION_POLL_MS      100    // 100ms
#define FACTORY_RESET_DELAY_MS         1000   // 1 second

// === Baud Rate Negotiation ===
#define BAUD_RATE_PRIMARY              9600     // Try first (per Tuya spec)
#define BAUD_RATE_SECONDARY            115200   // Try second (also used as fallback)
#define BAUD_NEGOTIATION_INTERVAL_MS   1000     // 1 second between queries (per Tuya spec)
#define BAUD_NEGOTIATION_MAX_CYCLES    5        // Max full cycles (each tries both rates)
// Note: Uses TUYA_COMMAND_TIMEOUT_MS (500ms) for response timeout per Tuya spec


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

// === Custom Cluster Role ===
enum class CustomClusterRole : uint8_t {
  SERVER = 0,
  CLIENT = 1
};

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



#endif // SKYFAN_CONFIG_H