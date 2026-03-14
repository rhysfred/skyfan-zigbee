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
  static constexpr uint8_t BOOT_LOG_SLOTS = 5;

  // In-memory cache
  uint32_t _mcuBaudRate;           // 0 = not set
  int8_t _powerOnLightState;       // -1 = not set, 0 = off, 1 = on
  int8_t _powerOnLightColourTemp;  // -1 = not set, 0-2 = temp value
  uint8_t _bootIdx;                // 0 = uninitialised, 1-5 = last written slot

public:
  PersistedProperties();

  void begin();  // Load from NVS into cache

  // MCU Baud Rate (returns 0 if not set)
  uint32_t getMcuBaudRate() const;
  void setMcuBaudRate(uint32_t baudRate);

  // Power-on Light State (returns -1 if not set, 0=off, 1=on)
  int8_t getPowerOnLightState() const;
  void setPowerOnLightState(bool state);

  // Power-on Light Colour Temp (returns -1 if not set, 0-2=temp)
  int8_t getPowerOnLightColourTemp() const;
  void setPowerOnLightColourTemp(uint8_t temp);

  // Boot log persistence (circular buffer of BOOT_LOG_SLOTS entries)
  void writeBootLog(const char* logData);
  void dumpBootLogs() const;
};

#endif // PERSISTED_PROPERTIES_H
