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
#include "TuyaProtocol.h"
#include "Logger.h"

// Helper to report a single attribute to coordinator
bool SkyfanZigbeeLight::reportAttribute(uint16_t clusterId, uint16_t attributeId) {
  esp_zb_zcl_report_attr_cmd_t report_attr_cmd;
  memset(&report_attr_cmd, 0, sizeof(report_attr_cmd));
  report_attr_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report_attr_cmd.attributeID = attributeId;
  report_attr_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report_attr_cmd.clusterID = clusterId;
  report_attr_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  report_attr_cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_err_t ret = esp_zb_zcl_report_attr_cmd_req(&report_attr_cmd);
  esp_zb_lock_release();

  return ret == ESP_OK;
}

bool SkyfanZigbeeLight::reportLightState() {
  bool ok = reportAttribute(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
  if (!ok) Log::error("Failed to report light state");
  return ok;
}

bool SkyfanZigbeeLight::reportLightLevel() {
  bool ok = reportAttribute(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);
  if (!ok) Log::error("Failed to report light level");
  return ok;
}

bool SkyfanZigbeeLight::reportLightColorTemp() {
  bool ok = reportAttribute(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID);
  if (!ok) Log::error("Failed to report light color temp");
  return ok;
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

// Direct setters — suppress callback to prevent echo when MCU reports status
bool SkyfanZigbeeLight::setLightStateDirect(bool on) {
  _suppressCallback = true;
  bool result = setLightState(on);
  _suppressCallback = false;
  return result;
}

bool SkyfanZigbeeLight::setLightLevelDirect(uint8_t level) {
  _suppressCallback = true;
  bool result = setLightLevel(level);
  _suppressCallback = false;
  return result;
}

bool SkyfanZigbeeLight::setLightColorTemperatureDirect(uint16_t mired) {
  _suppressCallback = true;
  bool result = setLightColorTemperature(mired);
  _suppressCallback = false;
  return result;
}

bool SkyfanZigbeeLight::isCallbackSuppressed() const {
  return _suppressCallback;
}

// Handle MCU status updates — validates data, updates Zigbee attributes, confirms state, reports to coordinator
void SkyfanZigbeeLight::handleStatusUpdate(uint8_t dpid, uint32_t value) {
  switch (dpid) {
    case DP_LIGHT_SWITCH: {
      bool lightOn = (value != 0);
      if (!setLightStateDirect(lightOn)) {
        Log::error("Failed to update Zigbee light switch status: %s", lightOn ? "ON" : "OFF");
      } else {
        Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                   _endpoint, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, lightOn ? 1UL : 0UL);
      }

      confirmLightState(lightOn);
      reportLightState();
      Log::info("Light switch set to %s (%d) by Skyfan", lightOn ? "ON" : "OFF", lightOn ? 1 : 0);
      break;
    }

    case DP_LIGHT_DIMMER: {
      uint8_t tuyaBrightness = static_cast<uint8_t>(value);
      if (!isValidTuyaBrightness(tuyaBrightness)) {
        Log::error("Invalid light brightness status received: %d", tuyaBrightness);
        return;
      }

      // Ignore brightness 0 — the MCU reports 0 as part of turning off and
      // transiently when turning on before the dimmer command arrives.
      // The switch DPID is the authoritative on/off signal; brightness 0 is
      // never a meaningful user-facing value. Preserving the last non-zero
      // level prevents sending brightness 0 on the next ON command, which would
      // cause the MCU to turn on at zero brightness and immediately switch off.
      if (tuyaBrightness == 0) {
        Log::debug("Ignoring brightness 0 status from MCU");
        return;
      }

      uint8_t zigbeeBrightness = tuyaBrightnessToZigbee(tuyaBrightness);
      if (!setLightLevelDirect(zigbeeBrightness)) {
        Log::error("Failed to update Zigbee light brightness: %d", zigbeeBrightness);
      } else {
        Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                   _endpoint, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, (uint32_t)zigbeeBrightness);
      }

      confirmLightLevel(zigbeeBrightness);
      reportLightLevel();
      Log::info("Light brightness set to %d (Zigbee: %d) by Skyfan", tuyaBrightness, zigbeeBrightness);
      break;
    }

    case DP_LIGHT_COLOUR_TEMP: {
      uint8_t colourTempValue = static_cast<uint8_t>(value);
      if (colourTempValue > static_cast<uint8_t>(ColourTempLevel::COOL)) {
        Log::error("Invalid light colour temperature status received: %d", colourTempValue);
        return;
      }

      ColourTempLevel colourLevel = static_cast<ColourTempLevel>(colourTempValue);
      uint16_t colourTempMired = tuyaColourTempToMired(colourLevel);

      if (!setLightColorTemperatureDirect(colourTempMired)) {
        Log::error("Failed to update Zigbee light colour temperature: %d mired", colourTempMired);
      } else {
        Log::debug("Read Zigbee message 'endpoint: %d, cluster: 0x%04X, attribute: 0x%04X: %lu'",
                   _endpoint, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, (uint32_t)colourTempMired);
      }

      confirmColorTemp(colourTempMired);
      reportLightColorTemp();
      Log::info("Light colour temp set to %d mired (%dK) by Skyfan", colourTempMired, miredToKelvin(colourTempMired));
      break;
    }

    default:
      Log::error("Unknown light status update - DPID: %d, Value: %lu", dpid, value);
      break;
  }
}

// Rollback to confirmed state and report
void SkyfanZigbeeLight::rollback() {
  bool stateOk = setLightStateDirect(confirmedLightState);
  bool levelOk = setLightLevelDirect(confirmedLightLevel);
  bool tempOk = setLightColorTemperatureDirect(confirmedColorTemp);
  bool reportStateOk = reportLightState();
  bool reportLevelOk = reportLightLevel();
  bool reportTempOk = reportLightColorTemp();
  Log::info("Rolled back light state=%d(%s/%s), level=%d(%s/%s), temp=%d(%s/%s)",
            confirmedLightState, stateOk ? "ok" : "FAIL", reportStateOk ? "ok" : "FAIL",
            confirmedLightLevel, levelOk ? "ok" : "FAIL", reportLevelOk ? "ok" : "FAIL",
            confirmedColorTemp, tempOk ? "ok" : "FAIL", reportTempOk ? "ok" : "FAIL");
}
