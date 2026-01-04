/*
 * Button Handler - Non-blocking debounced button input
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

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>

// === Button Timing Configuration ===
#define BUTTON_DEBOUNCE_DELAY_MS       100    // 100ms
#define FACTORY_RESET_HOLD_TIME_MS     3000   // 3 seconds

// Non-blocking Button Debounce Class
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
  DebouncedButton(uint8_t buttonPin, 
                  unsigned long debounceMs = BUTTON_DEBOUNCE_DELAY_MS, 
                  unsigned long longPressMs = FACTORY_RESET_HOLD_TIME_MS);

  void update();
  bool wasPressed();
  bool wasLongPressed();
  bool isLongPressed() const;
  bool isPressed() const;
};

#endif // BUTTON_HANDLER_H