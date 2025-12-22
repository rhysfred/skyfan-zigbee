/*
 * Tuya Protocol Implementation - Core implementation of Tuya serial protocol communication
 * Copyright (C) 2025 Rhys Frederick at Front Left Speaker
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "TuyaProtocol.h"

TuyaProtocol::TuyaProtocol() 
  : lastHeartbeat(0), tuyaConnected(false), deviceStatusCallback(nullptr), rxState(0), rxIndex(0), expectedLen(0), currentCmd(0) {
}

void TuyaProtocol::begin(uint32_t baudRate) {
  Serial.begin(baudRate);
}

void TuyaProtocol::update(bool zigbeeConnected) {
  processResponse(zigbeeConnected);
  
  // Send heartbeat every 10 seconds
  static unsigned long lastHeartbeatSent = 0;
  if (millis() - lastHeartbeatSent > 10000) {
    sendHeartbeat();
    lastHeartbeatSent = millis();
    // Serial.println("Heartbeat sent to MCU");
  }
  
  // Check connection status
  if (tuyaConnected && (millis() - lastHeartbeat > 30000)) {
    tuyaConnected = false;
    // Serial.println("MCU connection lost!");
  }
  
  // Send network status updates when Zigbee connection state changes
  static bool lastZigbeeState = false;
  static bool firstRun = true;
  
  if (firstRun || lastZigbeeState != zigbeeConnected) {
    uint8_t status = zigbeeConnected ? NETWORK_STATUS_CONNECTED : NETWORK_STATUS_DISCONNECTED;
    sendNetworkStatus(status);
    lastZigbeeState = zigbeeConnected;
    firstRun = false;
    // Serial.printf("Zigbee status change - sent status: %d\n", status);
  }
}

uint8_t TuyaProtocol::calculateChecksum(uint8_t* data, uint16_t len) {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return (uint8_t)(sum & 0xFF);
}

void TuyaProtocol::sendCommand(uint8_t cmd, uint8_t* data, uint16_t len) {
  uint8_t* packet = tuyaBuffer;
  uint16_t idx = 0;
  
  packet[idx++] = (TUYA_HEADER >> 8) & 0xFF;
  packet[idx++] = TUYA_HEADER & 0xFF;
  packet[idx++] = TUYA_VERSION;
  packet[idx++] = cmd;
  packet[idx++] = (len >> 8) & 0xFF;
  packet[idx++] = len & 0xFF;
  
  if (data && len > 0) {
    memcpy(&packet[idx], data, len);
    idx += len;
  }
  
  uint8_t checksum = calculateChecksum(&packet[2], idx - 2);
  packet[idx++] = checksum;
  
  Serial.write(packet, idx);
  Serial.flush();
}

void TuyaProtocol::sendDataPoint(uint8_t dpid, uint8_t type, uint32_t value) {
  uint8_t data[8];
  uint16_t dataLen = 0;
  
  data[dataLen++] = dpid;
  data[dataLen++] = type;
  
  if (type == DP_TYPE_BOOL) {
    data[dataLen++] = 0x00;
    data[dataLen++] = 0x01;
    data[dataLen++] = value ? 0x01 : 0x00;
  } else if (type == DP_TYPE_VALUE || type == DP_TYPE_ENUM) {
    data[dataLen++] = 0x00;
    data[dataLen++] = 0x04;
    data[dataLen++] = (value >> 24) & 0xFF;
    data[dataLen++] = (value >> 16) & 0xFF;
    data[dataLen++] = (value >> 8) & 0xFF;
    data[dataLen++] = value & 0xFF;
  }
  
  sendCommand(TUYA_CMD_SEND_COMMAND, data, dataLen);
  waitForResponse(TUYA_CMD_SEND_COMMAND, 500);
}

// Fan control functions
void TuyaProtocol::setFanSwitch(bool on) {
  sendDataPoint(DP_FAN_SWITCH, DP_TYPE_BOOL, on ? 1 : 0);
}

void TuyaProtocol::setFanSpeed(uint8_t speed) {
  sendDataPoint(DP_FAN_SPEED, DP_TYPE_VALUE, speed);
}

void TuyaProtocol::setFanMode(uint8_t mode) {
  sendDataPoint(DP_FAN_MODE, DP_TYPE_ENUM, mode);
}

void TuyaProtocol::setFanDirection(uint8_t direction) {
  sendDataPoint(DP_FAN_DIRECTION, DP_TYPE_ENUM, direction);
}

// Light control functions
void TuyaProtocol::setLightSwitch(bool on) {
  sendDataPoint(DP_LIGHT_SWITCH, DP_TYPE_BOOL, on ? 1 : 0);
}

void TuyaProtocol::setLightBrightness(uint8_t brightness) {
  sendDataPoint(DP_LIGHT_DIMMER, DP_TYPE_VALUE, brightness);
}

void TuyaProtocol::setLightColourTemp(uint8_t colourTemp) {
  sendDataPoint(DP_LIGHT_COLOUR_TEMP, DP_TYPE_ENUM, colourTemp);
}

void TuyaProtocol::sendHeartbeat() {
  sendCommand(TUYA_CMD_HEARTBEAT, nullptr, 0);
}

void TuyaProtocol::sendNetworkStatus(uint8_t status) {
  sendCommand(TUYA_CMD_NETWORK_STATUS, &status, 1);
}

void TuyaProtocol::setDeviceStatusCallback(void (*callback)(uint8_t dpid, uint32_t value)) {
  deviceStatusCallback = callback;
}


bool TuyaProtocol::waitForResponse(uint8_t expectedCmd, unsigned long timeout) {
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    if (Serial.available() >= 6) {
      if (Serial.read() == 0x55 && Serial.read() == 0xAA) {
        uint8_t version = Serial.read();
        uint8_t cmd = Serial.read();
        uint16_t len = (Serial.read() << 8) | Serial.read();
        
        if (cmd == expectedCmd || expectedCmd == 0xFF) {
          for (uint16_t i = 0; i < len + 1; i++) {
            if (Serial.available()) {
              Serial.read();
            }
          }
          return true;
        } else {
          for (uint16_t i = 0; i < len + 1; i++) {
            if (Serial.available()) {
              Serial.read();
            }
          }
        }
      }
    }
    delay(10);
  }
  return false;
}

void TuyaProtocol::processResponse(bool zigbeeConnected) {
  while (Serial.available()) {
    uint8_t byte = Serial.read();
    
    switch (rxState) {
      case 0:
        if (byte == 0x55) {
          rxBuffer[rxIndex++] = byte;
          rxState = 1;
        }
        break;
        
      case 1:
        if (byte == 0xAA) {
          rxBuffer[rxIndex++] = byte;
          rxState = 2;
        } else {
          rxState = 0;
          rxIndex = 0;
        }
        break;
        
      case 2:
        rxBuffer[rxIndex++] = byte;
        rxState = 3;
        break;
        
      case 3:
        currentCmd = byte;
        rxBuffer[rxIndex++] = byte;
        rxState = 4;
        break;
        
      case 4:
        expectedLen = byte << 8;
        rxBuffer[rxIndex++] = byte;
        rxState = 5;
        break;
        
      case 5:
        expectedLen |= byte;
        rxBuffer[rxIndex++] = byte;
        rxState = 6;
        break;
        
      case 6:
        rxBuffer[rxIndex++] = byte;
        if (rxIndex >= 6 + expectedLen + 1) {
          if (currentCmd == TUYA_CMD_STATUS_REPORT) {
            // Parse status report data points
            uint16_t dataIndex = 6; // Skip header, version, cmd, length
            while (dataIndex < 6 + expectedLen) {
              if (dataIndex + 4 < rxIndex) { // Need at least DPID + Type + Length
                uint8_t dpid = rxBuffer[dataIndex++];
                uint8_t type = rxBuffer[dataIndex++];
                uint16_t len = (rxBuffer[dataIndex] << 8) | rxBuffer[dataIndex + 1];
                dataIndex += 2;
                
                uint32_t value = 0;
                if (type == DP_TYPE_BOOL && len == 1) {
                  value = rxBuffer[dataIndex];
                  dataIndex += 1;
                } else if ((type == DP_TYPE_VALUE || type == DP_TYPE_ENUM) && len == 4) {
                  value = (rxBuffer[dataIndex] << 24) | (rxBuffer[dataIndex + 1] << 16) | 
                          (rxBuffer[dataIndex + 2] << 8) | rxBuffer[dataIndex + 3];
                  dataIndex += 4;
                } else {
                  dataIndex += len; // Skip unknown data point
                }
                
                // Call status callback if registered
                if (deviceStatusCallback) {
                  deviceStatusCallback(dpid, value);
                }
                // Serial.printf("Status update - DPID: %d, Value: %d\n", dpid, value);
              } else {
                break;
              }
            }
          } else if (currentCmd == TUYA_CMD_HEARTBEAT) {
            tuyaConnected = true;
            lastHeartbeat = millis();
          } else if (currentCmd == TUYA_CMD_NETWORK_STATUS) {
            // MCU is requesting network status - respond with current Zigbee connection status
            uint8_t status = zigbeeConnected ? NETWORK_STATUS_CONNECTED : NETWORK_STATUS_DISCONNECTED;
            sendNetworkStatus(status);
            // Serial.printf("Network status request - responding with: %d\n", status);
          }
          
          rxState = 0;
          rxIndex = 0;
          expectedLen = 0;
        }
        break;
    }
  }
}

bool TuyaProtocol::isConnected() const {
  return tuyaConnected;
}