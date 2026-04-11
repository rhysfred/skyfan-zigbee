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
    rxState(TuyaProtocolState::WAIT_HEADER_1),
    mcuNotResponding(false), mcuNotRespondingSince(0), radioConnected(false),
    queueHead(0), queueTail(0), queueCount(0),
    queueState(CommandQueueState::IDLE), commandSentTime(0), currentDpidForStatus(0) {
  rxMessage.reset();
}

void TuyaProtocol::begin(uint32_t baudRate) {
  serial->begin(baudRate);
}

void TuyaProtocol::setRadioConnected(bool connected) {
  radioConnected = connected;
}

// Process a single byte through the packet state machine
// Returns true when a complete packet has been received
bool TuyaProtocol::processByte(uint8_t byte) {
  // Header bytes are validated before storing; all other bytes store unconditionally
  if (rxState == TuyaProtocolState::WAIT_HEADER_1) {
    if (byte == 0x55) {
      rxMessage.raw[rxMessage.rawLength++] = byte;
      rxState = TuyaProtocolState::WAIT_HEADER_2;
    }
    return false;
  }

  if (rxState == TuyaProtocolState::WAIT_HEADER_2) {
    if (byte == 0xAA) {
      rxMessage.raw[rxMessage.rawLength++] = byte;
      rxState = TuyaProtocolState::WAIT_VERSION;
    } else {
      rxState = TuyaProtocolState::WAIT_HEADER_1;
      rxMessage.rawLength = 0;
    }
    return false;
  }

  // All remaining states: store byte first, then process
  if (rxMessage.rawLength >= TUYA_RX_BUFFER_SIZE) {
    rxState = TuyaProtocolState::WAIT_HEADER_1;
    rxMessage.reset();
    Log::error("Tuya RX buffer overflow - resetting state machine");
    return false;
  }
  rxMessage.raw[rxMessage.rawLength++] = byte;

  switch (rxState) {
    case TuyaProtocolState::WAIT_VERSION:
      rxMessage.version = byte;
      rxState = TuyaProtocolState::WAIT_COMMAND;
      break;

    case TuyaProtocolState::WAIT_COMMAND:
      rxMessage.command = byte;
      rxState = TuyaProtocolState::WAIT_LENGTH_HIGH;
      break;

    case TuyaProtocolState::WAIT_LENGTH_HIGH:
      rxMessage.dataLength = byte << 8;
      rxState = TuyaProtocolState::WAIT_LENGTH_LOW;
      break;

    case TuyaProtocolState::WAIT_LENGTH_LOW:
      rxMessage.dataLength |= byte;
      rxMessage.data = rxMessage.raw + TUYA_HEADER_SIZE;  // Data starts after header
      rxState = TuyaProtocolState::WAIT_DATA_AND_CHECKSUM;
      break;

    case TuyaProtocolState::WAIT_DATA_AND_CHECKSUM:
      if (rxMessage.isComplete()) {
        // Validate checksum before accepting packet
        uint8_t expectedChecksum = calculateChecksum(rxMessage.raw, rxMessage.rawLength - 1);
        uint8_t receivedChecksum = rxMessage.raw[rxMessage.rawLength - 1];
        if (expectedChecksum != receivedChecksum) {
          Log::error("Checksum mismatch: expected 0x%02X, got 0x%02X", expectedChecksum, receivedChecksum);
          rxState = TuyaProtocolState::WAIT_HEADER_1;
          rxMessage.reset();
          return false;
        }
        return true;  // Complete and valid packet received
      }
      break;

    default:
      break;
  }
  return false;
}

