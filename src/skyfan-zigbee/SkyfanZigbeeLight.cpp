/*
 * Skyfan Zigbee Light - Extended ZigbeeColorDimmableLight with reporting capabilities
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

#include "SkyfanZigbeeLight.h"
#include "Logger.h"

bool SkyfanZigbeeLight::reportLightState() {
  esp_zb_zcl_report_attr_cmd_t report_attr_cmd;
  memset(&report_attr_cmd, 0, sizeof(report_attr_cmd));
  report_attr_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report_attr_cmd.attributeID = ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
  report_attr_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report_attr_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF;
  report_attr_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  report_attr_cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd);
  esp_zb_lock_release();

  if (ret != ESP_OK) {
    Log::error("Failed to report light state: 0x%x", ret);
    return false;
  }
  return true;
}

bool SkyfanZigbeeLight::reportLightLevel() {
  esp_zb_zcl_report_attr_cmd_t report_attr_cmd;
  memset(&report_attr_cmd, 0, sizeof(report_attr_cmd));
  report_attr_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report_attr_cmd.attributeID = ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID;
  report_attr_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report_attr_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL;
  report_attr_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  report_attr_cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd);
  esp_zb_lock_release();

  if (ret != ESP_OK) {
    Log::error("Failed to report light level: 0x%x", ret);
    return false;
  }
  return true;
}

bool SkyfanZigbeeLight::reportLightColorTemp() {
  esp_zb_zcl_report_attr_cmd_t report_attr_cmd;
  memset(&report_attr_cmd, 0, sizeof(report_attr_cmd));
  report_attr_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report_attr_cmd.attributeID = ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID;
  report_attr_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report_attr_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL;
  report_attr_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  report_attr_cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd);
  esp_zb_lock_release();

  if (ret != ESP_OK) {
    Log::error("Failed to report light color temp: 0x%x", ret);
    return false;
  }
  return true;
}

void SkyfanZigbeeLight::reportAllAttributes() {
  reportLightState();
  reportLightLevel();
  reportLightColorTemp();
}

// Confirmed state management
void SkyfanZigbeeLight::confirmLightState(bool on) {
  confirmedLightState = on;
}

void SkyfanZigbeeLight::confirmLightLevel(uint8_t level) {
  confirmedLightLevel = level;
}

void SkyfanZigbeeLight::confirmColorTemp(uint16_t mired) {
  confirmedColorTemp = mired;
}

bool SkyfanZigbeeLight::getConfirmedLightState() const {
  return confirmedLightState;
}

uint8_t SkyfanZigbeeLight::getConfirmedLightLevel() const {
  return confirmedLightLevel;
}

uint16_t SkyfanZigbeeLight::getConfirmedColorTemp() const {
  return confirmedColorTemp;
}

// Rollback to confirmed state and report
void SkyfanZigbeeLight::rollback() {
  bool stateOk = setLightState(confirmedLightState);
  bool levelOk = setLightLevel(confirmedLightLevel);
  bool tempOk = setLightColorTemperature(confirmedColorTemp);
  bool reportStateOk = reportLightState();
  bool reportLevelOk = reportLightLevel();
  bool reportTempOk = reportLightColorTemp();
  Log::info("Rolled back light state=%d(%s/%s), level=%d(%s/%s), temp=%d(%s/%s)",
            confirmedLightState, stateOk ? "ok" : "FAIL", reportStateOk ? "ok" : "FAIL",
            confirmedLightLevel, levelOk ? "ok" : "FAIL", reportLevelOk ? "ok" : "FAIL",
            confirmedColorTemp, tempOk ? "ok" : "FAIL", reportTempOk ? "ok" : "FAIL");
}
