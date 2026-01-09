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
public:
  SkyfanZigbeeLight(uint8_t endpoint) : ZigbeeColorDimmableLight(endpoint) {}

  // Report attribute values to coordinator
  bool reportLightState();
  bool reportLightLevel();
  bool reportLightColorTemp();

  // Report all light attributes (convenience function for rollback)
  void reportAllAttributes();
};

#endif // SKYFAN_ZIGBEE_LIGHT_H
