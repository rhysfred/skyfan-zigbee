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

#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Arduino.h>

// === LED Status Indication Timing ===
#define LED_STATUS_UPDATE_INTERVAL_MS  100    // Check LED status every 100ms
#define LED_RAPID_FLASH_ON_TIME_MS     100    // Rapid flash on time (factory new)
#define LED_RAPID_FLASH_OFF_TIME_MS    100    // Rapid flash off time (factory new)
#define LED_COMMAND_FLASH_ON_TIME_MS   50     // Flash on duration for command feedback
#define LED_COMMAND_FLASH_OFF_TIME_MS  15     // Flash off duration before restart

// LED status states for visual indication
enum class LedStatus : uint8_t {
  FACTORY_NEW = 0,    // Rapid flash - device never joined network
  INITIALISING = 1,   // Solid on - device starting up
  CONNECTED = 2,      // Off - device connected to network
  COMMAND_FLASH = 3   // Command feedback flash (temporary)
};

// LED Status Indicator Class
class LedStatusIndicator {
private:
  uint8_t pin;
  LedStatus currentStatus;
  bool ledState;
  unsigned long lastUpdate;
  unsigned long lastFlashStart;
  unsigned long commandFlashStart;
  bool commandFlashPending;
  
public:
  LedStatusIndicator(uint8_t ledPin);
  
  void update();
  void setStatus(LedStatus status);
  void flashCommand();
  
  LedStatus getStatus() const {
    return currentStatus;
  }
};

#endif // LED_INDICATOR_H