// Check heartbeat - sends heartbeat and waits for first complete packet
// Captures received bytes for boot log diagnostics. Stops after first
// complete packet to avoid consuming header bytes of a subsequent packet.
HeartbeatResult TuyaProtocol::checkHeartbeat() {
  HeartbeatResult result;
  unsigned long startTime = millis();

  // Reset state machine and clear any stale data
  rxState = TuyaProtocolState::WAIT_HEADER_1;
  rxMessage.reset();
  while (serial->available()) {
    serial->read();
  }

  // Send heartbeat request
  sendHeartbeat();

  // Wait for response with timeout - stop after first complete packet
  while ((millis() - startTime) < TUYA_COMMAND_TIMEOUT_MS) {
    if (serial->available()) {
      uint8_t byte = serial->read();

      // Capture byte for diagnostics
      if (result.rxCount < HeartbeatResult::MAX_RX_CAPTURE) {
        result.rxBytes[result.rxCount++] = byte;
      }

      if (processByte(byte)) {
        // First complete packet received - check if heartbeat and stop
        result.success = (rxMessage.command == TUYA_CMD_HEARTBEAT);
        rxState = TuyaProtocolState::WAIT_HEADER_1;
        rxMessage.reset();
        return result;
      }
    }
  }
  return result;
}

int32_t TuyaProtocol::connect(uint32_t baudRate) {
  if (baudRate > 0) {
    // Verify connection at provided baud rate (5 attempts)
    Log::info("Verifying connection at %lu baud", baudRate);

    serial->end();
    delay(10);
    serial->begin(baudRate);

    for (uint8_t attempt = 0; attempt < 5; attempt++) {
      Log::debug("Connection attempt %d/5 at %lu baud", attempt + 1, baudRate);

      HeartbeatResult hbResult = checkHeartbeat();
      Log::boot(baudRate, tuyaBuffer, HEARTBEAT_PACKET_LENGTH,
                hbResult.rxBytes, hbResult.rxCount, hbResult.success);

      if (hbResult.success) {
        Log::info("Connection verified at %lu baud", baudRate);
        return baudRate;
      }

      // Wait remainder of 1-second interval before next attempt
      delay(BAUD_NEGOTIATION_INTERVAL_MS - TUYA_COMMAND_TIMEOUT_MS);
    }

    Log::error("Failed to verify connection at %lu baud", baudRate);
    return 0;
  }

  // Negotiate baud rate (try 9600 first, then 115200)
  const uint32_t baudRates[] = { BAUD_RATE_PRIMARY, BAUD_RATE_SECONDARY };
  const uint8_t numRates = sizeof(baudRates) / sizeof(baudRates[0]);

  Log::info("Starting baud rate negotiation (max %d cycles)", BAUD_NEGOTIATION_MAX_CYCLES);

  for (uint8_t cycle = 0; cycle < BAUD_NEGOTIATION_MAX_CYCLES; cycle++) {
    for (uint8_t rateIdx = 0; rateIdx < numRates; rateIdx++) {
      uint32_t rate = baudRates[rateIdx];
      unsigned long startTime = millis();

      serial->end();
      delay(10);
      serial->begin(rate);

      Log::debug("Trying %lu baud (cycle %d/%d)", rate, cycle + 1, BAUD_NEGOTIATION_MAX_CYCLES);

      HeartbeatResult hbResult = checkHeartbeat();
      Log::boot(rate, tuyaBuffer, HEARTBEAT_PACKET_LENGTH,
                hbResult.rxBytes, hbResult.rxCount, hbResult.success);

      if (hbResult.success) {
        Log::info("Baud rate negotiation successful: %lu baud", rate);
        return rate;
      }

      // Wait remainder of 1-second interval
      unsigned long elapsed = millis() - startTime;
      if (elapsed < BAUD_NEGOTIATION_INTERVAL_MS) {
        delay(BAUD_NEGOTIATION_INTERVAL_MS - elapsed);
      }
    }
  }

  Log::error("Baud rate negotiation failed after %d cycles", BAUD_NEGOTIATION_MAX_CYCLES);
  return -1;
}

