/*
 * LED Status Indicator - Visual feedback for device states
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

#include "LedIndicator.h"

LedStatusIndicator::LedStatusIndicator(uint8_t ledPin) 
  : pin(ledPin), currentStatus(LedStatus::INITIALISING), ledState(false), 
    lastUpdate(0), lastFlashStart(0), commandFlashStart(0), commandFlashPending(false) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void LedStatusIndicator::update() {
  unsigned long now = millis();
  
  if (now - lastUpdate < LED_STATUS_UPDATE_INTERVAL_MS) {
    return; // Not time to update yet
  }
  lastUpdate = now;
  
  // Handle command flash first (highest priority)
  if (currentStatus == LedStatus::COMMAND_FLASH) {
    if (now - commandFlashStart >= LED_COMMAND_FLASH_ON_TIME_MS) {
      // Flash period completed, return to CONNECTED (main code will update to correct status)
      currentStatus = LedStatus::CONNECTED;
      commandFlashPending = false;
    } else {
      // Keep LED on during flash period
      if (!ledState) {
        ledState = true;
        digitalWrite(pin, HIGH);
      }
    }
    return;
  }
  
  // Handle command flash restart logic during normal operation
  if (commandFlashPending && currentStatus == LedStatus::CONNECTED) {
    if (now - commandFlashStart >= LED_COMMAND_FLASH_OFF_TIME_MS) {
      // Restart command flash after off period
      currentStatus = LedStatus::COMMAND_FLASH;
      commandFlashStart = now;
      ledState = true;
      digitalWrite(pin, HIGH);
      return;
    }
  }
  
  switch (currentStatus) {
    case LedStatus::FACTORY_NEW:
      // Rapid flash - 5 times per second (100ms on, 100ms off)
      if (now - lastFlashStart >= LED_RAPID_FLASH_ON_TIME_MS + LED_RAPID_FLASH_OFF_TIME_MS) {
        lastFlashStart = now;
        ledState = true;
        digitalWrite(pin, HIGH);
      } else if (ledState && (now - lastFlashStart >= LED_RAPID_FLASH_ON_TIME_MS)) {
        ledState = false;
        digitalWrite(pin, LOW);
      }
      break;
      
    case LedStatus::INITIALISING:
      // Solid on
      if (!ledState) {
        ledState = true;
        digitalWrite(pin, HIGH);
      }
      break;
      
    case LedStatus::CONNECTED:
      // Off (unless command flash pending)
      if (!commandFlashPending && ledState) {
        ledState = false;
        digitalWrite(pin, LOW);
      }
      break;
      
    case LedStatus::COMMAND_FLASH:
      // Handled above
      break;
  }
}

void LedStatusIndicator::setStatus(LedStatus status) {
  if (currentStatus != status) {
    currentStatus = status;
    lastFlashStart = millis(); // Reset timing when status changes
    
    // Immediate state change for specific states
    if (status == LedStatus::INITIALISING) {
      ledState = true;
      digitalWrite(pin, HIGH);
    } else if (status == LedStatus::CONNECTED && !commandFlashPending) {
      ledState = false;
      digitalWrite(pin, LOW);
    }
  }
}

void LedStatusIndicator::flashCommand() {
  unsigned long now = millis();
  
  if (currentStatus == LedStatus::COMMAND_FLASH) {
    // Already flashing - restart with 15ms off period first
    currentStatus = LedStatus::CONNECTED;
    commandFlashPending = true;
    commandFlashStart = now;
    ledState = false;
    digitalWrite(pin, LOW);
  } else {
    // Start new command flash
    currentStatus = LedStatus::COMMAND_FLASH;
    commandFlashStart = now;
    commandFlashPending = false;
    ledState = true;
    digitalWrite(pin, HIGH);
  }
}