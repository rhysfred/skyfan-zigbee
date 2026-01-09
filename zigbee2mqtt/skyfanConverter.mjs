import {access, presets} from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as reporting from "zigbee-herdsman-converters/lib/reporting";
import * as fz from "zigbee-herdsman-converters/converters/fromZigbee";
import * as tz from "zigbee-herdsman-converters/converters/toZigbee";
import * as ota from "zigbee-herdsman-converters/lib/ota";
import {Zcl} from "zigbee-herdsman";

const e = presets;

// Front Left Speaker manufacturer-specific constants
const FLS_MANUFACTURER_CODE = 0x1818;
const CUSTOM_CLUSTER_ID = 0xFC00;
const FAN_DIRECTION_ATTR_ID = 0x0001;

const manufacturerOptions = {manufacturerCode: FLS_MANUFACTURER_CODE};

// Custom converters for fan direction manufacturer-specific cluster attribute
const fzLocal = {
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

export default {
    zigbeeModel: ["Ventair Skyfan/Light ZB Adaptor"],
    model: "Ventair Skyfan/Light ZB Adaptor",
    vendor: "Front Left Speaker",
    description: "Ventair Skyfan ceiling fan with integrated lighting controller",
    extend: [
        m.deviceAddCustomCluster("fanExtensions", {
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
        }),
        m.deviceEndpoints({
            endpoints: {
                fan: 1,
                light: 2,
            },
        }),
        m.light({
            colorTemp: {range: [154, 333]},
            endpointNames: ["light"],
        }),
    ],
    exposes: [
        e.fan().withModes(["off", "low", "medium", "high"]).withEndpoint("fan"),
        e.enum("fan_direction", access.ALL, ["forward", "reverse"]).withDescription("Fan rotation direction").withEndpoint("fan"),
    ],

    // Include standard fan converters + custom direction converters
    fromZigbee: [fz.fan, fzLocal.fan_direction],
    toZigbee: [tz.fan_mode, tzLocal.fan_direction],

    configure: async (device, coordinatorEndpoint) => {
        const fanEndpoint = device.getEndpoint(1);
        const lightEndpoint = device.getEndpoint(2);

        await reporting.bind(fanEndpoint, coordinatorEndpoint, ["hvacFanCtrl", "fanExtensions"]);
        await reporting.bind(lightEndpoint, coordinatorEndpoint, ["genOnOff", "genLevelCtrl", "lightingColorCtrl"]);

        // Configure fan mode reporting (requires firmware with reporting enabled)
        try {
            await reporting.fanMode(fanEndpoint);
        } catch {
            // Fan mode reporting not supported, using binding only
        }

        // Configure custom cluster attribute reporting for direction
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
    },

    // OTA updates from GitHub releases
    // User must add the following to their Zigbee2MQTT configuration.yaml:
    // ota:
    //   zigbee_ota_override_index_location: https://raw.githubusercontent.com/rhysfred/skyfan-zigbee/main/zigbee2mqtt/ota-index.json
    ota: ota.zigbeeOTA,
};