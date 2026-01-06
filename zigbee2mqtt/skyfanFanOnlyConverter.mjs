import {access, presets} from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as reporting from "zigbee-herdsman-converters/lib/reporting";
import * as fz from "zigbee-herdsman-converters/converters/fromZigbee";
import * as tz from "zigbee-herdsman-converters/converters/toZigbee";
import * as ota from "zigbee-herdsman-converters/lib/ota";
import {Zcl} from "zigbee-herdsman";

const e = presets;

// Made-up manufacturer options for Front Left Speaker
const manufacturerOptions = {manufacturerCode: 0x1818};

// Manufacturer-specific cluster for fan extensions
m.deviceAddCustomCluster("fanExtensions", {
    ID: 0xFC00,
    manufacturerCode: 0x1818,
    attributes: {
        fanDirection: {
            ID: 0x0001,
            type: Zcl.DataType.UINT8,
            write: true,
            read: true
        }
    }
});

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
    zigbeeModel: ["Ventair Skyfan ZB Adaptor"],
    model: "Ventair Skyfan ZB Adaptor",
    vendor: "Front Left Speaker",
    description: "Ventair Skyfan ceiling fan controller (fan only)",
    extend: [
        m.deviceEndpoints({
            endpoints: {
                fan: 1,
            },
        }),
    ],
    exposes: [
        e.fan().withState().withModes(["off", "low", "medium", "high"]).withEndpoint("fan"),
        e.enum("fan_direction", access.ALL, ["forward", "reverse"]).withDescription("Fan rotation direction").withEndpoint("fan"),
    ],

    // Include standard fan converters + custom direction converters
    fromZigbee: [fz.fan, fzLocal.fan_direction],
    toZigbee: [tz.fan_mode, tzLocal.fan_direction],

    configure: async (device, coordinatorEndpoint, logger) => {
        const fanEndpoint = device.getEndpoint(1);

        // Bind fan clusters only
        await reporting.bind(fanEndpoint, coordinatorEndpoint, ["hvacFanCtrl", "fanExtensions"]);
        await reporting.fanMode(fanEndpoint);

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
        } catch (error) {
            logger.warn("Failed to configure custom fan direction reporting", error);
        }
    },

    // OTA updates from GitHub releases
    // User must add the following to their Zigbee2MQTT configuration.yaml:
    // ota:
    //   zigbee_ota_override_index_location: https://raw.githubusercontent.com/rhysfred/skyfan-zigbee/main/zigbee2mqtt/ota-index.json
    ota: ota.zigbeeOTA,
};