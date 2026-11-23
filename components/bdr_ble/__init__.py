"""ESPHome component for BDR Thermea (De Dietrich / Baxi / Remeha) BLE boilers and heat pumps."""

import inspect
import logging

from esphome import automation
import esphome.codegen as cg
from esphome.components import ble_client
import esphome.config_validation as cv
from esphome.const import CONF_ID

_LOGGER = logging.getLogger(__name__)

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
DiscoverAction = bdr_ble_ns.class_("DiscoverAction", automation.Action)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BdrBleComponent),
            cv.Optional(CONF_NODE_ID, default=0x01): cv.int_range(min=0, max=255),
            cv.Optional(CONF_AUTH_LEVEL, default=4): cv.int_range(min=0, max=4),
            cv.Optional(CONF_PIN): cv.int_range(min=0, max=999999),
            cv.Optional(CONF_LOG_SNAPSHOT): cv.boolean,
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


_REGISTER_ACTION_KWARGS = (
    {"synchronous": True}
    if "synchronous" in inspect.signature(automation.register_action).parameters
    else {}
)


@automation.register_action(
    "bdr_ble.discover",
    DiscoverAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(CONF_ID): cv.use_id(BdrBleComponent),
        }
    ),
    **_REGISTER_ACTION_KWARGS,
)
async def discover_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_default_node_id(config[CONF_NODE_ID]))
    cg.add(var.set_auth_level(config[CONF_AUTH_LEVEL]))
    if CONF_LOG_SNAPSHOT in config:
        _LOGGER.warning(
            "'log_snapshot' is deprecated and no longer dumps BDR_SNAPSHOT on reconnect; "
            "use 'button: platform: bdr_ble' or the 'bdr_ble.discover' action instead."
        )
    if CONF_PIN in config:
        cg.add(var.set_pin(config[CONF_PIN]))
