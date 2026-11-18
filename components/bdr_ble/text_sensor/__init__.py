"""Text sensor platform for BDR Thermea (De Dietrich / Baxi / Remeha) BLE boilers."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INTERNAL,
    CONF_NAME,
    CONF_STATUS,
    CONF_UPDATE_INTERVAL,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from .. import (
    CONF_BDR_BLE_ID,
    CONF_NODE_ID,
    CONF_PARAMETER,
    CONF_SENSORS,
    CONF_SUB_INDEX,
    BdrBleComponent,
)
from ..obd import (
    DATA_TYPE_MAP,
    TEXT_DATA_TYPES,
    get_enum_mapping,
    infer_sensor_metadata,
    resolve_parameter,
)

DEPENDENCIES = ["bdr_ble"]

CONF_RESOLVED_INDEX = "_obd_index"
CONF_RESOLVED_SUB = "_obd_sub"
CONF_RESOLVED_TYPE = "_obd_type"
CONF_RESOLVED_ENUM = "_obd_enum"


def _preprocess_text_sensor_item(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise cv.Invalid("Text sensor configuration must be a mapping")
    if CONF_PARAMETER not in value:
        raise cv.Invalid(f"Missing required '{CONF_PARAMETER}'")

    cfg = dict(value)
    sub_override = cfg.get(CONF_SUB_INDEX)
    if sub_override is not None:
        sub_override = cv.int_range(min=0, max=255)(sub_override)

    dp = resolve_parameter(cfg[CONF_PARAMETER], sub_override)
    dtype = dp["type"]
    if dtype == "Struct":
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) is a composite Struct table and cannot be polled as a single entity."
        )
    if dtype not in TEXT_DATA_TYPES:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has numeric type '{dtype}'. "
            f"Use 'sensor:' instead of 'text_sensor:'."
        )

    meta = infer_sensor_metadata(dp)
    if CONF_NAME not in cfg and CONF_ID not in cfg and not cfg.get(CONF_INTERNAL, False):
        cfg[CONF_NAME] = meta["default_name"]

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_TYPE] = DATA_TYPE_MAP[dtype]
    cfg[CONF_RESOLVED_ENUM] = dp.get("enum", "")
    return cfg


_OBD_TEXT_ITEM_SCHEMA = cv.All(
    _preprocess_text_sensor_item,
    text_sensor.text_sensor_schema().extend(
        {
            cv.Required(CONF_PARAMETER): cv.string,
            cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
            cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
            cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
            cv.Required(CONF_RESOLVED_INDEX): cv.int_,
            cv.Required(CONF_RESOLVED_SUB): cv.int_,
            cv.Required(CONF_RESOLVED_TYPE): cv.int_,
            cv.Required(CONF_RESOLVED_ENUM): cv.string,
        }
    ),
)

_SINGLE_OBD_TEXT_SCHEMA = cv.All(
    _preprocess_text_sensor_item,
    text_sensor.text_sensor_schema().extend(
        {
            cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
            cv.Required(CONF_PARAMETER): cv.string,
            cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
            cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
            cv.Optional(
                CONF_UPDATE_INTERVAL, default="30s"
            ): cv.positive_time_period_milliseconds,
            cv.Required(CONF_RESOLVED_INDEX): cv.int_,
            cv.Required(CONF_RESOLVED_SUB): cv.int_,
            cv.Required(CONF_RESOLVED_TYPE): cv.int_,
            cv.Required(CONF_RESOLVED_ENUM): cv.string,
        }
    ),
)

_HUB_TEXT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(
            CONF_UPDATE_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
            icon="mdi:bluetooth-connect",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_SENSORS): cv.ensure_list(_OBD_TEXT_ITEM_SCHEMA),
    }
)


def _validate_text_platform(config: Any) -> dict[str, Any]:
    if isinstance(config, dict) and CONF_PARAMETER in config:
        return _SINGLE_OBD_TEXT_SCHEMA(config)
    return _HUB_TEXT_SCHEMA(config)


CONFIG_SCHEMA = cv.All(_validate_text_platform)


async def _register_one_text_sensor(
    hub: Any,
    item_cfg: dict[str, Any],
    default_interval_ms: int,
    default_node_id: int | None,
) -> None:
    sens = await text_sensor.new_text_sensor(item_cfg)
    interval = item_cfg.get(CONF_UPDATE_INTERVAL)
    interval_ms = (
        interval.total_milliseconds if interval is not None else default_interval_ms
    )
    node_id = item_cfg.get(CONF_NODE_ID, default_node_id)
    node_id_val = 0xFF if node_id is None else node_id

    cg.add(
        hub.register_text_sensor(
            sens,
            item_cfg[CONF_RESOLVED_INDEX],
            item_cfg[CONF_RESOLVED_SUB],
            item_cfg[CONF_RESOLVED_TYPE],
            interval_ms,
            node_id_val,
        )
    )
    enum_name = item_cfg.get(CONF_RESOLVED_ENUM, "")
    if enum_name:
        for enum_val, enum_desc in get_enum_mapping(enum_name).items():
            cg.add(
                hub.register_enum_translation(
                    item_cfg[CONF_RESOLVED_INDEX],
                    item_cfg[CONF_RESOLVED_SUB],
                    enum_val,
                    enum_desc,
                )
            )


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])

    if CONF_PARAMETER in config:
        await _register_one_text_sensor(
            hub,
            config,
            config[CONF_UPDATE_INTERVAL].total_milliseconds,
            config.get(CONF_NODE_ID),
        )
        return

    if CONF_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_STATUS])
        cg.add(hub.set_status_text_sensor(sens))

    if CONF_SENSORS in config:
        default_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
        default_node_id = config.get(CONF_NODE_ID)
        for item_cfg in config[CONF_SENSORS]:
            await _register_one_text_sensor(
                hub, item_cfg, default_interval_ms, default_node_id
            )
