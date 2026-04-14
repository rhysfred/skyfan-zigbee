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

#include "PersistedProperties.h"
#include "Logger.h"

PersistedProperties::PersistedProperties()
  : _mcuBaudRate(0),
    _powerOnLightState(-1),
    _powerOnLightColourTemp(-1),
    _bootIdx(0) {
  _productId[0] = '\0';
  _productIdMismatch[0] = '\0';
}

void PersistedProperties::begin() {
  if (!prefs.begin(NAMESPACE, true)) {  // Read-only mode for loading
    Log::error("Failed to open NVS namespace for reading");
    return;
  }

  // Load MCU baud rate (0 if not set)
  if (prefs.isKey(KEY_MCU_BAUD)) {
    _mcuBaudRate = prefs.getUInt(KEY_MCU_BAUD, 0);
    Log::debug("Loaded mcuBaudRate from NVS: %lu", _mcuBaudRate);
  }

  // Load power-on light state (-1 if not set)
  if (prefs.isKey(KEY_PO_LIGHT_STATE)) {
    _powerOnLightState = prefs.getBool(KEY_PO_LIGHT_STATE, false) ? 1 : 0;
    Log::debug("Loaded powerOnLightState from NVS: %d", _powerOnLightState);
  }

  // Load power-on light colour temp (-1 if not set)
  if (prefs.isKey(KEY_PO_LIGHT_TEMP)) {
    _powerOnLightColourTemp = prefs.getUChar(KEY_PO_LIGHT_TEMP, 0);
    Log::debug("Loaded powerOnLightColourTemp from NVS: %d", _powerOnLightColourTemp);
  }

  // Load boot log index (0 if not set)
  if (prefs.isKey(KEY_BOOT_IDX)) {
    _bootIdx = prefs.getUChar(KEY_BOOT_IDX, 0);
    Log::debug("Loaded bootIdx from NVS: %d", _bootIdx);
  }

  // Load product ID (empty string if not set)
  if (prefs.isKey(KEY_PRODUCT_ID)) {
    prefs.getString(KEY_PRODUCT_ID, _productId, sizeof(_productId));
    Log::debug("Loaded productId from NVS: %s", _productId);
  }

  // Load product ID mismatch (empty string if not set)
  if (prefs.isKey(KEY_PID_MISMATCH)) {
    prefs.getString(KEY_PID_MISMATCH, _productIdMismatch, sizeof(_productIdMismatch));
    Log::debug("Loaded pidMismatch from NVS: %s", _productIdMismatch);
  }

  prefs.end();
}

void PersistedProperties::clearAll() {
  if (!openNvsForWriting()) return;

  if (prefs.clear()) {
    Log::info("All persisted properties cleared from NVS");
  } else {
    Log::error("Failed to clear NVS namespace");
  }

  closeNvs();

  // Reset in-memory cache
  _mcuBaudRate = 0;
  _powerOnLightState = -1;
  _powerOnLightColourTemp = -1;
  _bootIdx = 0;
  _productId[0] = '\0';
  _productIdMismatch[0] = '\0';
}

// NVS write helpers
bool PersistedProperties::openNvsForWriting() {
  if (!prefs.begin(NAMESPACE, false)) {
    Log::error("Failed to open NVS namespace for writing");
    return false;
  }
  return true;
}

void PersistedProperties::closeNvs() {
  prefs.end();
}

// MCU Baud Rate
uint32_t PersistedProperties::getMcuBaudRate() const {
  return _mcuBaudRate;
}

void PersistedProperties::setMcuBaudRate(uint32_t baudRate) {
  _mcuBaudRate = baudRate;
  if (!openNvsForWriting()) return;

  if (prefs.putUInt(KEY_MCU_BAUD, baudRate) == 0) {
    Log::error("Failed to write mcuBaudRate to NVS");
  } else {
    Log::debug("Wrote mcuBaudRate to NVS: %lu", baudRate);
  }

  closeNvs();
}

void PersistedProperties::clearMcuBaudRate() {
  _mcuBaudRate = 0;
  if (!openNvsForWriting()) return;

  prefs.remove(KEY_MCU_BAUD);
  Log::info("Cleared persisted MCU baud rate from NVS");

  closeNvs();
}

// Power-on Light State
int8_t PersistedProperties::getPowerOnLightState() const {
  return _powerOnLightState;
}

