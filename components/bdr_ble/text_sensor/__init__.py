"""Text sensor platform for BDR Thermea (De Dietrich / Baxi / Remeha) BLE boilers."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_STATUS,
    CONF_TYPE,
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
from ..obd import DATA_TYPE_MAP, TEXT_DATA_TYPES, infer_sensor_metadata
from ..schema_helpers import (
    CONF_RESOLVED_ENUM,
    CONF_RESOLVED_INDEX,
    CONF_RESOLVED_SUB,
    CONF_RESOLVED_TYPE,
    apply_default_name,
    emit_flash_enum_table,
    resolve_item_datapoint,
)

DEPENDENCIES = ["bdr_ble"]


def _preprocess_text_sensor_item(value: Any) -> dict[str, Any]:
    cfg, dp = resolve_item_datapoint(value, "Text sensor", "e.g. 'AM012' or '0x341F:01'")
    dtype = dp["type"]
    if dtype not in TEXT_DATA_TYPES:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has numeric type '{dtype}'. "
            f"Use 'sensor:' instead of 'text_sensor:'."
        )

    meta = infer_sensor_metadata(dp)
    apply_default_name(cfg, meta["default_name"])

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_TYPE] = DATA_TYPE_MAP[dtype]
    cfg[CONF_RESOLVED_ENUM] = dp.get("enum", "")
    return cfg


_TEXT_SENSOR_ITEM_BASE = text_sensor.text_sensor_schema().extend(
    {
        cv.Required(CONF_PARAMETER): cv.string,
        cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(CONF_TYPE): cv.string,
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_TYPE): cv.int_,
        cv.Required(CONF_RESOLVED_ENUM): cv.string,
    }
)

_OBD_TEXT_ITEM_SCHEMA = cv.All(
    _preprocess_text_sensor_item,
    _TEXT_SENSOR_ITEM_BASE.extend(
        {
            cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
        }
    ),
)

_SINGLE_OBD_TEXT_SCHEMA = cv.All(
    _preprocess_text_sensor_item,
    _TEXT_SENSOR_ITEM_BASE.extend(
        {
            cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
            cv.Optional(
                CONF_UPDATE_INTERVAL, default="30s"
            ): cv.positive_time_period_milliseconds,
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
        table_expr, table_len = emit_flash_enum_table(enum_name)
        cg.add(
            hub.register_enum_table(
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                table_expr,
                table_len,
                node_id_val,
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
