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

#include "ButtonHandler.h"

DebouncedButton::DebouncedButton(uint8_t buttonPin, unsigned long debounceMs, unsigned long longPressMs) 
  : pin(buttonPin), lastStateChange(0), lastPressTime(0), lastState(HIGH), currentState(HIGH), 
    pressed(false), longPressed(false), debounceDelay(debounceMs), longPressDelay(longPressMs) {
  pinMode(pin, INPUT_PULLUP);
}

void DebouncedButton::update() {
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

bool DebouncedButton::wasPressed() {
  if (pressed && currentState == HIGH) {  // Just released after being pressed
    pressed = false;
    return !longPressed;  // Only return true if it wasn't a long press
  }
  return false;
}

bool DebouncedButton::wasLongPressed() {
  if (longPressed && currentState == HIGH) {  // Just released after long press
    longPressed = false;
    return true;
  }
  return false;
}

bool DebouncedButton::isLongPressed() const {
  return longPressed && currentState == LOW;
}

bool DebouncedButton::isPressed() const {
  return currentState == LOW;
}