import {access, presets} from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as reporting from "zigbee-herdsman-converters/lib/reporting";
import {Zcl} from "zigbee-herdsman";

const e = presets;

// Front Left Speaker manufacturer-specific constants
const FLS_MANUFACTURER_CODE = 0x1818;
const CUSTOM_CLUSTER_ID = 0xFC00;
const FAN_DIRECTION_ATTR_ID = 0x0001;

const manufacturerOptions = {manufacturerCode: FLS_MANUFACTURER_CODE};

// Custom fromZigbee converters for fan clusters
const fzLocal = {
    fan_mode: {
        cluster: "hvacFanCtrl",
        type: ["attributeReport", "readResponse"],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.fanMode !== undefined) {
                const fanModeMap = {0: "off", 1: "low", 2: "medium", 3: "high", 4: "on", 5: "auto", 6: "smart"};
                const mode = fanModeMap[msg.data.fanMode] ?? "off";
                return {fan_mode: mode, fan_state: mode === "off" ? "OFF" : "ON"};
            }
        },
    },
    fan_direction: {
        cluster: "fanExtensions",
        type: ["attributeReport", "readResponse"],
        convert: (model, msg, publish, options, meta) => {
            if (Object.hasOwn(msg.data, "fanDirection")) {
                const direction = msg.data.fanDirection === 0 ? "forward" : "reverse";
                return {fan_direction: direction};
            }
        },
    },
};

const tzLocal = {
    fan_mode: {
        key: ["fan_mode"],
        convertSet: async (entity, key, value, meta) => {
            const modeMap = {off: 0, low: 1, medium: 2, high: 3, on: 4, auto: 5, smart: 6};
            await entity.write("hvacFanCtrl", {fanMode: modeMap[value] ?? 0});
            return {state: {fan_mode: value, fan_state: value === "off" ? "OFF" : "ON"}};
        },
        convertGet: async (entity, key, meta) => {
            await entity.read("hvacFanCtrl", ["fanMode"]);
        },
    },
    fan_direction: {
        key: ["fan_direction"],
        convertSet: async (entity, key, value, meta) => {
            const directionValue = value === "forward" ? 0 : 1;
            await entity.write("fanExtensions", {fanDirection: directionValue}, manufacturerOptions);
            return {state: {[key]: value}};
        },
        convertGet: async (entity, key, meta) => {
            await entity.read("fanExtensions", ["fanDirection"], manufacturerOptions);
        },
    },
};

// Shared custom cluster extension for fan direction
const fanExtensionsCluster = m.deviceAddCustomCluster("fanExtensions", {
    ID: CUSTOM_CLUSTER_ID,
    manufacturerCode: FLS_MANUFACTURER_CODE,
    attributes: {
        fanDirection: {
            ID: FAN_DIRECTION_ATTR_ID,
            type: Zcl.DataType.ENUM8,
        },
    },
    commands: {},
    commandsResponse: {},
});

const fanExposes = [
    e.fan().withModes(["off", "low", "medium", "high", "on"]),
    e.enum("fan_direction", access.ALL, ["forward", "reverse"]).withDescription("Fan rotation direction"),
];

const fanFromZigbee = [fzLocal.fan_mode, fzLocal.fan_direction];
const fanToZigbee = [tzLocal.fan_mode, tzLocal.fan_direction];

async function configureFan(device, coordinatorEndpoint) {
    const fanEndpoint = device.getEndpoint(1);

    await reporting.bind(fanEndpoint, coordinatorEndpoint, ["hvacFanCtrl", "fanExtensions"]);

    try {
        await reporting.fanMode(fanEndpoint);
    } catch {
        // Fan mode reporting not supported, using binding only
    }

    try {
        await fanEndpoint.configureReporting("fanExtensions", [
            {
                attribute: "fanDirection",
                minimumReportInterval: 1,
                maximumReportInterval: 3600,
                reportableChange: 1,
            },
        ]);
    } catch {
        // Fan direction reporting not supported, using binding only
    }
}

// OTA updates from GitHub releases
// User must add the following to their Zigbee2MQTT configuration.yaml:
// ota:
//   zigbee_ota_override_index_location: https://raw.githubusercontent.com/rhysfred/skyfan-zigbee/main/zigbee2mqtt/ota-index.json

export default [
    {
        zigbeeModel: ["Ventair Skyfan/Light ZB Adaptor"],
        model: "Ventair Skyfan/Light ZB Adaptor",
        vendor: "Front Left Speaker",
        description: "Ventair Skyfan ceiling fan with integrated lighting controller",
        extend: [
            fanExtensionsCluster,
            m.deviceEndpoints({endpoints: {fan: 1, light: 2}}),
            m.light({colorTemp: {range: [154, 333]}, endpointNames: ["light"]}),
        ],
        exposes: fanExposes,
        fromZigbee: fanFromZigbee,
        toZigbee: fanToZigbee,
        configure: async (device, coordinatorEndpoint) => {
            await configureFan(device, coordinatorEndpoint);

            const lightEndpoint = device.getEndpoint(2);
            await reporting.bind(lightEndpoint, coordinatorEndpoint, ["genOnOff", "genLevelCtrl", "lightingColorCtrl"]);

            try {
                await lightEndpoint.configureReporting("genOnOff", [
                    {attribute: "onOff", minimumReportInterval: 0, maximumReportInterval: 3600, reportableChange: 1},
                ]);
            } catch {
                // genOnOff reporting not supported, using binding only
            }

            try {
                await lightEndpoint.configureReporting("genLevelCtrl", [
                    {attribute: "currentLevel", minimumReportInterval: 5, maximumReportInterval: 3600, reportableChange: 1},
                ]);
            } catch {
                // genLevelCtrl reporting not supported, using binding only
            }

            try {
                await lightEndpoint.configureReporting("lightingColorCtrl", [
                    {attribute: "colorTemperature", minimumReportInterval: 10, maximumReportInterval: 3600, reportableChange: 1},
                ]);
            } catch {
                // lightingColorCtrl reporting not supported, using binding only
            }
        },
        ota: true,
    },
    {
        zigbeeModel: ["Ventair Skyfan ZB Adaptor"],
        model: "Ventair Skyfan ZB Adaptor",
        vendor: "Front Left Speaker",
        description: "Ventair Skyfan ceiling fan controller (fan only)",
        extend: [
            fanExtensionsCluster,
            m.deviceEndpoints({endpoints: {fan: 1}}),
        ],
        exposes: fanExposes,
        fromZigbee: fanFromZigbee,
        toZigbee: fanToZigbee,
        configure: async (device, coordinatorEndpoint) => {
            await configureFan(device, coordinatorEndpoint);
        },
        ota: true,
    },
];
