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
#include "SkyfanConfig.h"

// Custom Zigbee Attributes for Skyfan
#define CUSTOM_ATTR_FAN_DIRECTION 0xF001  // Custom manufacturer attribute for fan direction

// Extended ZigbeeFanControl class with public setter methods for status updates
class SkyfanZigbeeFanControl : public ZigbeeFanControl {
public:
  SkyfanZigbeeFanControl(uint8_t endpoint) : ZigbeeFanControl(endpoint) {}
  
  // Public setter methods for bidirectional status updates
  bool setFanMode(ZigbeeFanMode mode) {
    // Update the Zigbee cluster attribute using protected member access
    esp_zb_attribute_list_t *fan_control_cluster =
      esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    if (fan_control_cluster) {
      esp_err_t ret = esp_zb_cluster_update_attr(fan_control_cluster, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, (void *)&mode);
      return (ret == ESP_OK);
    }
    return false;
  }
  
  // Convenience method to set fan state (on/off)
  bool setFanState(bool on) {
    ZigbeeFanMode targetMode = on ? FAN_MODE_ON : FAN_MODE_OFF;
    return setFanMode(targetMode);
  }
  
  // Map Tuya speed to appropriate Zigbee fan mode
  bool setFanSpeedMode(uint8_t speed) {
    // Validate input range
    if (!isValidTuyaFanSpeed(speed)) {
      return false;
    }
    
    ZigbeeFanMode mode;
    switch (speed) {
      case TUYA_FAN_SPEED_MIN:  // 0
        mode = FAN_MODE_OFF;
        break;
      case FAN_SPEED_LOW_TUYA:  // 1
      case FAN_SPEED_LOW_TUYA + 1:  // 2
        mode = FAN_MODE_LOW;
        break;
      case FAN_SPEED_MEDIUM_TUYA:  // 3
      case FAN_SPEED_MEDIUM_TUYA + 1:  // 4
        mode = FAN_MODE_MEDIUM;
        break;
      case FAN_SPEED_HIGH_TUYA:  // 5
        mode = FAN_MODE_HIGH;
        break;
      default:
        mode = FAN_MODE_ON;  // Generic on state for unknown speeds
        break;
    }
    return setFanMode(mode);
  }
};

#endif // SKYFAN_ZIGBEE_H