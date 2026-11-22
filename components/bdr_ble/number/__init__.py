"""Number platform for writable BDR Thermea (De Dietrich / Baxi / Remeha) BLE parameters."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_MODE,
    CONF_STEP,
    CONF_TYPE,
    CONF_UNIT_OF_MEASUREMENT,
    ENTITY_CATEGORY_CONFIG,
)

from .. import (
    CONF_BDR_BLE_ID,
    CONF_NODE_ID,
    CONF_PARAMETER,
    CONF_SUB_INDEX,
    BdrBleComponent,
    bdr_ble_ns,
)
from ..obd import DATA_TYPE_MAP, NUMERIC_DATA_TYPES, infer_number_metadata
from ..schema_helpers import (
    CONF_DEVICE_LIMITS,
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

CONF_NUMBERS = "numbers"

BdrBleNumber = bdr_ble_ns.class_(
    "BdrBleNumber",
    number.Number,
    cg.Component,
    cg.Parented.template(BdrBleComponent),
)


def _preprocess_number_item(value: Any) -> dict[str, Any]:
    cfg, dp = resolve_item_datapoint(
        value, "Number", "e.g. 'AP059', 'CP000', or '0x3400:00'"
    )
    dtype = dp["type"]
    if dtype not in NUMERIC_DATA_TYPES:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has non-numeric type '{dtype}'. "
            f"Only numeric parameters can be used with 'number:'."
        )
    if not dp.get("rw", False):
        raise cv.Invalid(
            f"OBD datapoint '{cfg[CONF_PARAMETER]}' ({dp['name']}) is read-only ({dp.get('cls', 'Signal')}). "
            f"Use 'sensor:' instead of 'number:'."
        )

    meta = infer_number_metadata(dp)
    apply_default_name(cfg, meta["default_name"])

    for key in (
        CONF_UNIT_OF_MEASUREMENT,
        CONF_DEVICE_CLASS,
        CONF_ICON,
        CONF_MIN_VALUE,
        CONF_MAX_VALUE,
        CONF_STEP,
    ):
        if key not in cfg and key in meta:
            cfg[key] = meta[key]

    if CONF_ENTITY_CATEGORY not in cfg:
        cfg[CONF_ENTITY_CATEGORY] = ENTITY_CATEGORY_CONFIG

    if CONF_MODE not in cfg:
        cfg[CONF_MODE] = "BOX"

    if cfg[CONF_MIN_VALUE] >= cfg[CONF_MAX_VALUE]:
        raise cv.Invalid(
            f"min_value ({cfg[CONF_MIN_VALUE]}) must be less than max_value ({cfg[CONF_MAX_VALUE]}) "
            f"for parameter '{cfg[CONF_PARAMETER]}'"
        )

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_TYPE] = DATA_TYPE_MAP[dtype]
    cfg[CONF_RESOLVED_GAIN] = float(cfg.get(CONF_SCALE, dp.get("gain", 1.0)))
    return cfg


_NUMBER_ITEM_BASE = number.number_schema(
    BdrBleNumber,
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend(
    {
        cv.Required(CONF_PARAMETER): cv.string,
        cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(CONF_SCALE): cv.positive_float,
        cv.Optional(CONF_TYPE): cv.string,
        cv.Optional(CONF_DEVICE_LIMITS, default=True): cv.boolean,
        cv.Required(CONF_MIN_VALUE): cv.float_,
        cv.Required(CONF_MAX_VALUE): cv.float_,
        cv.Required(CONF_STEP): cv.positive_float,
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_TYPE): cv.int_,
        cv.Required(CONF_RESOLVED_GAIN): cv.float_,
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = make_platform_schema(
    _preprocess_number_item,
    _NUMBER_ITEM_BASE,
    CONF_NUMBERS,
    default_interval="30s",
    allow_sensors_alias=True,
)


async def to_code(config: dict[str, Any]) -> None:
    cg.add_define("USE_BDR_BLE_NUMBER")
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    for item_cfg, interval_ms, node_id_val in iter_platform_items(config, CONF_NUMBERS):
        num = await number.new_number(
            item_cfg,
            min_value=item_cfg[CONF_MIN_VALUE],
            max_value=item_cfg[CONF_MAX_VALUE],
            step=item_cfg[CONF_STEP],
        )
        await cg.register_component(num, item_cfg)
        await cg.register_parented(num, hub)
        cg.add(
            num.set_datapoint_meta(
                node_id_val,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                item_cfg[CONF_RESOLVED_TYPE],
                item_cfg[CONF_RESOLVED_GAIN],
            )
        )
        cg.add(num.set_use_device_limits(item_cfg[CONF_DEVICE_LIMITS]))
        cg.add(
            hub.register_number(
                num,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                item_cfg[CONF_RESOLVED_TYPE],
                item_cfg[CONF_RESOLVED_GAIN],
                interval_ms,
                node_id_val,
            )
        )
