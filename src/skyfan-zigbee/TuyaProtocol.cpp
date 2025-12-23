/*
 * Tuya Protocol Implementation - Core implementation of Tuya serial protocol communication
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

#include "TuyaProtocol.h"

TuyaProtocol::TuyaProtocol() 
  : lastHeartbeat(0), tuyaConnected(false), deviceStatusCallback(nullptr), rxState(TuyaProtocolState::WAIT_HEADER_1), rxIndex(0), expectedLen(0), currentCmd(0) {
}

void TuyaProtocol::begin(uint32_t baudRate) {
  Serial.begin(baudRate);
}

void TuyaProtocol::update(bool zigbeeConnected) {
  processResponse(zigbeeConnected);
  
  // Send heartbeat every 10 seconds
  static unsigned long lastHeartbeatSent = 0;
  if (millis() - lastHeartbeatSent > TUYA_HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatSent = millis();
    // debugSerial.println("Heartbeat sent to MCU");
  }
  
  // Check connection status
  if (tuyaConnected && (millis() - lastHeartbeat > TUYA_CONNECTION_TIMEOUT_MS)) {
    tuyaConnected = false;
    // debugSerial.println("MCU connection lost!");
  }
  
  // Send network status updates when Zigbee connection state changes
  static bool lastZigbeeState = false;
  static bool firstRun = true;
  
  if (firstRun || lastZigbeeState != zigbeeConnected) {
    uint8_t status = zigbeeConnected ? NETWORK_STATUS_CONNECTED : NETWORK_STATUS_DISCONNECTED;
    sendNetworkStatus(status);
    lastZigbeeState = zigbeeConnected;
    firstRun = false;
    // debugSerial.printf("Zigbee status change - sent status: %d\n", status);
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
  waitForResponse(TUYA_CMD_SEND_COMMAND, TUYA_COMMAND_TIMEOUT_MS);
}

// Fan control functions
bool TuyaProtocol::setFanSwitch(bool on) {
  sendDataPoint(DP_FAN_SWITCH, DP_TYPE_BOOL, on ? 1 : 0);
  return true;
}

bool TuyaProtocol::setFanSpeed(uint8_t speed) {
  // Validate fan speed range
  if (!isValidTuyaFanSpeed(speed)) {
    // debugSerial.printf("Invalid fan speed: %d (valid range: %d-%d)\n", speed, TUYA_FAN_SPEED_MIN, TUYA_FAN_SPEED_MAX);
    return false;
  }
  sendDataPoint(DP_FAN_SPEED, DP_TYPE_VALUE, speed);
  return true;
}

bool TuyaProtocol::setFanMode(uint8_t mode) {
  // Validate fan mode
  if (mode > static_cast<uint8_t>(TuyaFanMode::SLEEP)) {
    // debugSerial.printf("Invalid fan mode: %d (valid range: 0-%d)\n", mode, static_cast<uint8_t>(TuyaFanMode::SLEEP));
    return false;
  }
  sendDataPoint(DP_FAN_MODE, DP_TYPE_ENUM, mode);
  return true;
}

bool TuyaProtocol::setFanDirection(uint8_t direction) {
  // Validate fan direction
  if (direction > static_cast<uint8_t>(FanDirection::REVERSE)) {
    // debugSerial.printf("Invalid fan direction: %d (valid range: 0-%d)\n", direction, static_cast<uint8_t>(FanDirection::REVERSE));
    return false;
  }
  sendDataPoint(DP_FAN_DIRECTION, DP_TYPE_ENUM, direction);
  return true;
}

// Light control functions
bool TuyaProtocol::setLightSwitch(bool on) {
  sendDataPoint(DP_LIGHT_SWITCH, DP_TYPE_BOOL, on ? 1 : 0);
  return true;
}

bool TuyaProtocol::setLightBrightness(uint8_t brightness) {
  // Validate brightness range
  if (!isValidTuyaBrightness(brightness)) {
    // debugSerial.printf("Invalid brightness: %d (valid range: %d-%d)\n", brightness, TUYA_BRIGHTNESS_MIN, TUYA_BRIGHTNESS_MAX);
    return false;
  }
  sendDataPoint(DP_LIGHT_DIMMER, DP_TYPE_VALUE, brightness);
  return true;
}

bool TuyaProtocol::setLightColourTemp(uint8_t colourTemp) {
  // Validate colour temperature
  if (colourTemp > static_cast<uint8_t>(ColourTempLevel::COOL)) {
    // debugSerial.printf("Invalid colour temperature: %d (valid range: 0-%d)\n", colourTemp, static_cast<uint8_t>(ColourTempLevel::COOL));
    return false;
  }
  sendDataPoint(DP_LIGHT_COLOUR_TEMP, DP_TYPE_ENUM, colourTemp);
  return true;
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
      case TuyaProtocolState::WAIT_HEADER_1:
        if (byte == 0x55) {
          rxBuffer[rxIndex++] = byte;
          rxState = TuyaProtocolState::WAIT_HEADER_2;
        }
        break;
        
      case TuyaProtocolState::WAIT_HEADER_2:
        if (byte == 0xAA) {
          rxBuffer[rxIndex++] = byte;
          rxState = TuyaProtocolState::WAIT_VERSION;
        } else {
          rxState = TuyaProtocolState::WAIT_HEADER_1;
          rxIndex = 0;
        }
        break;
        
      case TuyaProtocolState::WAIT_VERSION:
        rxBuffer[rxIndex++] = byte;
        rxState = TuyaProtocolState::WAIT_COMMAND;
        break;
        
      case TuyaProtocolState::WAIT_COMMAND:
        currentCmd = byte;
        rxBuffer[rxIndex++] = byte;
        rxState = TuyaProtocolState::WAIT_LENGTH_HIGH;
        break;
        
      case TuyaProtocolState::WAIT_LENGTH_HIGH:
        expectedLen = byte << 8;
        rxBuffer[rxIndex++] = byte;
        rxState = TuyaProtocolState::WAIT_LENGTH_LOW;
        break;
        
      case TuyaProtocolState::WAIT_LENGTH_LOW:
        expectedLen |= byte;
        rxBuffer[rxIndex++] = byte;
        rxState = TuyaProtocolState::WAIT_DATA_AND_CHECKSUM;
        break;
        
      case TuyaProtocolState::WAIT_DATA_AND_CHECKSUM:
        // Prevent buffer overflow
        if (rxIndex < TUYA_RX_BUFFER_SIZE) {
          rxBuffer[rxIndex++] = byte;
        } else {
          // Buffer overflow protection - reset state machine
          rxState = TuyaProtocolState::WAIT_HEADER_1;
          rxIndex = 0;
          expectedLen = 0;
          // debugSerial.println("Buffer overflow protection triggered");
          break;
        }
        
        if (rxIndex >= 6 + expectedLen + 1) {
          if (currentCmd == TUYA_CMD_STATUS_REPORT) {
            // Parse status report data points
            uint16_t dataIndex = 6; // Skip header, version, cmd, length
            while (dataIndex < 6 + expectedLen && dataIndex < rxIndex) {
              // Validate we have enough bytes for header (DPID + Type + Length = 4 bytes)
              if (dataIndex + 4 > rxIndex) {
                break;
              }
              
              uint8_t dpid = rxBuffer[dataIndex++];
              uint8_t type = rxBuffer[dataIndex++];
              uint16_t len = (rxBuffer[dataIndex] << 8) | rxBuffer[dataIndex + 1];
              dataIndex += 2;
              
              // Validate data length doesn't exceed remaining buffer
              if (dataIndex + len > rxIndex || len > 8) {  // Sanity check on length
                // debugSerial.printf("Invalid data point length: %d at index %d\n", len, dataIndex);
                break;
              }
              
              uint32_t value = 0;
              bool validDataPoint = false;
              
              if (type == DP_TYPE_BOOL && len == 1) {
                value = rxBuffer[dataIndex];
                dataIndex += 1;
                validDataPoint = true;
              } else if ((type == DP_TYPE_VALUE || type == DP_TYPE_ENUM) && len == 4) {
                value = (rxBuffer[dataIndex] << 24) | (rxBuffer[dataIndex + 1] << 16) | 
                        (rxBuffer[dataIndex + 2] << 8) | rxBuffer[dataIndex + 3];
                dataIndex += 4;
                validDataPoint = true;
              } else {
                // Skip unknown or invalid data point
                dataIndex += len;
                // debugSerial.printf("Skipping unknown data point - DPID: %d, Type: %d, Len: %d\n", dpid, type, len);
              }
              
              // Call status callback if we have a valid data point and callback is registered
              if (validDataPoint && deviceStatusCallback) {
                deviceStatusCallback(dpid, value);
                // debugSerial.printf("Status update - DPID: %d, Value: %d\n", dpid, value);
              }
            }
          } else if (currentCmd == TUYA_CMD_HEARTBEAT) {
            tuyaConnected = true;
            lastHeartbeat = millis();
          } else if (currentCmd == TUYA_CMD_NETWORK_STATUS) {
            // MCU is requesting network status - respond with current Zigbee connection status
            uint8_t status = zigbeeConnected ? NETWORK_STATUS_CONNECTED : NETWORK_STATUS_DISCONNECTED;
            sendNetworkStatus(status);
            // debugSerial.printf("Network status request - responding with: %d\n", status);
          }
          
          rxState = TuyaProtocolState::WAIT_HEADER_1;
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