void TuyaProtocol::update() {
  processResponse();

  // Process command queue
  processQueue();

  // Send heartbeat every 10 seconds
  static unsigned long lastHeartbeatSent = 0;
  if (millis() - lastHeartbeatSent > TUYA_HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    lastHeartbeatSent = millis();
  }

  // Check connection status
  if (tuyaConnected && (millis() - lastHeartbeat > TUYA_CONNECTION_TIMEOUT_MS)) {
    tuyaConnected = false;
    Log::error("MCU connection lost - heartbeat timeout");
  }

  // Send network status updates when radio connection state changes
  static bool lastRadioState = false;
  static bool firstRun = true;

  if (firstRun || lastRadioState != radioConnected) {
    uint8_t status = radioConnected ? NETWORK_STATUS_CONNECTED : NETWORK_STATUS_NOT_JOINED;
    sendNetworkStatus(status);
    lastRadioState = radioConnected;
    firstRun = false;
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
  
  uint8_t checksum = calculateChecksum(packet, idx);
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

void TuyaProtocol::sendDatapointQuery() {
  sendCommand(TUYA_CMD_DP_QUERY, nullptr, 0);
}

void TuyaProtocol::setDeviceStatusCallback(void (*callback)(uint8_t dpid, uint32_t value)) {
  deviceStatusCallback = callback;
}

void TuyaProtocol::processResponse() {
  while (serial->available()) {
    uint8_t byte = serial->read();

    if (!processByte(byte)) {
      continue;  // Packet not yet complete
    }

    // Complete packet received - handle command
    if (rxMessage.command == TUYA_CMD_STATUS_REPORT) {
      // Parse status report data points
      uint16_t dataIndex = 0;  // Index into rxMessage.data
      while (dataIndex < rxMessage.dataLength) {
        if (dataIndex + 4 > rxMessage.dataLength) {
          break;
        }

        uint8_t dpid = rxMessage.data[dataIndex++];
        uint8_t type = rxMessage.data[dataIndex++];
        uint16_t len = (rxMessage.data[dataIndex] << 8) | rxMessage.data[dataIndex + 1];
        dataIndex += 2;

        if (dataIndex + len > rxMessage.dataLength || len > 8) {
          Log::debug("Invalid data point length: %d for DPID %d", len, dpid);
          break;
        }

        uint32_t value = 0;
        bool validDataPoint = false;

        if (type == DP_TYPE_BOOL && len == 1) {
          value = rxMessage.data[dataIndex];
          dataIndex += 1;
          validDataPoint = true;
        } else if (type == DP_TYPE_VALUE && len == 4) {
          value = (rxMessage.data[dataIndex] << 24) | (rxMessage.data[dataIndex + 1] << 16) |
                  (rxMessage.data[dataIndex + 2] << 8) | rxMessage.data[dataIndex + 3];
          dataIndex += 4;
          validDataPoint = true;
        } else if (type == DP_TYPE_ENUM && len == 1) {
          value = rxMessage.data[dataIndex];
          dataIndex += 1;
          validDataPoint = true;
        } else {
          dataIndex += len;
          Log::debug("Skipping unknown data point type %d for DPID %d", type, dpid);
        }

        if (validDataPoint && deviceStatusCallback) {
          handleStatusForQueue(dpid, value);
          deviceStatusCallback(dpid, value);
        }
      }
    } else if (rxMessage.command == TUYA_CMD_SEND_COMMAND) {
      if (queueState == CommandQueueState::AWAITING_ACK) {
        Log::debug("ACK received for DPID=%d", currentDpidForStatus);
        if (currentCommand.tracked) {
          queueState = CommandQueueState::AWAITING_STATUS;
        } else {
          lastCommandCompletedTime = millis();
          queueState = CommandQueueState::IDLE;
        }
      }
    } else if (rxMessage.command == TUYA_CMD_HEARTBEAT) {
      tuyaConnected = true;
      lastHeartbeat = millis();
      if (mcuNotResponding) {
        mcuNotResponding = false;
        Log::debug("MCU not responding flag cleared by heartbeat");
      }
    } else if (rxMessage.command == TUYA_CMD_PRODUCT_INFO) {
      sendProductInfo();
    } else if (rxMessage.command == TUYA_CMD_QUERY_WORK_MODE) {
      sendWorkMode();
    }

    // Log received packet and reset state machine
    debugLogPacket("Read", rxMessage.raw, rxMessage.rawLength);
    rxState = TuyaProtocolState::WAIT_HEADER_1;
    rxMessage.reset();
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

  queueHead = (queueHead + 1) % COMMAND_QUEUE_SIZE;
  queueCount++;

  Log::debug("Queued command DPID=%d, type=%d, value=%lu, tracked=%d (queue size: %d)",
             dpid, type, value, tracked, queueCount);

  return true;
}

void TuyaProtocol::processQueue() {
  unsigned long now = millis();

  switch (queueState) {
    case CommandQueueState::AWAITING_ACK:
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
        lastCommandCompletedTime = now;
        queueState = CommandQueueState::IDLE;
      }
      // Still waiting for ACK - don't process further
      return;

    case CommandQueueState::AWAITING_STATUS:
      if (now - commandSentTime >= TUYA_STATUS_RESPONSE_TIMEOUT_MS) {
        // Status timeout - invoke rollback
        Log::error("Status timeout for DPID=%d - rolling back", currentCommand.dpid);
        if (currentCommand.rollback) {
          rollbackInProgress = true;
          currentCommand.rollback();
          rollbackInProgress = false;
        }
        lastCommandCompletedTime = now;
        queueState = CommandQueueState::IDLE;
        // Don't clear queue - just move to next command
      }
      // Still waiting for status - don't process further
      return;

    case CommandQueueState::IDLE:
      // Check if we have commands to process
      if (queueCount > 0) {
        // Enforce minimum gap between commands so MCU has time to process
        if (now - lastCommandCompletedTime < TUYA_INTER_COMMAND_DELAY_MS) {
          return;
        }
        // Dequeue next command
        currentCommand = commandQueue[queueTail];
        queueTail = (queueTail + 1) % COMMAND_QUEUE_SIZE;
        queueCount--;

        // Send command (non-blocking - just send, don't wait for response)
        uint8_t data[8];
        uint16_t dataLen = 0;

        data[dataLen++] = currentCommand.dpid;
        data[dataLen++] = currentCommand.type;

        if (currentCommand.type == DP_TYPE_BOOL) {
          data[dataLen++] = 0x00;
          data[dataLen++] = DP_BOOL_PAYLOAD_LENGTH;
          data[dataLen++] = currentCommand.value ? 0x01 : 0x00;
        } else if (currentCommand.type == DP_TYPE_VALUE) {
          data[dataLen++] = 0x00;
          data[dataLen++] = DP_VALUE_PAYLOAD_LENGTH;
          data[dataLen++] = (currentCommand.value >> 24) & 0xFF;
          data[dataLen++] = (currentCommand.value >> 16) & 0xFF;
          data[dataLen++] = (currentCommand.value >> 8) & 0xFF;
          data[dataLen++] = currentCommand.value & 0xFF;
        } else if (currentCommand.type == DP_TYPE_ENUM) {
          data[dataLen++] = 0x00;
          data[dataLen++] = DP_ENUM_PAYLOAD_LENGTH;
          data[dataLen++] = currentCommand.value & 0xFF;
        }

        sendCommand(TUYA_CMD_SEND_COMMAND, data, dataLen);

        queueState = CommandQueueState::AWAITING_ACK;
        commandSentTime = now;
        currentDpidForStatus = currentCommand.dpid;

        Log::debug("Sent queued command DPID=%d, awaiting ACK", currentCommand.dpid);
      }
      break;
  }
}

void TuyaProtocol::clearQueue() {
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
  if (queueState == CommandQueueState::AWAITING_STATUS && dpid == currentDpidForStatus) {
    Log::debug("Status received for DPID=%d, command confirmed", dpid);
    lastCommandCompletedTime = millis();
    queueState = CommandQueueState::IDLE;
  }
}