void PersistedProperties::setPowerOnLightState(bool state) {
  _powerOnLightState = state ? 1 : 0;
  if (!openNvsForWriting()) return;

  if (prefs.putBool(KEY_PO_LIGHT_STATE, state) == 0) {
    Log::error("Failed to write powerOnLightState to NVS");
  } else {
    Log::debug("Wrote powerOnLightState to NVS: %d", state ? 1 : 0);
  }

  closeNvs();
}

// Power-on Light Colour Temp
int8_t PersistedProperties::getPowerOnLightColourTemp() const {
  return _powerOnLightColourTemp;
}

void PersistedProperties::setPowerOnLightColourTemp(uint8_t temp) {
  _powerOnLightColourTemp = temp;
  if (!openNvsForWriting()) return;

  if (prefs.putUChar(KEY_PO_LIGHT_TEMP, temp) == 0) {
    Log::error("Failed to write powerOnLightColourTemp to NVS");
  } else {
    Log::debug("Wrote powerOnLightColourTemp to NVS: %d", temp);
  }

  closeNvs();
}

// Product ID
const char* PersistedProperties::getProductId() const {
  return _productId;
}

void PersistedProperties::setProductId(const char* id) {
  if (strcmp(_productId, id) == 0) return;

  strncpy(_productId, id, sizeof(_productId) - 1);
  _productId[sizeof(_productId) - 1] = '\0';

  if (!openNvsForWriting()) return;

  if (prefs.putString(KEY_PRODUCT_ID, _productId) == 0) {
    Log::error("Failed to write productId to NVS");
  } else {
    Log::debug("Wrote productId to NVS: %s", _productId);
  }

  closeNvs();
}

// Product ID Mismatch
const char* PersistedProperties::getProductIdMismatch() const {
  return _productIdMismatch;
}

void PersistedProperties::setProductIdMismatch(const char* id) {
  strncpy(_productIdMismatch, id, sizeof(_productIdMismatch) - 1);
  _productIdMismatch[sizeof(_productIdMismatch) - 1] = '\0';

  if (!openNvsForWriting()) return;

  if (prefs.putString(KEY_PID_MISMATCH, _productIdMismatch) == 0) {
    Log::error("Failed to write pidMismatch to NVS");
  } else {
    Log::debug("Wrote pidMismatch to NVS: %s", _productIdMismatch);
  }

  closeNvs();
}

// Boot Log - circular buffer write
void PersistedProperties::writeBootLog(const char* logData) {
  _bootIdx = (_bootIdx % BOOT_LOG_SLOTS) + 1;

  char key[8];
  snprintf(key, sizeof(key), "%s%d", KEY_BOOT_PREFIX, _bootIdx);

  if (!openNvsForWriting()) return;

  if (prefs.putString(key, logData) == 0) {
    Log::error("Failed to write boot log to NVS slot %s", key);
  } else {
    Log::info("Boot log written to NVS slot %s", key);
  }

  if (prefs.putUChar(KEY_BOOT_IDX, _bootIdx) == 0) {
    Log::error("Failed to write bootIdx to NVS");
  }

  closeNvs();
}

// Boot Log - dump all slots in most-recent-first order
void PersistedProperties::dumpBootLogs() const {
  if (_bootIdx == 0) {
    Log::info("No boot logs recorded");
    return;
  }

  Log::info("=== Boot Logs (most recent first) ===");

  Preferences readPrefs;
  if (!readPrefs.begin(NAMESPACE, true)) {
    Log::error("Failed to open NVS namespace for reading boot logs");
    return;
  }

  uint8_t slot = _bootIdx;
  for (uint8_t i = 0; i < BOOT_LOG_SLOTS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "%s%d", KEY_BOOT_PREFIX, slot);

    if (readPrefs.isKey(key)) {
      String logEntry = readPrefs.getString(key, "");
      if (logEntry.length() > 0) {
        Log::info("Boot log [%d/%d]%s slot %s: %s",
                  i + 1, BOOT_LOG_SLOTS,
                  (i == 0) ? " (most recent)" : "",
                  key, logEntry.c_str());
      }
    }

    // Move to previous slot, wrapping from 1 to BOOT_LOG_SLOTS
    slot = (slot == 1) ? BOOT_LOG_SLOTS : slot - 1;
  }

  readPrefs.end();
  Log::info("=== End Boot Logs ===");
}
