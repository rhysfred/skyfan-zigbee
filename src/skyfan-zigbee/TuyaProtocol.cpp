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

#ifdef __DEBUG__
// Helper function to format packet data for debug output
void debugLogPacket(const char* direction, uint8_t* packet, uint16_t len) {
  Serial.print("DEBUG: ");
  Serial.print(direction);
  Serial.print(" MCU message ");
  
  if (len < 6) {
    // Invalid packet - just print raw bytes
    for (uint16_t i = 0; i < len; i++) {
      Serial.printf("%02X", packet[i]);
    }
    Serial.println();
    return;
  }
  
  // Format as "55AA-version-command-dataLength-data-checksum"
  uint16_t dataLen = (packet[4] << 8) | packet[5];  // Big-endian data length
  
  // Header (55AA)
  Serial.printf("%02X%02X-", packet[0], packet[1]);
  
  // Version
  Serial.printf("%02X-", packet[2]);
  
  // Command
  Serial.printf("%02X-", packet[3]);
  
  // Data Length (2 bytes)
  Serial.printf("%02X%02X", packet[4], packet[5]);
  
  // Data (if any)
  if (dataLen > 0) {
    Serial.print("-");
    for (uint16_t i = 6; i < 6 + dataLen && i < len; i++) {
      Serial.printf("%02X", packet[i]);
    }
  }
  
  // Checksum
  if (len > 6 + dataLen) {
    Serial.print("-");
    Serial.printf("%02X", packet[6 + dataLen]);
  }
  
  Serial.println();
}
#endif

TuyaProtocol::TuyaProtocol(HardwareSerial* serialInterface)
  : lastHeartbeat(0), tuyaConnected(false), deviceStatusCallback(nullptr), rollbackCallback(nullptr), serial(serialInterface), rxState(TuyaProtocolState::WAIT_HEADER_1), rxIndex(0), expectedLen(0), currentCmd(0), mcuNotResponding(false), mcuNotRespondingSince(0) {
  // Initialize pending commands array
  for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
    pendingCommands[i].active = false;
  }
}

void TuyaProtocol::begin(uint32_t baudRate) {
  serial->begin(baudRate);
}

