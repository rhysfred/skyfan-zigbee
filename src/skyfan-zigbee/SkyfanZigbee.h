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
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_cluster.h"
#include "SkyfanConfig.h"

// Extended ZigbeeFanControl class with custom cluster support
class SkyfanZigbeeFanControl : public ZigbeeFanControl {
private:
  void (*fanDirectionCallback)(uint8_t direction) = nullptr;
  esp_zb_attribute_list_t *customCluster = nullptr;
  bool customClusterRegistered = false;

public:
  SkyfanZigbeeFanControl(uint8_t endpoint) : ZigbeeFanControl(endpoint) {}
  
  // Destructor to clean up allocated resources
  ~SkyfanZigbeeFanControl();
  
  // Set callback for fan direction changes from Zigbee
  void onFanDirectionChange(void (*callback)(uint8_t direction));
  
  // Public setter methods for bidirectional status updates
  bool setFanMode(ZigbeeFanMode mode);
  bool setFanState(bool on);
  bool setFanSpeed(uint8_t speed);
  
  // Custom cluster attribute methods for fan direction
  bool setFanDirection(uint8_t direction);
  uint8_t getFanDirection() const;
  
  // Create and register manufacturer-specific cluster for fan direction  
  bool createCustomCluster();
  
  // Handle custom cluster attribute changes
  void handleCustomClusterAttributeChange(uint16_t cluster_id, uint16_t attr_id, uint8_t *data);
  
  // Get custom cluster ID for external reference
  uint16_t getCustomClusterId() const;
  
  // Check if custom cluster is registered
  bool isCustomClusterRegistered() const;
  
  // Clean up custom cluster resources
  void cleanupCustomCluster();
  
};

#endif // SKYFAN_ZIGBEE_H