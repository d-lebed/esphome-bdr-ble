"""Button platform for BDR Thermea (De Dietrich / Baxi / Remeha) BLE discovery actions."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from .. import CONF_BDR_BLE_ID, BdrBleComponent, bdr_ble_ns

DEPENDENCIES = ["bdr_ble"]

CONF_DISCOVER = "discover"

BdrBleDiscoverButton = bdr_ble_ns.class_(
    "BdrBleDiscoverButton",
    button.Button,
    cg.Parented.template(BdrBleComponent),
)

_DISCOVER_BUTTON_SCHEMA = button.button_schema(
    BdrBleDiscoverButton,
    icon="mdi:radar",
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)

_SINGLE_BUTTON_SCHEMA = _DISCOVER_BUTTON_SCHEMA.extend(
    {
        cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
    }
)

_GROUPED_BUTTON_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
        cv.Optional(CONF_DISCOVER): _DISCOVER_BUTTON_SCHEMA,
    }
)


def _validate_button_platform(config: Any) -> dict[str, Any]:
    if isinstance(config, dict) and CONF_DISCOVER in config:
        return _GROUPED_BUTTON_SCHEMA(config)
    return _SINGLE_BUTTON_SCHEMA(config)


CONFIG_SCHEMA = cv.All(_validate_button_platform)


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    btn_cfg = config[CONF_DISCOVER] if CONF_DISCOVER in config else config
    var = await button.new_button(btn_cfg)
    await cg.register_parented(var, hub)
