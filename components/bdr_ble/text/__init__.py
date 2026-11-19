"""Text platform for writable BDR Thermea (De Dietrich / Baxi / Remeha) BLE string parameters."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import text
import esphome.config_validation as cv
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_ID,
    CONF_INTERNAL,
    CONF_MAX_LENGTH,
    CONF_MIN_LENGTH,
    CONF_MODE,
    CONF_NAME,
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
from ..obd import infer_sensor_metadata, resolve_parameter

DEPENDENCIES = ["bdr_ble"]

CONF_TEXTS = "texts"
CONF_RESOLVED_INDEX = "_obd_index"
CONF_RESOLVED_SUB = "_obd_sub"
CONF_RESOLVED_MAX_LEN = "_obd_max_len"

BdrBleText = bdr_ble_ns.class_(
    "BdrBleText", text.Text, cg.Parented.template(BdrBleComponent)
)


def _preprocess_text_item(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise cv.Invalid("Text configuration must be a mapping")
    if CONF_PARAMETER not in value:
        raise cv.Invalid(
            f"Missing required '{CONF_PARAMETER}' (e.g. 'parZoneFriendlyName', 'parZoneFriendlyNameUserActivity', or 'AP089')"
        )

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
    if dtype != "VisibleString":
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has type '{dtype}' (not VisibleString). "
            f"Use 'number:' for numeric parameters or 'select:' for Enum parameters instead of 'text:'."
        )
    if not dp.get("rw", False):
        raise cv.Invalid(
            f"OBD string datapoint '{cfg[CONF_PARAMETER]}' ({dp['name']}) is read-only ({dp.get('cls', 'Signal')}). "
            f"Use 'text_sensor:' instead of 'text:'."
        )

    meta = infer_sensor_metadata(dp)
    if CONF_NAME not in cfg and CONF_ID not in cfg and not cfg.get(CONF_INTERNAL, False):
        cfg[CONF_NAME] = meta["default_name"]

    if CONF_ICON not in cfg:
        cfg[CONF_ICON] = "mdi:form-textbox"
    if CONF_ENTITY_CATEGORY not in cfg:
        cfg[CONF_ENTITY_CATEGORY] = ENTITY_CATEGORY_CONFIG
    if CONF_MODE not in cfg:
        cfg[CONF_MODE] = "TEXT"

    max_len = int(cfg.get(CONF_MAX_LENGTH, dp.get("len", 20)))
    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_MAX_LEN] = max(1, min(255, max_len))
    return cfg


_TEXT_ITEM_BASE = text.text_schema(
    BdrBleText,
    entity_category=ENTITY_CATEGORY_CONFIG,
    icon="mdi:form-textbox",
    mode="TEXT",
).extend(
    {
        cv.Required(CONF_PARAMETER): cv.string,
        cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(CONF_MIN_LENGTH, default=0): cv.int_range(min=0, max=255),
        cv.Optional(CONF_MAX_LENGTH): cv.int_range(min=1, max=255),
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_MAX_LEN): cv.int_,
    }
)

_SINGLE_TEXT_SCHEMA = cv.All(
    _preprocess_text_item,
    _TEXT_ITEM_BASE.extend(
        {
            cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
            cv.Optional(
                CONF_UPDATE_INTERVAL, default="30s"
            ): cv.positive_time_period_milliseconds,
        }
    ),
)

_GROUP_TEXT_ITEM_SCHEMA = cv.All(
    _preprocess_text_item,
    _TEXT_ITEM_BASE.extend(
        {
            cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
        }
    ),
)

_GROUP_TEXT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(
            CONF_UPDATE_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TEXTS): cv.ensure_list(_GROUP_TEXT_ITEM_SCHEMA),
        cv.Optional(CONF_SENSORS): cv.ensure_list(_GROUP_TEXT_ITEM_SCHEMA),
    }
)


def _validate_platform_config(config: Any) -> dict[str, Any]:
    if isinstance(config, dict) and (CONF_TEXTS in config or CONF_SENSORS in config):
        return _GROUP_TEXT_SCHEMA(config)
    return _SINGLE_TEXT_SCHEMA(config)


CONFIG_SCHEMA = cv.All(_validate_platform_config)


async def _register_one_text(
    hub: Any,
    item_cfg: dict[str, Any],
    default_interval_ms: int,
    default_node_id: int | None,
) -> None:
    min_len = int(item_cfg.get(CONF_MIN_LENGTH, 0))
    max_len = int(item_cfg[CONF_RESOLVED_MAX_LEN])

    txt = await text.new_text(item_cfg, min_length=min_len, max_length=max_len)
    await cg.register_parented(txt, hub)

    interval = item_cfg.get(CONF_UPDATE_INTERVAL)
    interval_ms = (
        interval.total_milliseconds if interval is not None else default_interval_ms
    )
    node_id = item_cfg.get(CONF_NODE_ID, default_node_id)
    node_id_val = 0xFF if node_id is None else node_id

    cg.add(
        txt.set_datapoint_meta(
            node_id_val,
            item_cfg[CONF_RESOLVED_INDEX],
            item_cfg[CONF_RESOLVED_SUB],
            max_len,
        )
    )
    cg.add(
        hub.register_text(
            txt,
            item_cfg[CONF_RESOLVED_INDEX],
            item_cfg[CONF_RESOLVED_SUB],
            interval_ms,
            node_id_val,
        )
    )


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    default_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
    default_node_id = config.get(CONF_NODE_ID)

    if CONF_TEXTS in config or CONF_SENSORS in config:
        items = list(config.get(CONF_TEXTS, [])) + list(config.get(CONF_SENSORS, []))
        for item_cfg in items:
            await _register_one_text(hub, item_cfg, default_interval_ms, default_node_id)
    else:
        await _register_one_text(hub, config, default_interval_ms, default_node_id)
