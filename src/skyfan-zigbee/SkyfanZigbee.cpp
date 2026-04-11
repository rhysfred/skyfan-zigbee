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
#include "TuyaProtocol.h"
#include "Logger.h"

// Constructor copied from ZigbeeFanControl with modifications for our use case
SkyfanZigbeeFanControl::SkyfanZigbeeFanControl(uint8_t endpoint) : ZigbeeEP(endpoint) {
  _device_id = ESP_ZB_HA_THERMOSTAT_DEVICE_ID;  // There is no FAN_CONTROL_DEVICE_ID in the Zigbee spec
  fanModeCallback = nullptr;
  fanDirectionCallback = nullptr;

  // Create cluster list with basic, identify, and fan control clusters
  // (copied exactly from ZigbeeFanControl)
  _cluster_list = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_identify_cluster(_cluster_list, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_fan_control_cluster(_cluster_list, esp_zb_fan_control_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  // Set endpoint config (copied exactly from ZigbeeFanControl)
  _ep_config = {
    .endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = ESP_ZB_HA_HEATING_COOLING_UNIT_DEVICE_ID, .app_device_version = 0
  };
}

SkyfanZigbeeFanControl::~SkyfanZigbeeFanControl() {
  cleanupCustomCluster();
}

void SkyfanZigbeeFanControl::onFanDirectionChange(void (*callback)(uint8_t direction)) {
  fanDirectionCallback = callback;
}

void SkyfanZigbeeFanControl::onFanModeChange(void (*callback)(ZigbeeFanMode mode)) {
  fanModeCallback = callback;
}

// Copied from ZigbeeFanControl::setFanModeSequence
bool SkyfanZigbeeFanControl::setFanModeSequence(ZigbeeFanModeSequence sequence) {
  esp_zb_attribute_list_t *fan_control_cluster =
    esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_err_t ret = esp_zb_cluster_update_attr(fan_control_cluster, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_SEQUENCE_ID, (void *)&sequence);
  if (ret != ESP_OK) {
    Log::error("Failed to set fan mode sequence: 0x%x", ret);
    return false;
  }
  _current_fan_mode_sequence = sequence;
  _current_fan_mode = FAN_MODE_OFF;
  // Set initial fan mode to OFF
  ret = esp_zb_cluster_update_attr(fan_control_cluster, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, (void *)&_current_fan_mode);
  if (ret != ESP_OK) {
    Log::error("Failed to set fan mode: 0x%x", ret);
    return false;
  }
  return true;
}

bool SkyfanZigbeeFanControl::setFanMode(ZigbeeFanMode mode) {
  // Always track intended mode (needed for confirmed state even before Zigbee.begin())
  _current_fan_mode = mode;

  // Update Zigbee attribute (works after Zigbee.begin(), fails silently before)
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
    Log::error("Failed to create custom cluster attribute list");
    return false;
  }
  
  // Add manufacturer-specific fan direction attribute with REPORTING enabled
  uint8_t default_direction = static_cast<uint8_t>(FanDirection::FORWARD);
  esp_err_t ret = esp_zb_cluster_add_manufacturer_attr(customCluster,
                                                       VENTAIR_CUSTOM_CLUSTER_ID,
                                                       CUSTOM_ATTR_FAN_DIRECTION,
                                                       FLS_MANUFACTURER_CODE,
                                                       ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
                                                       ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                                       &default_direction);
  
  if (ret != ESP_OK) {
    Log::error("Failed to add custom fan direction attribute (code: %d)", ret);
    // Clean up the allocated cluster on failure
    cleanupCustomCluster();
    return false;
  }
  
  // Add custom cluster to cluster list
  ret = esp_zb_cluster_list_add_custom_cluster(_cluster_list, customCluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  
  if (ret == ESP_OK) {
    Log::debug("Custom cluster created and registered successfully");
    customClusterRegistered = true;
    return true;
  } else {
    Log::error("Failed to add custom cluster to cluster list (code: %d)", ret);
    // Clean up the allocated cluster on failure
    cleanupCustomCluster();
    return false;
  }
}

void SkyfanZigbeeFanControl::zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) {
  // Handle our custom cluster for fan direction
  if (message->info.cluster == VENTAIR_CUSTOM_CLUSTER_ID) {
    if (message->attribute.id == CUSTOM_ATTR_FAN_DIRECTION) {
      uint8_t direction = *(uint8_t *)message->attribute.data.value;
      handleCustomClusterAttributeChange(VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, &direction);
    } else {
      Log::debug("Received message ignored - attribute ID %d not supported for custom cluster", message->attribute.id);
    }
  } else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL) {
    // Handle standard fan control cluster
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
      ZigbeeFanMode mode = *(ZigbeeFanMode *)message->attribute.data.value;
      _current_fan_mode = mode;  // Track current state
      if (fanModeCallback) {
        fanModeCallback(mode);
      }
    } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_SEQUENCE_ID &&
               message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
      _current_fan_mode_sequence = *(ZigbeeFanModeSequence *)message->attribute.data.value;
    } else {
      Log::debug("Received message ignored - attribute ID %d not supported for Fan Control", message->attribute.id);
    }
  } else {
    Log::debug("Received message ignored - cluster ID 0x%04X not supported", message->info.cluster);
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
    Log::debug("Custom cluster resources cleaned up");
  }
}

