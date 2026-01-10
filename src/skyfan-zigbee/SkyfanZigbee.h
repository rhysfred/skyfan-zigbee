/*
 * Skyfan Zigbee Library - Extended Zigbee classes and custom attributes for Skyfan controller
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

#ifndef SKYFAN_ZIGBEE_H
#define SKYFAN_ZIGBEE_H

#include <Arduino.h>
#include "Zigbee.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "aps/esp_zigbee_aps.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_cluster.h"
#include "SkyfanConfig.h"

// Custom fan control class that replicates ZigbeeFanControl functionality
// but adds raw APS reporting (to bypass access flag checks) and custom cluster support
class SkyfanZigbeeFanControl : public ZigbeeEP {
private:
  void (*fanDirectionCallback)(uint8_t direction) = nullptr;
  void (*fanModeCallback)(ZigbeeFanMode mode) = nullptr;
  esp_zb_attribute_list_t *customCluster = nullptr;
  bool customClusterRegistered = false;

  // Fan state (replicated from ZigbeeFanControl since we don't inherit from it)
  ZigbeeFanMode _current_fan_mode = FAN_MODE_OFF;
  ZigbeeFanModeSequence _current_fan_mode_sequence = FAN_MODE_SEQUENCE_LOW_MED_HIGH;

  // Confirmed state (last value acknowledged by MCU)
  ZigbeeFanMode confirmedFanMode = FAN_MODE_OFF;
  uint8_t confirmedFanDirection = static_cast<uint8_t>(FanDirection::FORWARD);

public:
  SkyfanZigbeeFanControl(uint8_t endpoint);
  
  // Destructor to clean up allocated resources
  ~SkyfanZigbeeFanControl();
  
  // Set callback for fan direction changes from Zigbee
  void onFanDirectionChange(void (*callback)(uint8_t direction));

  // Set callback for fan mode changes from Zigbee
  void onFanModeChange(void (*callback)(ZigbeeFanMode mode));

  // Get current fan mode
  ZigbeeFanMode getFanMode() const { return _current_fan_mode; }

  // Get current fan mode sequence
  ZigbeeFanModeSequence getFanModeSequence() const { return _current_fan_mode_sequence; }

  // Set the fan mode sequence value
  bool setFanModeSequence(ZigbeeFanModeSequence sequence);

  // Public setter methods for bidirectional status updates
  bool setFanMode(ZigbeeFanMode mode);
  bool setFanState(bool on);
  bool setFanSpeed(uint8_t speed);
  
  // Custom cluster attribute methods for fan direction
  bool setFanDirection(uint8_t direction);
  uint8_t getFanDirection() const;
  
  // Create and register manufacturer-specific cluster for fan direction
  bool createCustomCluster();
  
  // Handle custom cluster attribute changes (called internally by zbAttributeSet)
  void handleCustomClusterAttributeChange(uint16_t cluster_id, uint16_t attr_id, uint8_t *data);

  // Override zbAttributeSet to handle custom cluster writes
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) override;

  // Get custom cluster ID for external reference
  uint16_t getCustomClusterId() const;
  
  // Check if custom cluster is registered
  bool isCustomClusterRegistered() const;

  // Clean up custom cluster resources
  void cleanupCustomCluster();

  // Report attribute values to coordinator
  bool reportFanMode();
  bool reportFanDirection();

  // Confirmed state management (called when MCU confirms via status report)
  void confirmFanMode(ZigbeeFanMode mode);
  void confirmFanDirection(uint8_t direction);
  ZigbeeFanMode getConfirmedFanMode() const;
  uint8_t getConfirmedFanDirection() const;

  // Rollback to confirmed state and report to coordinator
  void rollbackFanMode();
  void rollbackFanDirection();

};

#endif // SKYFAN_ZIGBEE_H