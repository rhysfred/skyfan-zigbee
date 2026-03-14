/*
 * Logger - Centralized logging for Skyfan Zigbee Controller
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

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <stdarg.h>

class Logger {
public:
  // Initialize the logger (call in setup)
  static void begin(unsigned long baudRate = 115200) {
    Serial.begin(baudRate);
  }

  // Error messages - always printed
  static void error(const char* format, ...) {
    Serial.print("ERROR: ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    Serial.println();
  }

  // Info messages - always printed
  static void info(const char* format, ...) {
    Serial.print("INFO: ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    Serial.println();
  }

  // Debug messages - only printed when __DEBUG__ is defined
  static void debug([[maybe_unused]] const char* format, ...) {
#ifdef __DEBUG__
    Serial.print("DEBUG: ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    Serial.println();
#endif
  }

  // Raw print without prefix or newline (for special cases like progress dots)
  static void raw(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }

  // Boot log methods - buffer entries in memory for later NVS persistence
#ifdef __BOOT_LOG__
  static void boot(uint32_t baudRate, const uint8_t* sent, uint16_t sentLen,
                    const uint8_t* received, uint16_t receivedLen, bool pass) {
    // Build entry in a temporary buffer
    char entry[128];
    uint16_t pos = 0;

    // Baud rate
    pos += snprintf(entry + pos, sizeof(entry) - pos, "%lu:", baudRate);

    // Sent hex
    pos += toHex(sent, sentLen, entry + pos, sizeof(entry) - pos);

    // Separator
    if (pos < sizeof(entry) - 1) entry[pos++] = ':';

    // Received hex
    pos += toHex(received, receivedLen, entry + pos, sizeof(entry) - pos);

    // Separator and result
    pos += snprintf(entry + pos, sizeof(entry) - pos, ":%s", pass ? "PASS" : "FAIL");

    // Also print via info
    info("BOOT: %s", entry);

    // Append to boot buffer with em dash separator
    uint16_t entryLen = pos;
    uint16_t separatorLen = (_bootBufferPos > 0) ? 3 : 0;  // UTF-8 em dash is 3 bytes

    if (_bootBufferPos + separatorLen + entryLen + 1 > BOOT_LOG_BUFFER_SIZE) {
      return;  // Buffer full, drop entry
    }

    if (separatorLen > 0) {
      _bootBuffer[_bootBufferPos++] = '\xe2';
      _bootBuffer[_bootBufferPos++] = '\x80';
      _bootBuffer[_bootBufferPos++] = '\x94';
    }

    memcpy(_bootBuffer + _bootBufferPos, entry, entryLen);
    _bootBufferPos += entryLen;
    _bootBuffer[_bootBufferPos] = '\0';
  }

  static const char* getBootLog() { return _bootBuffer; }
  static void clearBootLog() { _bootBufferPos = 0; _bootBuffer[0] = '\0'; }
#else
  static void boot(uint32_t, const uint8_t*, uint16_t, const uint8_t*, uint16_t, bool) {}
  static const char* getBootLog() { return ""; }
  static void clearBootLog() {}
#endif

private:
  static void vprintf(const char* format, va_list args) {
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.print(buffer);
  }

#ifdef __BOOT_LOG__
  static constexpr uint16_t BOOT_LOG_BUFFER_SIZE = 700;
  inline static char _bootBuffer[BOOT_LOG_BUFFER_SIZE];
  inline static uint16_t _bootBufferPos = 0;

  static uint16_t toHex(const uint8_t* data, uint16_t len, char* out, uint16_t outSize) {
    uint16_t written = 0;
    for (uint16_t i = 0; i < len && written + 2 < outSize; i++) {
      out[written++] = "0123456789abcdef"[data[i] >> 4];
      out[written++] = "0123456789abcdef"[data[i] & 0x0F];
    }
    out[written] = '\0';
    return written;
  }
#endif
};

// Global logger instance for convenience
#define Log Logger

#endif // LOGGER_H