bool SkyfanZigbeeFanControl::reportFanMode() {
  // Build raw ZCL Report Attributes frame to bypass access flag check
  // ZCL Frame: [frame_ctrl][seq_num][cmd_id][attr_id_lo][attr_id_hi][attr_type][attr_value]
  uint8_t zcl_frame[7];
  zcl_frame[0] = ZCL_FRAME_CTRL_GLOBAL_TO_CLIENT;
  zcl_frame[1] = 0x00;  // Sequence number (will be filled by stack or ignored)
  zcl_frame[2] = ZCL_CMD_REPORT_ATTRIBUTES;
  zcl_frame[3] = ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID & 0xFF;
  zcl_frame[4] = (ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID >> 8) & 0xFF;
  zcl_frame[5] = ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM;
  zcl_frame[6] = static_cast<uint8_t>(_current_fan_mode);

  esp_zb_apsde_data_req_t req;
  memset(&req, 0, sizeof(req));
  req.dst_addr_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;  // Use binding table
  req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
  req.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL;
  req.src_endpoint = _endpoint;
  req.asdu_length = sizeof(zcl_frame);
  req.asdu = zcl_frame;
  req.tx_options = 0;  // No special options
  req.radius = 0;  // Use default radius

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_aps_data_request(&req);
  esp_zb_lock_release();

  if (ret != ESP_OK) {
    Log::error("Failed to report fan mode via APS: 0x%x", ret);
    return false;
  }
  Log::debug("Fan mode report sent via raw APS (mode=%d)", _current_fan_mode);
  return true;
}

bool SkyfanZigbeeFanControl::reportFanDirection() {
  // Build raw ZCL Report Attributes frame for manufacturer-specific attribute
  // ZCL Frame: [frame_ctrl][manuf_lo][manuf_hi][seq_num][cmd_id][attr_id_lo][attr_id_hi][attr_type][attr_value]
  uint8_t direction = confirmedFanDirection;
  uint8_t zcl_frame[9];
  zcl_frame[0] = ZCL_FRAME_CTRL_GLOBAL_TO_CLIENT_MANUF;
  zcl_frame[1] = FLS_MANUFACTURER_CODE & 0xFF;
  zcl_frame[2] = (FLS_MANUFACTURER_CODE >> 8) & 0xFF;
  zcl_frame[3] = 0x00;  // Sequence number
  zcl_frame[4] = ZCL_CMD_REPORT_ATTRIBUTES;
  zcl_frame[5] = CUSTOM_ATTR_FAN_DIRECTION & 0xFF;
  zcl_frame[6] = (CUSTOM_ATTR_FAN_DIRECTION >> 8) & 0xFF;
  zcl_frame[7] = ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM;
  zcl_frame[8] = direction;

  esp_zb_apsde_data_req_t req;
  memset(&req, 0, sizeof(req));
  req.dst_addr_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;  // Use binding table
  req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
  req.cluster_id = VENTAIR_CUSTOM_CLUSTER_ID;
  req.src_endpoint = _endpoint;
  req.asdu_length = sizeof(zcl_frame);
  req.asdu = zcl_frame;
  req.tx_options = 0;
  req.radius = 0;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_aps_data_request(&req);
  esp_zb_lock_release();

  if (ret != ESP_OK) {
    Log::error("Failed to report fan direction via APS: 0x%x", ret);
    return false;
  }
  Log::debug("Fan direction report sent via raw APS (direction=%d)", direction);
  return true;
}

void SkyfanZigbeeFanControl::reportAllAttributes() {
  reportFanMode();
  reportFanDirection();
}

// Confirmed state management
void SkyfanZigbeeFanControl::confirmFanMode(ZigbeeFanMode mode) {
  confirmedFanMode = mode;
}

