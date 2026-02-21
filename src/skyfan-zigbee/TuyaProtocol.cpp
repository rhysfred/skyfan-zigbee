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
#include "Logger.h"

// Helper function to format packet data for debug output
static void debugLogPacket([[maybe_unused]] const char* direction, [[maybe_unused]] uint8_t* packet, [[maybe_unused]] uint16_t len) {
#ifdef __DEBUG__
  char buffer[256];
  int pos = 0;

  pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s MCU message ", direction);

  if (len < 6) {
    // Invalid packet - just print raw bytes
    for (uint16_t i = 0; i < len && pos < (int)sizeof(buffer) - 3; i++) {
      pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%02X", packet[i]);
    }
    Log::debug("%s", buffer);
    return;
  }

  // Format as "55AA-version-command-dataLength-data-checksum"
  uint16_t dataLen = (packet[4] << 8) | packet[5];  // Big-endian data length

  // Header, Version, Command, Data Length
  pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%02X%02X-%02X-%02X-%02X%02X",
                  packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);

  // Data (if any)
  if (dataLen > 0 && pos < (int)sizeof(buffer) - 1) {
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "-");
    for (uint16_t i = 6; i < 6 + dataLen && i < len && pos < (int)sizeof(buffer) - 3; i++) {
      pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%02X", packet[i]);
    }
  }

  // Checksum
  if (len > 6 + dataLen && pos < (int)sizeof(buffer) - 4) {
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "-%02X", packet[6 + dataLen]);
  }

  Log::debug("%s", buffer);
#endif
}

TuyaProtocol::TuyaProtocol(HardwareSerial* serialInterface)
  : lastHeartbeat(0), tuyaConnected(false), deviceStatusCallback(nullptr), serial(serialInterface),
    rxState(TuyaProtocolState::WAIT_HEADER_1), rxIndex(0), expectedLen(0), currentCmd(0),
    mcuNotResponding(false), mcuNotRespondingSince(0),
    queueHead(0), queueTail(0), queueCount(0), processingCommand(false),
    awaitingAck(false), awaitingStatus(false), commandSentTime(0), currentDpidForStatus(0) {
  // Initialize command queue
  for (int i = 0; i < COMMAND_QUEUE_SIZE; i++) {
    commandQueue[i].active = false;
  }
}

void TuyaProtocol::begin(uint32_t baudRate) {
  serial->begin(baudRate);
}

void TuyaProtocol::update(bool zigbeeConnected) {
  processResponse(zigbeeConnected);

  // Process command queue
  processQueue();

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
    Log::error("MCU connection lost - heartbeat timeout");
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
  
  debugLogPacket("Write", packet, idx);

  serial->write(packet, idx);
  serial->flush();
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
          Log::error("Tuya RX buffer overflow - resetting state machine");
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
                Log::debug("Invalid data point length: %d for DPID %d", len, dpid);
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
                Log::debug("Skipping unknown data point type %d for DPID %d", type, dpid);
              }
              
              // Call status callback if we have a valid data point and callback is registered
              if (validDataPoint && deviceStatusCallback) {
                // Check if this status response matches queue command
                handleStatusForQueue(dpid, value);

                deviceStatusCallback(dpid, value);
              }
            }
          } else if (currentCmd == TUYA_CMD_SEND_COMMAND) {
            // ACK received for command we sent
            if (processingCommand && awaitingAck) {
              Log::debug("ACK received for DPID=%d", currentDpidForStatus);
              awaitingAck = false;
              if (currentCommand.tracked) {
                // Transition to waiting for status
                awaitingStatus = true;
              } else {
                // Untracked command - we're done
                processingCommand = false;
                awaitingStatus = false;
              }
            }
          } else if (currentCmd == TUYA_CMD_HEARTBEAT) {
            tuyaConnected = true;
            lastHeartbeat = millis();
            // Heartbeat received - MCU is responding, clear the not responding flag
            if (mcuNotResponding) {
              mcuNotResponding = false;
              Log::debug("MCU not responding flag cleared by heartbeat");
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

          // Log received packet
          debugLogPacket("Read", rxBuffer, rxIndex);

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
      Log::debug("MCU not responding flag reset after 2s timeout");
    }
  }
  return !mcuNotResponding;
}

// =============================================================================
// Command Queue Implementation
// =============================================================================

