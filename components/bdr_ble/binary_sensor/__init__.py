"""Binary sensor platform for BDR Thermea (De Dietrich / Baxi / Remeha) BLE boilers and heat pumps."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_DEVICE_CLASS, CONF_ICON, CONF_TYPE

from .. import (
    CONF_BDR_BLE_ID,
    CONF_NODE_ID,
    CONF_PARAMETER,
    CONF_SUB_INDEX,
)
from ..obd import DATA_TYPE_MAP, NUMERIC_DATA_TYPES, infer_sensor_metadata
from ..schema_helpers import (
    CONF_RESOLVED_INDEX,
    CONF_RESOLVED_SUB,
    CONF_RESOLVED_TYPE,
    apply_default_name,
    iter_platform_items,
    make_platform_schema,
    resolve_item_datapoint,
)

DEPENDENCIES = ["bdr_ble"]

CONF_BINARY_SENSORS = "binary_sensors"


def _preprocess_binary_sensor_item(value: Any) -> dict[str, Any]:
    cfg, dp = resolve_item_datapoint(
        value, "Binary sensor", "e.g. 'HM012', 'HM013', 'AM015', or '0x430C:00'"
    )
    dtype = dp["type"]
    if dtype not in NUMERIC_DATA_TYPES:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has non-boolean/non-numeric type '{dtype}'."
        )

    meta = infer_sensor_metadata(dp)
    apply_default_name(cfg, meta["default_name"])

    if CONF_ICON not in cfg and CONF_DEVICE_CLASS not in cfg and CONF_ICON in meta:
        cfg[CONF_ICON] = meta[CONF_ICON]

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_TYPE] = DATA_TYPE_MAP[dtype]
    return cfg


_BINARY_SENSOR_ITEM_BASE = binary_sensor.binary_sensor_schema().extend(
    {
        cv.Required(CONF_PARAMETER): cv.string,
        cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(CONF_TYPE): cv.string,
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_TYPE): cv.int_,
    }
)

CONFIG_SCHEMA = make_platform_schema(
    _preprocess_binary_sensor_item,
    _BINARY_SENSOR_ITEM_BASE,
    CONF_BINARY_SENSORS,
    default_interval="10s",
    allow_sensors_alias=True,
)


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    for item_cfg, interval_ms, node_id_val in iter_platform_items(
        config, CONF_BINARY_SENSORS
    ):
        bsens = await binary_sensor.new_binary_sensor(item_cfg)
        cg.add(
            hub.register_binary_sensor(
                bsens,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                item_cfg[CONF_RESOLVED_TYPE],
                interval_ms,
                node_id_val,
            )
        )