void TuyaProtocol::update(bool zigbeeConnected) {
  processResponse(zigbeeConnected);
  
  // Send heartbeat every 10 seconds
  static unsigned long lastHeartbeatSent = 0;
  if (millis() - lastHeartbeatSent > TUYA_HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatSent = millis();
    // Heartbeat sent to MCU
  }
  
  // Check connection status
  if (tuyaConnected && (millis() - lastHeartbeat > TUYA_CONNECTION_TIMEOUT_MS)) {
    tuyaConnected = false;
    Serial.println("ERROR: MCU connection lost - heartbeat timeout");
  }
  
  // Send network status updates when Zigbee connection state changes
  static bool lastZigbeeState = false;
  static bool firstRun = true;
  
  if (firstRun || lastZigbeeState != zigbeeConnected) {
    uint8_t status = zigbeeConnected ? NETWORK_STATUS_CONNECTED : NETWORK_STATUS_NOT_JOINED;
    sendNetworkStatus(status);
    lastZigbeeState = zigbeeConnected;
    firstRun = false;
    // Zigbee status change - sent status
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
  packet[idx++] = TUYA_VERSION_MODULE_TO_MCU;
  packet[idx++] = cmd;
  packet[idx++] = (len >> 8) & 0xFF;
  packet[idx++] = len & 0xFF;
  
  if (data && len > 0) {
    memcpy(&packet[idx], data, len);
    idx += len;
  }
  
  uint8_t checksum = calculateChecksum(&packet[2], idx - 2);
  packet[idx++] = checksum;
  
#ifdef __DEBUG__
  debugLogPacket("Write", packet, idx);
#endif
  
  serial->write(packet, idx);
  serial->flush();
}

bool TuyaProtocol::sendDataPoint(uint8_t dpid, uint8_t type, uint32_t value) {
  uint8_t data[8];
  uint16_t dataLen = 0;

  data[dataLen++] = dpid;
  data[dataLen++] = type;

  if (type == DP_TYPE_BOOL) {
    data[dataLen++] = 0x00;
    data[dataLen++] = 0x01;
    data[dataLen++] = value ? 0x01 : 0x00;
  } else if (type == DP_TYPE_VALUE) {
    data[dataLen++] = 0x00;
    data[dataLen++] = 0x04;
    // 4-byte value in big-endian format
    data[dataLen++] = (value >> 24) & 0xFF;
    data[dataLen++] = (value >> 16) & 0xFF;
    data[dataLen++] = (value >> 8) & 0xFF;
    data[dataLen++] = value & 0xFF;
  } else if (type == DP_TYPE_ENUM) {
    data[dataLen++] = 0x00;
    data[dataLen++] = 0x01;
    // 1-byte enum value
    data[dataLen++] = value & 0xFF;
  }

  sendCommand(TUYA_CMD_SEND_COMMAND, data, dataLen);
  bool gotResponse = waitForResponse(TUYA_CMD_SEND_COMMAND, TUYA_COMMAND_TIMEOUT_MS);

  // If ACK timed out, set the not responding flag
  if (!gotResponse) {
    mcuNotResponding = true;
    mcuNotRespondingSince = millis();
#ifdef __DEBUG__
    Serial.println("DEBUG: MCU ACK timeout - setting not responding flag");
#endif
  }

  return gotResponse;
}

// Fan control functions
bool TuyaProtocol::setFanSwitch(bool on) {
  sendDataPoint(DP_FAN_SWITCH, DP_TYPE_BOOL, on ? 1 : 0);
  return true;
}

bool TuyaProtocol::setFanSpeed(uint8_t speed) {
  // Validate fan speed range
  if (!isValidTuyaFanSpeed(speed)) {
#ifdef __DEBUG__
    Serial.printf("DEBUG: Invalid fan speed value: %d\n", speed);
#endif
    return false;
  }
  sendDataPoint(DP_FAN_SPEED, DP_TYPE_VALUE, speed);
  return true;
}

bool TuyaProtocol::setFanMode(uint8_t mode) {
  // Validate fan mode
  if (mode > static_cast<uint8_t>(TuyaFanMode::SLEEP)) {
#ifdef __DEBUG__
    Serial.printf("DEBUG: Invalid fan mode value: %d\n", mode);
#endif
    return false;
  }
  sendDataPoint(DP_FAN_MODE, DP_TYPE_ENUM, mode);
  return true;
}

bool TuyaProtocol::setFanDirection(uint8_t direction) {
  // Validate fan direction
  if (direction > static_cast<uint8_t>(FanDirection::REVERSE)) {
#ifdef __DEBUG__
    Serial.printf("DEBUG: Invalid fan direction value: %d\n", direction);
#endif
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
#ifdef __DEBUG__
    Serial.printf("DEBUG: Invalid brightness value: %d\n", brightness);
#endif
    return false;
  }
  sendDataPoint(DP_LIGHT_DIMMER, DP_TYPE_VALUE, brightness);
  return true;
}

bool TuyaProtocol::setLightColourTemp(uint8_t colourTemp) {
  // Validate colour temperature
  if (colourTemp > static_cast<uint8_t>(ColourTempLevel::COOL)) {
#ifdef __DEBUG__
    Serial.printf("DEBUG: Invalid colour temperature value: %d\n", colourTemp);
#endif
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

void TuyaProtocol::sendProductInfo() {
  // Send minimal product info response - empty data
  sendCommand(TUYA_CMD_PRODUCT_INFO, nullptr, 0);
}

void TuyaProtocol::sendWorkMode() {
  // Send work mode response - 0x00 for standard mode
  uint8_t workMode = 0x00;
  sendCommand(TUYA_CMD_QUERY_WORK_MODE, &workMode, 1);
}

void TuyaProtocol::setDeviceStatusCallback(void (*callback)(uint8_t dpid, uint32_t value)) {
  deviceStatusCallback = callback;
}


bool TuyaProtocol::waitForResponse(uint8_t expectedCmd, unsigned long timeout) {
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    if (serial->available() >= 6) {
      if (serial->read() == 0x55 && serial->read() == 0xAA) {
        uint8_t version = serial->read();
        uint8_t cmd = serial->read();
        uint16_t len = (serial->read() << 8) | serial->read();
        
        if (cmd == expectedCmd || expectedCmd == 0xFF) {
          for (uint16_t i = 0; i < len + 1; i++) {
            if (serial->available()) {
              serial->read();
            }
          }
          return true;
        } else {
          for (uint16_t i = 0; i < len + 1; i++) {
            if (serial->available()) {
              serial->read();
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
  while (serial->available()) {
    uint8_t byte = serial->read();
    
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
          Serial.println("ERROR: Tuya RX buffer overflow - resetting state machine");
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
#ifdef __DEBUG__
                Serial.printf("DEBUG: Invalid data point length: %d for DPID %d\n", len, dpid);
#endif
                break;
              }
              
              uint32_t value = 0;
              bool validDataPoint = false;
              
              if (type == DP_TYPE_BOOL && len == 1) {
                value = rxBuffer[dataIndex];
                dataIndex += 1;
                validDataPoint = true;
              } else if (type == DP_TYPE_VALUE && len == 4) {
                // 4-byte value in big-endian format
                value = (rxBuffer[dataIndex] << 24) | (rxBuffer[dataIndex + 1] << 16) | 
                        (rxBuffer[dataIndex + 2] << 8) | rxBuffer[dataIndex + 3];
                dataIndex += 4;
                validDataPoint = true;
              } else if (type == DP_TYPE_ENUM && len == 1) {
                // 1-byte enum value
                value = rxBuffer[dataIndex];
                dataIndex += 1;
                validDataPoint = true;
              } else {
                // Skip unknown or invalid data point
                dataIndex += len;
#ifdef __DEBUG__
                Serial.printf("DEBUG: Skipping unknown data point type %d for DPID %d\n", type, dpid);
#endif
              }
              
              // Call status callback if we have a valid data point and callback is registered
              if (validDataPoint && deviceStatusCallback) {
                // Check if this status response matches a pending command
                isStatusResponseForPendingCommand(dpid, value);
                
                deviceStatusCallback(dpid, value);
                // Status update received
              }
            }
          } else if (currentCmd == TUYA_CMD_HEARTBEAT) {
            tuyaConnected = true;
            lastHeartbeat = millis();
            // Heartbeat received - MCU is responding, clear the not responding flag
            if (mcuNotResponding) {
              mcuNotResponding = false;
#ifdef __DEBUG__
              Serial.println("DEBUG: MCU not responding flag cleared by heartbeat");
#endif
            }
          } else if (currentCmd == TUYA_CMD_PRODUCT_INFO) {
            // MCU is requesting product info - send basic response
            sendProductInfo();
          } else if (currentCmd == TUYA_CMD_QUERY_WORK_MODE) {
            // MCU is requesting work mode - send basic response  
            sendWorkMode();
          } else if (currentCmd == TUYA_CMD_NETWORK_STATUS) {
            // MCU is requesting network status - respond with current Zigbee connection status
            uint8_t status = zigbeeConnected ? NETWORK_STATUS_CONNECTED : NETWORK_STATUS_NOT_JOINED;
            sendNetworkStatus(status);
            // Network status request received
          }
          
#ifdef __DEBUG__
          // Log received packet
          debugLogPacket("Read", rxBuffer, rxIndex);
#endif
          
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

bool TuyaProtocol::isMcuResponding() {
  // If flag is set, check if 2 seconds have elapsed to reset it
  if (mcuNotResponding) {
    if (millis() - mcuNotRespondingSince >= MCU_NOT_RESPONDING_BYPASS_MS) {
      mcuNotResponding = false;
#ifdef __DEBUG__
      Serial.println("DEBUG: MCU not responding flag reset after 2s timeout");
#endif
    }
  }
  return !mcuNotResponding;
}

// Set callback for rollback operations
void TuyaProtocol::setRollbackCallback(void (*callback)(CommandType type)) {
  rollbackCallback = callback;
}

// Send data point with command tracking
bool TuyaProtocol::sendDataPointWithTracking(uint8_t dpid, uint8_t type, uint32_t value, CommandType cmdType) {
  // Send command and wait for ACK (synchronous)
  bool ackReceived = sendDataPoint(dpid, type, value);

  // Only track for 1.5s status response if ACK was received
  // If ACK failed, mcuNotResponding flag is already set - no point waiting for status
  if (ackReceived) {
    addPendingCommand(dpid, value, cmdType);
  }

  return ackReceived;
}

// Add command to pending list
void TuyaProtocol::addPendingCommand(uint8_t dpid, uint32_t expectedValue, CommandType cmdType) {
  // Find empty slot
  for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
    if (!pendingCommands[i].active) {
      pendingCommands[i].active = true;
      pendingCommands[i].dpid = dpid;
      pendingCommands[i].expectedValue = expectedValue;
      pendingCommands[i].commandType = cmdType;
      pendingCommands[i].statusTimeout = millis() + TUYA_STATUS_RESPONSE_TIMEOUT_MS;
      return;
    }
  }
  // If we get here, no slots available - oldest command will timeout anyway
}

// Clear pending command
void TuyaProtocol::clearPendingCommand(int index) {
  if (index >= 0 && index < MAX_PENDING_COMMANDS) {
    pendingCommands[index].active = false;
  }
}

// Check if incoming status matches a pending command
bool TuyaProtocol::isStatusResponseForPendingCommand(uint8_t dpid, uint32_t value) {
  for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
    if (pendingCommands[i].active && 
        pendingCommands[i].dpid == dpid && 
        pendingCommands[i].expectedValue == value) {
      clearPendingCommand(i);
      return true;
    }
  }
  return false;
}

// Check for command timeouts
void TuyaProtocol::checkPendingCommandTimeouts() {
  unsigned long now = millis();
  
  for (int i = 0; i < MAX_PENDING_COMMANDS; i++) {
    if (pendingCommands[i].active && now >= pendingCommands[i].statusTimeout) {
      // Command timed out - trigger rollback
      if (rollbackCallback) {
        rollbackCallback(pendingCommands[i].commandType);
      }
      clearPendingCommand(i);
    }
  }
}