bool TuyaProtocol::queueCommand(uint8_t dpid, uint8_t type, uint32_t value,
                                 RollbackCallback rollback, bool tracked) {
  // Prevent re-entrant queueing during rollback (e.g., if Zigbee callback fires)
  if (rollbackInProgress) {
    Log::debug("Ignoring command during rollback DPID=%d", dpid);
    return false;
  }

  // If MCU not responding, trigger immediate rollback instead of queueing
  if (!isMcuResponding() && rollback) {
    Log::error("MCU not responding - triggering immediate rollback for DPID=%d", dpid);
    rollbackInProgress = true;
    rollback();
    rollbackInProgress = false;
    return false;
  }

  if (queueCount >= COMMAND_QUEUE_SIZE) {
    Log::debug("Command queue full, dropping command");
    return false;
  }

  QueuedCommand& cmd = commandQueue[queueHead];
  cmd.dpid = dpid;
  cmd.type = type;
  cmd.value = value;
  cmd.rollback = rollback;
  cmd.tracked = tracked;
  cmd.active = true;

  queueHead = (queueHead + 1) % COMMAND_QUEUE_SIZE;
  queueCount++;

  Log::debug("Queued command DPID=%d, type=%d, value=%lu, tracked=%d (queue size: %d)",
             dpid, type, value, tracked, queueCount);

  return true;
}

void TuyaProtocol::processQueue() {
  unsigned long now = millis();

  // State: AWAITING_ACK - check for ACK timeout
  if (processingCommand && awaitingAck) {
    if (now - commandSentTime >= TUYA_COMMAND_TIMEOUT_MS) {
      // ACK timeout - invoke rollback, set mcuNotResponding, clear queue
      Log::error("ACK timeout for DPID=%d - MCU not responding, rolling back", currentCommand.dpid);
      mcuNotResponding = true;
      mcuNotRespondingSince = now;

      if (currentCommand.rollback) {
        rollbackInProgress = true;
        currentCommand.rollback();
        rollbackInProgress = false;
      }

      clearQueue();
      processingCommand = false;
      awaitingAck = false;
      awaitingStatus = false;
      return;
    }
    // Still waiting for ACK - don't process further
    return;
  }

  // State: AWAITING_STATUS - check for status timeout
  if (processingCommand && awaitingStatus) {
    if (now - commandSentTime >= TUYA_STATUS_RESPONSE_TIMEOUT_MS) {
      // Status timeout - invoke rollback
      Log::error("Status timeout for DPID=%d - rolling back", currentCommand.dpid);
      if (currentCommand.rollback) {
        rollbackInProgress = true;
        currentCommand.rollback();
        rollbackInProgress = false;
      }

      processingCommand = false;
      awaitingAck = false;
      awaitingStatus = false;
      // Don't clear queue - just move to next command
    }
    // Still waiting for status - don't process further
    return;
  }

  // State: IDLE - check if we have commands to process
  if (!processingCommand && queueCount > 0) {
    // Dequeue next command
    currentCommand = commandQueue[queueTail];
    commandQueue[queueTail].active = false;
    queueTail = (queueTail + 1) % COMMAND_QUEUE_SIZE;
    queueCount--;

    // Send command (non-blocking - just send, don't wait for response)
    uint8_t data[8];
    uint16_t dataLen = 0;

    data[dataLen++] = currentCommand.dpid;
    data[dataLen++] = currentCommand.type;

    if (currentCommand.type == DP_TYPE_BOOL) {
      data[dataLen++] = 0x00;
      data[dataLen++] = 0x01;
      data[dataLen++] = currentCommand.value ? 0x01 : 0x00;
    } else if (currentCommand.type == DP_TYPE_VALUE) {
      data[dataLen++] = 0x00;
      data[dataLen++] = 0x04;
      data[dataLen++] = (currentCommand.value >> 24) & 0xFF;
      data[dataLen++] = (currentCommand.value >> 16) & 0xFF;
      data[dataLen++] = (currentCommand.value >> 8) & 0xFF;
      data[dataLen++] = currentCommand.value & 0xFF;
    } else if (currentCommand.type == DP_TYPE_ENUM) {
      data[dataLen++] = 0x00;
      data[dataLen++] = 0x01;
      data[dataLen++] = currentCommand.value & 0xFF;
    }

    sendCommand(TUYA_CMD_SEND_COMMAND, data, dataLen);

    processingCommand = true;
    awaitingAck = true;
    awaitingStatus = false;
    commandSentTime = now;
    currentDpidForStatus = currentCommand.dpid;

    Log::debug("Sent queued command DPID=%d, awaiting ACK", currentCommand.dpid);
  }
}

void TuyaProtocol::clearQueue() {
  for (int i = 0; i < COMMAND_QUEUE_SIZE; i++) {
    commandQueue[i].active = false;
  }
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;

  Log::debug("Command queue cleared");
}

bool TuyaProtocol::isQueueEmpty() const {
  return queueCount == 0;
}

uint8_t TuyaProtocol::getQueueCount() const {
  return queueCount;
}

void TuyaProtocol::handleStatusForQueue(uint8_t dpid, [[maybe_unused]] uint32_t value) {
  // Check if we're waiting for status on a tracked command
  if (processingCommand && awaitingStatus && dpid == currentDpidForStatus) {
    Log::debug("Status received for DPID=%d, command confirmed", dpid);
    processingCommand = false;
    awaitingAck = false;
    awaitingStatus = false;
  }
}