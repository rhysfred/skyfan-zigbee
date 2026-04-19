/*
 * Persisted Properties - NVS storage wrapper with in-memory cache
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

#ifndef PERSISTED_PROPERTIES_H
#define PERSISTED_PROPERTIES_H

#include <Arduino.h>
#include <Preferences.h>

class PersistedProperties {
private:
  Preferences prefs;
  static constexpr const char* NAMESPACE = "skyfan";

  // NVS key names (max 15 chars)
  static constexpr const char* KEY_MCU_BAUD = "mcuBaud";
  static constexpr const char* KEY_PO_LIGHT_STATE = "poLightState";
  static constexpr const char* KEY_PO_LIGHT_TEMP = "poLightTemp";
  static constexpr const char* KEY_BOOT_IDX = "bootIdx";
  static constexpr const char* KEY_BOOT_PREFIX = "boot";  // boot1-boot5
  static constexpr const char* KEY_PRODUCT_ID = "productId";
  static constexpr const char* KEY_PID_MISMATCH = "pidMismatch";
  static constexpr const char* KEY_MCU_RESTARTS = "mcuRestarts";
  static constexpr const char* KEY_ZB_RESTARTS = "zbRestarts";
  static constexpr uint8_t BOOT_LOG_SLOTS = 5;

  // NVS write helpers
  bool openNvsForWriting();
  void closeNvs();

  // In-memory cache
  uint32_t _mcuBaudRate;           // 0 = not set
  int8_t _powerOnLightState;       // -1 = not set, 0 = off, 1 = on
  int8_t _powerOnLightColourTemp;  // -1 = not set, 0-2 = temp value
  uint8_t _bootIdx;                // 0 = uninitialised, 1-5 = last written slot
  char _productId[32];             // Empty string = not set
  char _productIdMismatch[32];     // Empty string = no mismatch detected
  uint32_t _mcuRestarts;            // Count of MCU restart heartbeats
  uint32_t _zigbeeModuleRestarts;   // Count of Zigbee module restarts
  bool _skipNextMcuRestart;         // True when mcuRestarts key was just created (or cleared)

public:
  PersistedProperties();

  void begin();   // Load from NVS into cache
  void clearAll();  // Erase entire NVS namespace and reset cache

  // MCU Baud Rate (returns 0 if not set)
  uint32_t getMcuBaudRate() const;
  void setMcuBaudRate(uint32_t baudRate);

  // Power-on Light State (returns -1 if not set, 0=off, 1=on)
  int8_t getPowerOnLightState() const;
  void setPowerOnLightState(bool state);

  // Power-on Light Colour Temp (returns -1 if not set, 0-2=temp)
  int8_t getPowerOnLightColourTemp() const;
  void setPowerOnLightColourTemp(uint8_t temp);

  // Product ID (returns empty string if not set)
  const char* getProductId() const;
  void setProductId(const char* id);

  // Product ID mismatch (diagnostic - returns empty string if no mismatch)
  const char* getProductIdMismatch() const;
  void setProductIdMismatch(const char* id);

  // Restart metrics
  uint32_t getMcuRestarts() const;
  uint32_t getZigbeeModuleRestarts() const;
  void onMcuHeartbeat(bool isRestart);

  // Diagnostics
  void dumpAll() const;

  // Boot log persistence (circular buffer of BOOT_LOG_SLOTS entries)
  void writeBootLog(const char* logData);
  void dumpBootLogs() const;
};

#endif // PERSISTED_PROPERTIES_H
