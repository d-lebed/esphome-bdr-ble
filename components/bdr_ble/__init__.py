"""ESPHome component for BDR Thermea (De Dietrich / Baxi / Remeha) BLE boilers and heat pumps."""

import esphome.codegen as cg
from esphome.components import ble_client
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@d-lebed"]
DEPENDENCIES = ["ble_client"]
MULTI_CONF = True

CONF_BDR_BLE_ID = "bdr_ble_id"
CONF_NODE_ID = "node_id"
CONF_AUTH_LEVEL = "auth_level"
CONF_PIN = "pin"
CONF_LOG_SNAPSHOT = "log_snapshot"
CONF_PARAMETER = "parameter"
CONF_SUB_INDEX = "sub_index"
CONF_SENSORS = "sensors"

bdr_ble_ns = cg.esphome_ns.namespace("bdr_ble")
BdrBleComponent = bdr_ble_ns.class_(
    "BdrBleComponent", cg.Component, ble_client.BLEClientNode
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BdrBleComponent),
            cv.Optional(CONF_NODE_ID, default=0x01): cv.int_range(min=0, max=255),
            cv.Optional(CONF_AUTH_LEVEL, default=4): cv.int_range(min=0, max=4),
            cv.Optional(CONF_PIN): cv.int_range(min=0, max=999999),
            cv.Optional(CONF_LOG_SNAPSHOT, default=True): cv.boolean,
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_default_node_id(config[CONF_NODE_ID]))
    cg.add(var.set_auth_level(config[CONF_AUTH_LEVEL]))
    cg.add(var.set_log_snapshot(config[CONF_LOG_SNAPSHOT]))
    if CONF_PIN in config:
        cg.add(var.set_pin(config[CONF_PIN]))
