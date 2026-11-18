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
    CONF_ID,
    CONF_INTERNAL,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_MODE,
    CONF_NAME,
    CONF_STEP,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_UPDATE_INTERVAL,
    ENTITY_CATEGORY_CONFIG,
)

from .. import (
    CONF_BDR_BLE_ID,
    CONF_NODE_ID,
    CONF_PARAMETER,
    CONF_SENSORS,
    CONF_SUB_INDEX,
    BdrBleComponent,
    bdr_ble_ns,
)
from ..obd import (
    DATA_TYPE_MAP,
    NUMERIC_DATA_TYPES,
    infer_number_metadata,
    resolve_parameter,
)

DEPENDENCIES = ["bdr_ble"]

CONF_NUMBERS = "numbers"
CONF_SCALE = "scale"
CONF_RESOLVED_INDEX = "_obd_index"
CONF_RESOLVED_SUB = "_obd_sub"
CONF_RESOLVED_TYPE = "_obd_type"
CONF_RESOLVED_GAIN = "_obd_gain"

BdrBleNumber = bdr_ble_ns.class_(
    "BdrBleNumber", number.Number, cg.Parented.template(BdrBleComponent)
)


def _preprocess_number_item(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise cv.Invalid("Number configuration must be a mapping")
    if CONF_PARAMETER not in value:
        raise cv.Invalid(
            f"Missing required '{CONF_PARAMETER}' (e.g. 'AP059', 'CP000', or '0x3400:00')"
        )

    cfg = dict(value)
    sub_override = cfg.get(CONF_SUB_INDEX)
    if sub_override is not None:
        sub_override = cv.int_range(min=0, max=255)(sub_override)

    dp = resolve_parameter(cfg[CONF_PARAMETER], sub_override)
    dtype = dp["type"]
    if dtype not in NUMERIC_DATA_TYPES:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has non-numeric type '{dtype}'. "
            f"Only numeric parameters can be used with 'number:'."
        )

    meta = infer_number_metadata(dp)

    if CONF_NAME not in cfg and CONF_ID not in cfg and not cfg.get(CONF_INTERNAL, False):
        cfg[CONF_NAME] = meta["default_name"]

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
        cv.Optional(CONF_SCALE): cv.float_,
        cv.Required(CONF_MIN_VALUE): cv.float_,
        cv.Required(CONF_MAX_VALUE): cv.float_,
        cv.Required(CONF_STEP): cv.positive_float,
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_TYPE): cv.int_,
        cv.Required(CONF_RESOLVED_GAIN): cv.float_,
    }
)

_SINGLE_NUMBER_SCHEMA = cv.All(
    _preprocess_number_item,
    _NUMBER_ITEM_BASE.extend(
        {
            cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
            cv.Optional(
                CONF_UPDATE_INTERVAL, default="30s"
            ): cv.positive_time_period_milliseconds,
        }
    ),
)

_GROUP_NUMBER_ITEM_SCHEMA = cv.All(
    _preprocess_number_item,
    _NUMBER_ITEM_BASE.extend(
        {
            cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
        }
    ),
)

_GROUP_NUMBER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(
            CONF_UPDATE_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_NUMBERS): cv.ensure_list(_GROUP_NUMBER_ITEM_SCHEMA),
        cv.Optional(CONF_SENSORS): cv.ensure_list(_GROUP_NUMBER_ITEM_SCHEMA),
    }
)


def _validate_platform_config(config: Any) -> dict[str, Any]:
    if isinstance(config, dict) and (
        CONF_NUMBERS in config or CONF_SENSORS in config
    ):
        return _GROUP_NUMBER_SCHEMA(config)
    return _SINGLE_NUMBER_SCHEMA(config)


CONFIG_SCHEMA = cv.All(_validate_platform_config)


async def _register_one_number(
    hub: Any,
    item_cfg: dict[str, Any],
    default_interval_ms: int,
    default_node_id: int | None,
) -> None:
    num = await number.new_number(
        item_cfg,
        min_value=item_cfg[CONF_MIN_VALUE],
        max_value=item_cfg[CONF_MAX_VALUE],
        step=item_cfg[CONF_STEP],
    )
    await cg.register_parented(num, hub)
    interval = item_cfg.get(CONF_UPDATE_INTERVAL)
    interval_ms = (
        interval.total_milliseconds if interval is not None else default_interval_ms
    )
    node_id = item_cfg.get(CONF_NODE_ID, default_node_id)
    node_id_val = 0xFF if node_id is None else node_id

    cg.add(
        num.set_datapoint_meta(
            node_id_val,
            item_cfg[CONF_RESOLVED_INDEX],
            item_cfg[CONF_RESOLVED_SUB],
            item_cfg[CONF_RESOLVED_TYPE],
            item_cfg[CONF_RESOLVED_GAIN],
        )
    )
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


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    items = config.get(CONF_NUMBERS) or config.get(CONF_SENSORS)
    if items is not None:
        default_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
        default_node_id = config.get(CONF_NODE_ID)
        for item_cfg in items:
            await _register_one_number(
                hub, item_cfg, default_interval_ms, default_node_id
            )
    else:
        await _register_one_number(
            hub,
            config,
            config[CONF_UPDATE_INTERVAL].total_milliseconds,
            config.get(CONF_NODE_ID),
        )
