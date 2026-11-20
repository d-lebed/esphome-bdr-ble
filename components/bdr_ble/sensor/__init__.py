"""Sensor platform for BDR Thermea (De Dietrich / Baxi / Remeha) BLE boilers."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
    CONF_DEVICE_CLASS,
    CONF_ICON,
    CONF_STATE_CLASS,
    CONF_TYPE,
    CONF_UNIT_OF_MEASUREMENT,
)

from .. import (
    CONF_BDR_BLE_ID,
    CONF_NODE_ID,
    CONF_PARAMETER,
    CONF_SENSORS,
    CONF_SUB_INDEX,
)
from ..obd import DATA_TYPE_MAP, NUMERIC_DATA_TYPES, infer_sensor_metadata
from ..schema_helpers import (
    CONF_RESOLVED_GAIN,
    CONF_RESOLVED_INDEX,
    CONF_RESOLVED_SUB,
    CONF_RESOLVED_TYPE,
    CONF_SCALE,
    apply_default_name,
    iter_platform_items,
    make_platform_schema,
    resolve_item_datapoint,
)

DEPENDENCIES = ["bdr_ble"]


def _preprocess_sensor_item(value: Any) -> dict[str, Any]:
    cfg, dp = resolve_item_datapoint(
        value, "Sensor", "e.g. 'AC005', 'AM027', or '0x5044:00'"
    )
    dtype = dp["type"]
    if dtype not in NUMERIC_DATA_TYPES:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has non-numeric type '{dtype}'. "
            f"Use 'text_sensor:' instead of 'sensor:'."
        )

    meta = infer_sensor_metadata(dp)
    apply_default_name(cfg, meta["default_name"])

    for key in (
        CONF_UNIT_OF_MEASUREMENT,
        CONF_ACCURACY_DECIMALS,
        CONF_DEVICE_CLASS,
        CONF_STATE_CLASS,
        CONF_ICON,
    ):
        if key not in cfg and key in meta:
            cfg[key] = meta[key]

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_TYPE] = DATA_TYPE_MAP[dtype]
    cfg[CONF_RESOLVED_GAIN] = float(cfg.get(CONF_SCALE, dp.get("gain", 1.0)))
    return cfg


_SENSOR_ITEM_BASE = sensor.sensor_schema().extend(
    {
        cv.Required(CONF_PARAMETER): cv.string,
        cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(CONF_SCALE): cv.positive_float,
        cv.Optional(CONF_TYPE): cv.string,
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_TYPE): cv.int_,
        cv.Required(CONF_RESOLVED_GAIN): cv.float_,
    }
)

CONFIG_SCHEMA = make_platform_schema(
    _preprocess_sensor_item,
    _SENSOR_ITEM_BASE,
    CONF_SENSORS,
    default_interval="30s",
    allow_sensors_alias=False,
)


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    for item_cfg, interval_ms, node_id_val in iter_platform_items(config, CONF_SENSORS):
        sens = await sensor.new_sensor(item_cfg)
        cg.add(
            hub.register_sensor(
                sens,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                item_cfg[CONF_RESOLVED_TYPE],
                item_cfg[CONF_RESOLVED_GAIN],
                interval_ms,
                node_id_val,
            )
        )
