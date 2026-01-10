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

private:
  static void vprintf(const char* format, va_list args) {
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.print(buffer);
  }
};

// Global logger instance for convenience
#define Log Logger

#endif // LOGGER_H
