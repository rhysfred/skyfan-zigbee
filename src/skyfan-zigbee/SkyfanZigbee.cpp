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

#include "SkyfanZigbee.h"

SkyfanZigbeeFanControl::~SkyfanZigbeeFanControl() {
  cleanupCustomCluster();
}

void SkyfanZigbeeFanControl::onFanDirectionChange(void (*callback)(uint8_t direction)) {
  fanDirectionCallback = callback;
}

void SkyfanZigbeeFanControl::onFanModeChange(void (*callback)(ZigbeeFanMode mode)) {
  fanModeCallback = callback;
  // Also register with parent so other mechanisms work
  ZigbeeFanControl::onFanModeChange(callback);
}

bool SkyfanZigbeeFanControl::setFanMode(ZigbeeFanMode mode) {
  // Use esp_zb_zcl_set_attribute_val which works after Zigbee.begin()
  esp_zb_zcl_status_t ret = esp_zb_zcl_set_attribute_val(
    _endpoint,
    ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL,
    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
    ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID,
    (void *)&mode,
    false  // Don't check value
  );
  return (ret == ESP_ZB_ZCL_STATUS_SUCCESS);
}

bool SkyfanZigbeeFanControl::setFanState(bool on) {
  ZigbeeFanMode targetMode = on ? FAN_MODE_ON : FAN_MODE_OFF;
  return setFanMode(targetMode);
}

bool SkyfanZigbeeFanControl::setFanSpeed(uint8_t speed) {
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

bool SkyfanZigbeeFanControl::setFanDirection(uint8_t direction) {
  // Validate direction
  if (direction > static_cast<uint8_t>(FanDirection::REVERSE)) {
    return false;
  }
  
  // Update the manufacturer-specific cluster attribute
  if (customCluster && customClusterRegistered) {
    esp_zb_zcl_status_t ret = esp_zb_zcl_set_manufacturer_attribute_val(_endpoint, VENTAIR_CUSTOM_CLUSTER_ID,
                                                                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 
                                                                        FLS_MANUFACTURER_CODE,
                                                                        CUSTOM_ATTR_FAN_DIRECTION, 
                                                                        (void *)&direction, false);
    return (ret == ESP_ZB_ZCL_STATUS_SUCCESS);
  }
  return false;
}

uint8_t SkyfanZigbeeFanControl::getFanDirection() const {
  if (customCluster && customClusterRegistered) {
    // Get the manufacturer-specific attribute from the custom cluster
    esp_zb_zcl_attr_t *attr = esp_zb_zcl_get_manufacturer_attribute(_endpoint, VENTAIR_CUSTOM_CLUSTER_ID, 
                                                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 
                                                                     CUSTOM_ATTR_FAN_DIRECTION,
                                                                     FLS_MANUFACTURER_CODE);
    if (attr && attr->data_p) {
      return *((uint8_t*)attr->data_p);
    }
  }
  return static_cast<uint8_t>(FanDirection::FORWARD); // Default to forward
}

bool SkyfanZigbeeFanControl::createCustomCluster() {
  if (customClusterRegistered) {
    return true; // Already registered
  }
  
  // Clean up any existing cluster first (safety check)
  if (customCluster != nullptr) {
    cleanupCustomCluster();
  }
  
  // Create custom cluster attribute list
  customCluster = esp_zb_zcl_attr_list_create(VENTAIR_CUSTOM_CLUSTER_ID);
  if (!customCluster) {
    Serial.println("ERROR: Failed to create custom cluster attribute list");
    return false;
  }
  
  // Add manufacturer-specific fan direction attribute
  uint8_t default_direction = static_cast<uint8_t>(FanDirection::FORWARD);
  esp_err_t ret = esp_zb_cluster_add_manufacturer_attr(customCluster,
                                                       VENTAIR_CUSTOM_CLUSTER_ID,
                                                       CUSTOM_ATTR_FAN_DIRECTION,
                                                       FLS_MANUFACTURER_CODE,
                                                       ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
                                                       ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE,
                                                       &default_direction);
  
  if (ret != ESP_OK) {
    Serial.printf("ERROR: Failed to add custom fan direction attribute (code: %d)\n", ret);
    // Clean up the allocated cluster on failure
    cleanupCustomCluster();
    return false;
  }
  
  // Add custom cluster to cluster list
  ret = esp_zb_cluster_list_add_custom_cluster(_cluster_list, customCluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  
  if (ret == ESP_OK) {
#ifdef __DEBUG__
    Serial.println("DEBUG: Custom cluster created and registered successfully");
#endif
    customClusterRegistered = true;
    return true;
  } else {
    Serial.printf("ERROR: Failed to add custom cluster to cluster list (code: %d)\n", ret);
    // Clean up the allocated cluster on failure
    cleanupCustomCluster();
    return false;
  }
}

bool SkyfanZigbeeFanControl::enableAttributeReporting() {
  bool success = true;

  // Enable reporting for standard fan mode attribute
  // Use cluster list API (works before Zigbee.begin())
  esp_zb_attribute_list_t *fanCluster = esp_zb_cluster_list_get_cluster(
    _cluster_list,
    ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL,
    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE
  );
  if (fanCluster) {
    // Iterate through attributes to find fan mode
    esp_zb_attribute_list_t *attr = fanCluster;
    bool found = false;
    while (attr != nullptr) {
      if (attr->attribute.id == ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID) {
        attr->attribute.access |= ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
#ifdef __DEBUG__
        Serial.println("DEBUG: Enabled reporting for fan mode attribute");
#endif
        found = true;
        break;
      }
      attr = attr->next;
    }
    if (!found) {
      Serial.println("ERROR: Fan mode attribute not found in cluster");
      success = false;
    }
  } else {
    Serial.println("ERROR: Could not find fan control cluster");
    success = false;
  }

  // Enable reporting for custom fan direction attribute (if custom cluster exists)
  if (customCluster && customClusterRegistered) {
    // Iterate through custom cluster attributes
    esp_zb_attribute_list_t *attr = customCluster;
    bool found = false;
    while (attr != nullptr) {
      if (attr->attribute.id == CUSTOM_ATTR_FAN_DIRECTION) {
        attr->attribute.access |= ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
#ifdef __DEBUG__
        Serial.println("DEBUG: Enabled reporting for fan direction attribute");
#endif
        found = true;
        break;
      }
      attr = attr->next;
    }
    if (!found) {
      Serial.println("ERROR: Fan direction attribute not found in custom cluster");
      success = false;
    }
  }

  return success;
}

void SkyfanZigbeeFanControl::zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) {
  // Handle our custom cluster for fan direction
  if (message->info.cluster == VENTAIR_CUSTOM_CLUSTER_ID) {
    if (message->attribute.id == CUSTOM_ATTR_FAN_DIRECTION) {
      uint8_t direction = *(uint8_t *)message->attribute.data.value;
      handleCustomClusterAttributeChange(VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, &direction);
    } else {
#ifdef __DEBUG__
      Serial.printf("DEBUG: Received message ignored - attribute ID %d not supported for custom cluster\n", message->attribute.id);
#endif
    }
  } else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL) {
    // Handle standard fan control cluster (replicating parent's private zbAttributeSet)
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
      ZigbeeFanMode mode = *(ZigbeeFanMode *)message->attribute.data.value;
      if (fanModeCallback) {
        fanModeCallback(mode);
      }
    } else {
#ifdef __DEBUG__
      Serial.printf("DEBUG: Received message ignored - attribute ID %d not supported for Fan Control\n", message->attribute.id);
#endif
    }
  } else {
#ifdef __DEBUG__
    Serial.printf("DEBUG: Received message ignored - cluster ID 0x%04X not supported\n", message->info.cluster);
#endif
  }
}

