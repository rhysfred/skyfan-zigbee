/*
 * Tuya Protocol Library - Serial protocol implementation for Tuya MCU communication
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

#ifndef TUYA_PROTOCOL_H
#define TUYA_PROTOCOL_H

#include <Arduino.h>
#include <functional>
#include "SkyfanConfig.h"

// Rollback callback type for command queue
using RollbackCallback = std::function<void()>;

// Queue configuration
#define COMMAND_QUEUE_SIZE 16

// Queued command structure for the command queue
struct QueuedCommand {
  uint8_t dpid;
  uint8_t type;           // DP_TYPE_BOOL, DP_TYPE_VALUE, DP_TYPE_ENUM
  uint32_t value;
  RollbackCallback rollback;
  bool tracked;           // Wait for status confirmation (1.5s timeout)
  bool active;            // Slot in use

  QueuedCommand() : dpid(0), type(0), value(0), rollback(nullptr), tracked(true), active(false) {}
};

// Tuya Serial Protocol Configuration
#define TUYA_HEADER 0x55AA
#define TUYA_VERSION_MODULE_TO_MCU 0x00  // Module (us) sending to MCU
#define TUYA_VERSION_MCU_TO_MODULE 0x03  // MCU responding to module
#define TUYA_CMD_HEARTBEAT 0x00
#define TUYA_CMD_PRODUCT_INFO 0x01
#define TUYA_CMD_QUERY_WORK_MODE 0x02
#define TUYA_CMD_NETWORK_STATUS 0x03
#define TUYA_CMD_RESET_WIFI 0x04
#define TUYA_CMD_SEND_COMMAND 0x06
#define TUYA_CMD_STATUS_REPORT 0x07

// Fan Control Data Points (DPIDs)
#define DP_FAN_SWITCH 1       // Boolean: Fan on/off
#define DP_FAN_MODE 2         // Enum: 0=normal, 1=eco, 2=sleep
#define DP_FAN_SPEED 3        // Value: Fan speed (0-5)
#define DP_FAN_DIRECTION 8    // Enum: 0=forward, 1=reverse

// Light Control Data Points (DPIDs)
#define DP_LIGHT_SWITCH 15    // Boolean: Light on/off
#define DP_LIGHT_DIMMER 16    // Value: Light brightness (0-5)
#define DP_LIGHT_COLOUR_TEMP 19 // Enum: 0=warm(3000K), 1=natural(4200K), 2=cool(6500K)

// Data Point Types
#define DP_TYPE_BOOL 0x01
#define DP_TYPE_VALUE 0x02
#define DP_TYPE_ENUM 0x04

// Fan Mode Values
#define FAN_MODE_NORMAL 0
#define FAN_MODE_ECO 1
#define FAN_MODE_SLEEP 2

// Fan Direction Values
#define FAN_DIRECTION_FORWARD 0
#define FAN_DIRECTION_REVERSE 1

// Light Colour Temperature Values
#define COLOUR_TEMP_WARM 0    // 3000K
#define COLOUR_TEMP_NATURAL 1 // 4200K
#define COLOUR_TEMP_COOL 2    // 6500K

// Network Status Codes (WiFi protocol mapped to Zigbee states)
#define NETWORK_STATUS_NOT_JOINED     0x00  // Zigbee not joined to network
#define NETWORK_STATUS_JOINED_NO_CONN 0x02  // Zigbee joined but not connected to coordinator
#define NETWORK_STATUS_CONNECTED      0x03  // Zigbee connected to coordinator

class TuyaProtocol {
private:
  uint8_t tuyaBuffer[TUYA_BUFFER_SIZE];
  unsigned long lastHeartbeat;
  bool tuyaConnected;
  void (*deviceStatusCallback)(uint8_t dpid, uint32_t value);
  HardwareSerial* serial;

  // Internal state for response processing
  TuyaProtocolState rxState;
  uint8_t rxBuffer[TUYA_RX_BUFFER_SIZE];
  uint16_t rxIndex;
  uint16_t expectedLen;
  uint8_t currentCmd;

  // MCU responsiveness tracking - when ACK timeout occurs, bypass Tuya for 2s
  bool mcuNotResponding;
  unsigned long mcuNotRespondingSince;

  // Command queue (circular buffer)
  QueuedCommand commandQueue[COMMAND_QUEUE_SIZE];
  uint8_t queueHead = 0;  // Next position to write
  uint8_t queueTail = 0;  // Next position to read
  uint8_t queueCount = 0; // Number of items in queue

  // Current command processing state
  bool processingCommand = false;
  QueuedCommand currentCommand;
  bool awaitingAck = false;
  bool awaitingStatus = false;
  unsigned long commandSentTime = 0;
  uint8_t currentDpidForStatus = 0;  // DPID we're waiting for status on

public:
  TuyaProtocol(HardwareSerial* serialInterface);
  
  void begin(uint32_t baudRate = MCU_SERIAL_BAUD_RATE);
  void update(bool zigbeeConnected);
  
  // Core protocol functions
  void sendCommand(uint8_t cmd, uint8_t* data, uint16_t len);
  void sendHeartbeat();
  void sendNetworkStatus(uint8_t status);
  void sendProductInfo();
  void sendWorkMode();

  // Status functions
  bool isConnected() const;
  bool isMcuResponding();  // Returns false if MCU ACK timed out recently (within 2s)
  void processResponse(bool zigbeeConnected);
  void setDeviceStatusCallback(void (*callback)(uint8_t dpid, uint32_t value));

  // Utility functions
  static uint8_t calculateChecksum(uint8_t* data, uint16_t len);

  // Command queue functions
  bool queueCommand(uint8_t dpid, uint8_t type, uint32_t value,
                    RollbackCallback rollback = nullptr, bool tracked = true);
  void processQueue();
  void clearQueue();
  bool isQueueEmpty() const;
  uint8_t getQueueCount() const;

  // Status matching for queue (called from processResponse)
  void handleStatusForQueue(uint8_t dpid, uint32_t value);
};

#endif // TUYA_PROTOCOL_H