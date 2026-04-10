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

#ifndef SKYFAN_ZIGBEE_LIGHT_H
#define SKYFAN_ZIGBEE_LIGHT_H

#include "Zigbee.h"
#include "SkyfanConfig.h"

class SkyfanZigbeeLight : public ZigbeeColorDimmableLight {
private:
  // Confirmed state (last value acknowledged by MCU)
  bool confirmedLightState = true;
  uint8_t confirmedLightLevel = 127;
  uint16_t confirmedColorTemp = COLOUR_TEMP_WARM_MIRED;

  // Callback suppression flag (prevents echo when MCU reports status)
  bool _suppressCallback = false;

  // Helper to report a single attribute to coordinator
  bool reportAttribute(uint16_t clusterId, uint16_t attributeId);

public:
  SkyfanZigbeeLight(uint8_t endpoint) : ZigbeeColorDimmableLight(endpoint) {}

  // Report attribute values to coordinator
  bool reportLightState();
  bool reportLightLevel();
  bool reportLightColorTemp();

  // Report all light attributes (convenience function for rollback)
  void reportAllAttributes();

  // Confirmed state management (called when MCU confirms via status report)
  void confirmLightState(bool on);
  void confirmLightLevel(uint8_t level);
  void confirmColorTemp(uint16_t mired);
  bool getConfirmedLightState() const;
  uint8_t getConfirmedLightLevel() const;
  uint16_t getConfirmedColorTemp() const;

  // Direct setters for MCU status updates (suppress callback to avoid echo)
  bool setLightStateDirect(bool on);
  bool setLightLevelDirect(uint8_t level);
  bool setLightColorTemperatureDirect(uint16_t mired);

  // Check if callback is currently suppressed (used by setLight callback)
  bool isCallbackSuppressed() const;

  // Rollback to confirmed state and report to coordinator
  void rollback();
};

#endif // SKYFAN_ZIGBEE_LIGHT_H