void SkyfanZigbeeFanControl::confirmFanDirection(uint8_t direction) {
  confirmedFanDirection = direction;
}

ZigbeeFanMode SkyfanZigbeeFanControl::getConfirmedFanMode() const {
  return confirmedFanMode;
}

uint8_t SkyfanZigbeeFanControl::getConfirmedFanDirection() const {
  return confirmedFanDirection;
}

// Rollback to confirmed state and report
void SkyfanZigbeeFanControl::rollback() {
  rollbackFanMode();
  rollbackFanDirection();
}

void SkyfanZigbeeFanControl::rollbackFanMode() {
  bool setOk = setFanMode(confirmedFanMode);
  bool reportOk = reportFanMode();
  Log::info("Rolled back fan mode to %d (set=%s, report=%s)",
            confirmedFanMode, setOk ? "ok" : "FAIL", reportOk ? "ok" : "FAIL");
}

void SkyfanZigbeeFanControl::rollbackFanDirection() {
  bool setOk = setFanDirection(confirmedFanDirection);
  bool reportOk = reportFanDirection();
  Log::info("Rolled back fan direction to %d (set=%s, report=%s)",
            confirmedFanDirection, setOk ? "ok" : "FAIL", reportOk ? "ok" : "FAIL");
}

// Handle MCU status updates — validates data, updates Zigbee attributes, confirms state, reports to coordinator
void SkyfanZigbeeFanControl::handleStatusUpdate(uint8_t dpid, uint32_t value) {
  switch (dpid) {
    case DP_FAN_SWITCH: {
      bool fanOn = (value != 0);

      if (fanOn) {
        // When turning on, preserve current mode if already set to a specific speed
        // (speed status may have arrived first and set LOW/MEDIUM/HIGH)
        if (_current_fan_mode == FAN_MODE_OFF) {
          setFanMode(FAN_MODE_ON);
        }
        // Otherwise keep the existing specific mode (LOW/MEDIUM/HIGH)
      } else {
        setFanMode(FAN_MODE_OFF);
      }

      Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                 _endpoint, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, (uint32_t)_current_fan_mode);

      confirmFanMode(_current_fan_mode);
      reportFanMode();
      Log::info("Fan switch set to %s (%d) by Skyfan", fanOn ? "ON" : "OFF", fanOn ? 1 : 0);
      break;
    }

    case DP_FAN_SPEED: {
      uint8_t speed = static_cast<uint8_t>(value);
      if (!isValidTuyaFanSpeed(speed)) {
        Log::error("Invalid fan speed status received: %d", speed);
        return;
      }

      // setFanSpeed handles the speed-to-mode mapping and attribute update
      if (!setFanSpeed(speed)) {
        Log::error("Failed to update Zigbee fan speed status: %d", speed);
      } else {
        Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                   _endpoint, ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, (uint32_t)_current_fan_mode);
      }

      confirmFanMode(_current_fan_mode);
      reportFanMode();
      Log::info("Fan speed set to %d by Skyfan", speed);
      break;
    }

    case DP_FAN_MODE: {
      uint8_t mode = static_cast<uint8_t>(value);
      if (mode <= static_cast<uint8_t>(TuyaFanMode::SLEEP)) {
        const char* modeName = (mode == static_cast<uint8_t>(TuyaFanMode::NORMAL)) ? "NORMAL" :
                               (mode == static_cast<uint8_t>(TuyaFanMode::ECO)) ? "ECO" : "SLEEP";
        Log::info("Fan MCU mode set to %s (%d) by Skyfan", modeName, mode);
      } else {
        Log::error("Invalid fan mode status received: %d", mode);
      }
      break;
    }

    case DP_FAN_DIRECTION: {
      uint8_t direction = static_cast<uint8_t>(value);
      if (direction > static_cast<uint8_t>(FanDirection::REVERSE)) {
        Log::error("Invalid fan direction status received: %d", direction);
        return;
      }

      if (!setFanDirection(direction)) {
        Log::error("Failed to update Zigbee fan direction status: %d", direction);
      } else {
        Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                   _endpoint, VENTAIR_CUSTOM_CLUSTER_ID, CUSTOM_ATTR_FAN_DIRECTION, (uint32_t)direction);
      }

      confirmFanDirection(direction);
      reportFanDirection();
      Log::info("Fan direction set to %s (%d) by Skyfan",
        (direction == static_cast<uint8_t>(FanDirection::FORWARD)) ? "FORWARD" : "REVERSE", direction);
      break;
    }

    default:
      Log::error("Unknown fan status update - DPID: %d, Value: %lu", dpid, value);
      break;
  }
}