void SkyfanZigbeeFanControl::handleCustomClusterAttributeChange(uint16_t cluster_id, uint16_t attr_id, uint8_t *data) {
  if (cluster_id == VENTAIR_CUSTOM_CLUSTER_ID && attr_id == CUSTOM_ATTR_FAN_DIRECTION && fanDirectionCallback) {
    uint8_t direction = *data;
    if (direction <= static_cast<uint8_t>(FanDirection::REVERSE)) {
      fanDirectionCallback(direction);
      // Note: Logging is done in the callback (setFanDirection) to avoid duplicates
    }
  }
}

uint16_t SkyfanZigbeeFanControl::getCustomClusterId() const {
  return VENTAIR_CUSTOM_CLUSTER_ID;
}

bool SkyfanZigbeeFanControl::isCustomClusterRegistered() const {
  return customClusterRegistered;
}

void SkyfanZigbeeFanControl::cleanupCustomCluster() {
  if (customCluster != nullptr) {
    // Note: ESP32 Zigbee SDK manages cluster lifecycle internally
    // Setting to nullptr to indicate cleanup
    customCluster = nullptr;
    customClusterRegistered = false;
#ifdef __DEBUG__
    Serial.println("DEBUG: Custom cluster resources cleaned up");
#endif
  }
}

bool SkyfanZigbeeFanControl::reportFanMode() {
  esp_zb_zcl_report_attr_cmd_t report_attr_cmd;
  memset(&report_attr_cmd, 0, sizeof(report_attr_cmd));
  report_attr_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report_attr_cmd.attributeID = ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID;
  report_attr_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report_attr_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL;
  report_attr_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  report_attr_cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd);
  esp_zb_lock_release();

  if (ret != ESP_OK) {
    Serial.printf("ERROR: Failed to report fan mode: 0x%x\n", ret);
    return false;
  }
  return true;
}

bool SkyfanZigbeeFanControl::reportFanDirection() {
  esp_zb_zcl_report_attr_cmd_t report_attr_cmd;
  memset(&report_attr_cmd, 0, sizeof(report_attr_cmd));
  report_attr_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report_attr_cmd.attributeID = CUSTOM_ATTR_FAN_DIRECTION;
  report_attr_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report_attr_cmd.clusterID = VENTAIR_CUSTOM_CLUSTER_ID;
  report_attr_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  report_attr_cmd.manuf_code = FLS_MANUFACTURER_CODE;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd);
  esp_zb_lock_release();

  if (ret != ESP_OK) {
    Serial.printf("ERROR: Failed to report fan direction: 0x%x\n", ret);
    return false;
  }
  return true;
}