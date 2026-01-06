# Building a Zigbee Device with ESP32-C6

A practical guide to developing Zigbee 3.0 devices using the ESP32-C6 and Arduino framework.

This guide uses the [Skyfan Zigbee Controller](src/skyfan-zigbee/) as a worked example throughout. It's a ceiling fan controller that talks to a Tuya MCU, which is probably more complexity than you need, but the Zigbee bits are broadly applicable to whatever you're building.

## Table of Contents

1. [Zigbee in 60 Seconds](#zigbee-in-60-seconds)
2. [Development Environment Setup](#development-environment-setup)
3. [Creating Endpoints](#creating-endpoints)
4. [Handling Commands](#handling-commands)
5. [Reporting State Back](#reporting-state-back)
6. [Custom Clusters](#custom-clusters)
7. [OTA Updates](#ota-updates)
8. [Zigbee2MQTT Integration](#zigbee2mqtt-integration)

---

## Zigbee in 60 Seconds

Zigbee networks have three device types:

- **Coordinator**: The boss. Forms the network. You have exactly one (your gateway/hub).
- **Router**: Mains-powered devices that relay messages and extend range. This is probably what you're building.
- **End Device**: Battery-powered things that sleep a lot. Sensors, buttons, that sort of thing.

The important concepts:

**Endpoints** (1-240): A device can expose multiple endpoints. Think of them as separate "things" on one physical device. A fan/light combo might have endpoint 1 for the fan and endpoint 2 for the light.

**Clusters**: Standardised groupings of functionality. The Zigbee Cluster Library (ZCL) defines these so devices can interoperate. Common ones:
- `On/Off` (0x0006) - switches
- `Level Control` (0x0008) - dimmers
- `Fan Control` (0x0202) - fans (surprisingly)
- `Color Control` (0x0300) - colour temperature, RGB, etc.

**Attributes**: Properties within a cluster. The `Fan Control` cluster has a `FanMode` attribute (Off/Low/Medium/High/On).

**Commands**: Actions on clusters. Unlike attributes (which you read/write), commands trigger behaviours.

The ESP32 Arduino library gives you C++ classes that wrap all this up nicely:

| Class | What It Creates |
|-------|-----------------|
| `ZigbeeFanControl` | Fan endpoint with Fan Control cluster |
| `ZigbeeLight` | On/Off light |
| `ZigbeeDimmableLight` | Dimmable light |
| `ZigbeeColorDimmableLight` | Colour temperature light |

---

## Development Environment Setup

### Arduino IDE Configuration

1. **Install ESP32 boards**: Preferences → Additional Board Manager URLs → add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
   Then Board Manager → search "esp32" → install v3.3.5+

2. **Select your board**: Tools → Board → ESP32 Arduino → your ESP32-C6 variant

3. **The settings that actually matter** (get these wrong and nothing works):
   - **Partition Scheme**: "Zigbee ZCZR 4MB with spiffs"
   - **Zigbee Mode**: "Zigbee ZCZR (coordinator/router)"

### Compiler Check

Add this at the top of your sketch - it'll fail compilation with a helpful message if the Zigbee mode isn't set:

```cpp
#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator mode is not selected in Tools->Zigbee mode"
#endif
```

---

## Creating Endpoints

### The Basics

```cpp
#include "Zigbee.h"

ZigbeeFanControl zbFan(1);  // Endpoint 1

void setup() {
  zbFan.setManufacturerAndModel("My Company", "My Device");
  zbFan.setPowerSource(ZB_POWER_SOURCE_MAINS);

  Zigbee.addEndpoint(&zbFan);

  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    ESP.restart();  // If it fails, just reboot
  }

  while (!Zigbee.connected()) {
    delay(100);
  }
}
```

**Worked example** - This creates a fan endpoint and optionally a light endpoint based on build configuration ([`skyfan-zigbee.ino:43-46`](src/skyfan-zigbee/skyfan-zigbee.ino)):

```cpp
SkyfanZigbeeFanControl zbFanControl = SkyfanZigbeeFanControl(ZIGBEE_FAN_CONTROL_ENDPOINT);
#ifdef WITH_LIGHT
ZigbeeColorDimmableLight zbLight = ZigbeeColorDimmableLight(ZIGBEE_LIGHT_CONTROL_ENDPOINT);
#endif
```

The `#ifdef WITH_LIGHT` pattern lets one codebase produce two firmware variants. The same approach works for any optional features.

### Configuration Options

Before adding the endpoint, configure it:

```cpp
// For fans - set which modes are valid
zbFan.setFanModeSequence(FAN_MODE_SEQUENCE_LOW_MED_HIGH);

// For colour temperature lights - set the range
zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
zbLight.setLightColorTemperatureRange(154, 333);  // Mired values (154=6500K, 333=3000K)
```

**Worked example** - This configures both endpoints ([`skyfan-zigbee.ino:426-435`](src/skyfan-zigbee/skyfan-zigbee.ino)):

```cpp
#ifdef WITH_LIGHT
zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
zbLight.setLightColorTemperatureRange(ZIGBEE_COLOUR_TEMP_MIN_MIRED, ZIGBEE_COLOUR_TEMP_MAX_MIRED);
#endif

zbFanControl.setFanModeSequence(FAN_MODE_SEQUENCE_LOW_MED_HIGH);
```

---

## Handling Commands

Register callbacks to respond when the coordinator sends commands:

```cpp
void onFanModeChange(ZigbeeFanMode mode) {
  switch (mode) {
    case FAN_MODE_OFF:  /* turn it off */  break;
    case FAN_MODE_LOW:  /* slow */         break;
    case FAN_MODE_MEDIUM: /* medium */     break;
    case FAN_MODE_HIGH: /* fast */         break;
  }
}

void setup() {
  zbFan.onFanModeChange(onFanModeChange);
  // ... rest of setup
}
```

**Worked example** - In the following snippet from the Skyfan controller, the setFan method is called when a zigbee fan mode message is received (a.k.a. change speed) and maps Zigbee modes to Tuya MCU commands ([`skyfan-zigbee.ino:64-99`](src/skyfan-zigbee/skyfan-zigbee.ino)):

```cpp
void setFan(ZigbeeFanMode mode) {
  statusLed.flashCommand();  // Blink to show we got something

  switch (mode) {
    case FAN_MODE_OFF:
      tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 0, CommandType::FAN_SWITCH);
      break;
    case FAN_MODE_LOW:
      tuya.sendDataPointWithTracking(DP_FAN_SWITCH, DP_TYPE_BOOL, 1, CommandType::FAN_SWITCH);
      tuya.sendDataPointWithTracking(DP_FAN_SPEED, DP_TYPE_VALUE, FAN_SPEED_LOW_TUYA, CommandType::FAN_SPEED);
      break;
    // ... etc
  }
}
```

The `sendDataPointWithTracking` bit is Skyfan-specific (it tracks commands for rollback if the MCU doesn't respond). Your implementation will be whatever actually controls your device.

---

## Reporting State Back

Here's where things get interesting. A lot of the standard Arduino Zigbee classes let you *receive* commands but don't expose methods to *report* state changes back. This is a problem when:

- Someone uses a physical remote/button
- The device changes state on its own
- You want to sync state after reconnection

### The Solution: Extend the Class

You need to add setter methods that call into the ESP-Zigbee-SDK directly:

```cpp
class MyFanControl : public ZigbeeFanControl {
public:
  MyFanControl(uint8_t endpoint) : ZigbeeFanControl(endpoint) {}

  bool setFanMode(ZigbeeFanMode mode) {
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_err_t ret = esp_zb_zcl_set_attribute_val(
      _endpoint,
      ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID,
      &mode,
      false
    );
    esp_zb_lock_release();
    return ret == ESP_OK;
  }
};
```

**Worked example** - An extended class for Fans ([`SkyfanZigbee.h:30-69`](src/skyfan-zigbee/SkyfanZigbee.h)):

```cpp
class SkyfanZigbeeFanControl : public ZigbeeFanControl {
public:
  SkyfanZigbeeFanControl(uint8_t endpoint) : ZigbeeFanControl(endpoint) {}

  bool setFanMode(ZigbeeFanMode mode);
  bool setFanState(bool on);
  bool setFanSpeed(uint8_t speed);
  bool setFanDirection(uint8_t direction);
  // ...
};
```

Then when your device state changes externally, update Zigbee:

```cpp
void onPhysicalButtonPress() {
  currentState = !currentState;
  zbFan.setFanMode(currentState ? FAN_MODE_ON : FAN_MODE_OFF);
}
```

**Worked example** - Skyfan updates Zigbee when the MCU reports status ([`skyfan-zigbee.ino:164-212`](src/skyfan-zigbee/skyfan-zigbee.ino)):

```cpp
void handleFanSpeedStatus(uint32_t value) {
  uint8_t speed = static_cast<uint8_t>(value);
  if (isValidTuyaFanSpeed(speed)) {
    zbFanControl.setFanSpeed(speed);  // Update Zigbee attribute
    lastConfirmedFanMode = speedToFanMode(speed);  // Track for rollback
  }
}
```

---

## Custom Clusters

Sometimes standard clusters don't cut it. The Skyfan controller needed fan direction control, which isn't in the standard Fan Control cluster.

### Creating a Manufacturer-Specific Cluster

1. **Pick your IDs** - cluster ID must be ≥ 0xFC00:

```cpp
#define MY_CUSTOM_CLUSTER_ID     0xFC00
#define MY_MANUFACTURER_CODE     0x1234  // Should be registered, mine is made up for the moment.
#define MY_CUSTOM_ATTR_ID        0x0001
```

2. **Create the cluster before `Zigbee.begin()`**:

```cpp
bool createCustomCluster() {
  customCluster = esp_zb_zcl_attr_list_create(MY_CUSTOM_CLUSTER_ID);

  uint8_t initialValue = 0;
  esp_zb_custom_cluster_add_custom_attr(
    customCluster,
    MY_CUSTOM_ATTR_ID,
    ESP_ZB_ZCL_ATTR_TYPE_U8,
    ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
    &initialValue
  );

  return esp_zb_cluster_list_add_custom_cluster(
    _cluster_list,
    customCluster,
    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE
  ) == ESP_OK;
}
```

3. **Register write handlers after `Zigbee.begin()`**:

```cpp
void custom_write_handler(uint8_t endpoint, uint16_t attr_id, uint8_t *value, uint16_t manuf_code) {
  if (attr_id == MY_CUSTOM_ATTR_ID && manuf_code == MY_MANUFACTURER_CODE) {
    // Do something with *value
  }
}

void setup() {
  myEndpoint.createCustomCluster();  // Before Zigbee.begin()
  Zigbee.addEndpoint(&myEndpoint);
  Zigbee.begin(ZIGBEE_ROUTER);

  // Register handlers after begin
  esp_zb_zcl_custom_cluster_handlers_t handlers = {
    .cluster_id = MY_CUSTOM_CLUSTER_ID,
    .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
    .check_value_cb = NULL,
    .write_attr_cb = custom_write_handler
  };
  esp_zb_zcl_custom_cluster_handlers_update(handlers);
}
```

**Worked example** - This fan direction cluster ([`SkyfanZigbee.cpp`](src/skyfan-zigbee/SkyfanZigbee.cpp) and [`skyfan-zigbee.ino:558-579`](src/skyfan-zigbee/skyfan-zigbee.ino)) follows this exact pattern for the `FanDirection` attribute.

---

## OTA Updates

The ESP32 Zigbee library supports over-the-air firmware updates. This is genuinely useful - nobody wants to climb a ladder to reflash a ceiling fan controller.

### Enabling OTA

```cpp
void setup() {
  // Add OTA client before Zigbee.begin()
  zbFan.addOTAClient(
    FIRMWARE_VERSION,      // Current version (uint32_t)
    FIRMWARE_VERSION,      // Downloaded version (same initially)
    HARDWARE_VERSION,      // Hardware revision
    MANUFACTURER_CODE,     // Your manufacturer ID
    IMAGE_TYPE             // Image type identifier
  );

  zbFan.onOTAStateChange([](bool active) {
    if (active) Serial.println("Updating - don't unplug!");
  });

  Zigbee.addEndpoint(&zbFan);
  Zigbee.begin(ZIGBEE_ROUTER);

  // After connected, start checking for updates
  zbFan.requestOTAUpdate();  // Queries immediately, then hourly
}
```

### Version Numbers

OTA uses a 32-bit version number. Higher = newer. The Skyfan firmware uses this encoding:

```
0xRRRRRDDD
├── Upper 20 bits: tagged releases (v1.0.0, v1.0.1, etc.)
└── Lower 12 bits: dev builds (1-4095)
```

This ensures releases always appear newer than dev builds.

### Creating OTA Images

Use Espressif's `image_builder_tool.py`:

```bash
python image_builder_tool.py \
  --create firmware.ota \
  --tag-file firmware.bin \
  --version 0x0008F000 \
  --manuf-id 0x1818 \
  --image-type 0x0001
```

---

## Zigbee2MQTT Integration

I use zigbee2mqtt for integrating with my zigbee network. If you are using the same and if your device isn't in the Zigbee2MQTT device database (and it won't be), you need an external converter.

How you configure your zigbee controller for solutions other than zigbee2mqtt is a little beyond the scope of this guide, refer to your zigbee gateway documentation. For the most part the device should work in some form without extra gateway configuration, but functions like custom clusters and ota might be degraded.

### External Converter

Create a `.mjs` file:

```javascript
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as fz from "zigbee-herdsman-converters/converters/fromZigbee";
import * as tz from "zigbee-herdsman-converters/converters/toZigbee";
import {presets} from "zigbee-herdsman-converters/lib/exposes";

const e = presets;

export default {
    zigbeeModel: ["My Device Model"],  // Must match your ZIGBEE_MODEL_NAME exactly
    model: "My Device Model",
    vendor: "My Company",
    description: "Does things",
    exposes: [
        e.fan().withModes(["off", "low", "medium", "high"]),
    ],
    fromZigbee: [fz.fan],
    toZigbee: [tz.fan_mode],
};
```

**Worked example** - This ([`zigbee2mqtt/skyfanConverter.mjs`](zigbee2mqtt/skyfanConverter.mjs)) adds a custom cluster definition for fan direction and configures two endpoints.

### Custom Clusters in Converters

If you have manufacturer-specific clusters, define them:

```javascript
import {Zcl} from "zigbee-herdsman";

m.deviceAddCustomCluster("myCluster", {
    ID: 0xFC00,
    manufacturerCode: 0x1234,
    attributes: {
        myAttribute: {ID: 0x0001, type: Zcl.DataType.UINT8, write: true, read: true}
    }
});
```

### Installation

1. Copy `.mjs` to your Zigbee2MQTT external converters directory. By the way, zigbee2mqtt requires that the external converters directory is writable. 
2. Restart Zigbee2MQTT. 
3. Profit.

### OTA via Zigbee2MQTT

Host an index file somewhere accessible:

```json
[{
  "url": "https://github.com/you/repo/releases/download/v1.0.0/firmware.ota",
  "manufacturerCode": 4660,
  "imageType": 1,
  "fileVersion": 65536,
  "modelId": "My Device Model"
}]
```

Add to Zigbee2MQTT configuration:
```yaml
ota:
  zigbee_ota_override_index_location: https://your-url/ota-index.json
```

---

## Tips

**Always check return values**. Zigbee operations can fail:
```cpp
if (!Zigbee.begin(ZIGBEE_ROUTER)) {
  ESP.restart();
}
```

**Implement factory reset**. Users will need it:
```cpp
if (buttonHeldFor3Seconds && !otaRunning) {
  Zigbee.factoryReset();
}
```

**Don't reset during OTA**. Bricked devices make people sad.

**Clean up custom cluster memory** before restart/reset if you allocated any.

---

## References

- [ESP32 Zigbee SDK Docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/zigbee/index.html)
- [ESP32 Arduino Zigbee Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/Zigbee/examples)
- [Zigbee2MQTT Docs](https://www.zigbee2mqtt.io/)
- [Zigbee Cluster Library Spec](https://csa-iot.org/developer-resource/specifications-download-request/) (requires registration, naturally)
