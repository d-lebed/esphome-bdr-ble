"""Select platform for writable BDR Thermea (De Dietrich / Baxi / Remeha) BLE enum parameters."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_ID,
    CONF_INTERNAL,
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
from ..obd import get_enum_mapping, infer_sensor_metadata, resolve_parameter

DEPENDENCIES = ["bdr_ble"]

CONF_SELECTS = "selects"
CONF_RESOLVED_INDEX = "_obd_index"
CONF_RESOLVED_SUB = "_obd_sub"
CONF_RESOLVED_ENUM = "_obd_enum"

BdrBleSelect = bdr_ble_ns.class_(
    "BdrBleSelect", select.Select, cg.Parented.template(BdrBleComponent)
)


def _preprocess_select_item(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise cv.Invalid("Select configuration must be a mapping")
    if CONF_PARAMETER not in value:
        raise cv.Invalid(
            f"Missing required '{CONF_PARAMETER}' (e.g. 'AP111', 'CP020', or '0x3404:00')"
        )

    cfg = dict(value)
    sub_override = cfg.get(CONF_SUB_INDEX)
    if sub_override is not None:
        sub_override = cv.int_range(min=0, max=255)(sub_override)

    dp = resolve_parameter(cfg[CONF_PARAMETER], sub_override)
    dtype = dp["type"]
    enum_name = dp.get("enum", "")
    if dtype != "Enum" or not enum_name:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has type '{dtype}' (not an Enum). "
            f"Use 'number:' for numeric parameters instead of 'select:'."
        )

    enum_map = get_enum_mapping(enum_name)
    if not enum_map:
        raise cv.Invalid(
            f"OBD enum '{enum_name}' for parameter '{cfg[CONF_PARAMETER]}' has no defined options."
        )

    meta = infer_sensor_metadata(dp)
    if CONF_NAME not in cfg and CONF_ID not in cfg and not cfg.get(CONF_INTERNAL, False):
        cfg[CONF_NAME] = meta["default_name"]

    if CONF_ICON not in cfg:
        cfg[CONF_ICON] = "mdi:tune-variant"
    if CONF_ENTITY_CATEGORY not in cfg:
        cfg[CONF_ENTITY_CATEGORY] = ENTITY_CATEGORY_CONFIG

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_ENUM] = enum_name
    return cfg


_SELECT_ITEM_BASE = select.select_schema(
    BdrBleSelect,
    entity_category=ENTITY_CATEGORY_CONFIG,
    icon="mdi:tune-variant",
).extend(
    {
        cv.Required(CONF_PARAMETER): cv.string,
        cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_ENUM): cv.string,
    }
)

_SINGLE_SELECT_SCHEMA = cv.All(
    _preprocess_select_item,
    _SELECT_ITEM_BASE.extend(
        {
            cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
            cv.Optional(
                CONF_UPDATE_INTERVAL, default="30s"
            ): cv.positive_time_period_milliseconds,
        }
    ),
)

_GROUP_SELECT_ITEM_SCHEMA = cv.All(
    _preprocess_select_item,
    _SELECT_ITEM_BASE.extend(
        {
            cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
        }
    ),
)

_GROUP_SELECT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(
            CONF_UPDATE_INTERVAL, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SELECTS): cv.ensure_list(_GROUP_SELECT_ITEM_SCHEMA),
        cv.Optional(CONF_SENSORS): cv.ensure_list(_GROUP_SELECT_ITEM_SCHEMA),
    }
)


def _validate_platform_config(config: Any) -> dict[str, Any]:
    if isinstance(config, dict) and (
        CONF_SELECTS in config or CONF_SENSORS in config
    ):
        return _GROUP_SELECT_SCHEMA(config)
    return _SINGLE_SELECT_SCHEMA(config)


CONFIG_SCHEMA = cv.All(_validate_platform_config)


async def _register_one_select(
    hub: Any,
    item_cfg: dict[str, Any],
    default_interval_ms: int,
    default_node_id: int | None,
) -> None:
    enum_map = get_enum_mapping(item_cfg[CONF_RESOLVED_ENUM])
    # Deduplicate option strings while preserving order
    options: list[str] = []
    seen: set[str] = set()
    for desc in enum_map.values():
        if desc not in seen:
            seen.add(desc)
            options.append(desc)

    sel = await select.new_select(item_cfg, options=options)
    await cg.register_parented(sel, hub)

    interval = item_cfg.get(CONF_UPDATE_INTERVAL)
    interval_ms = (
        interval.total_milliseconds if interval is not None else default_interval_ms
    )
    node_id = item_cfg.get(CONF_NODE_ID, default_node_id)
    node_id_val = 0xFF if node_id is None else node_id

    cg.add(
        sel.set_datapoint_meta(
            node_id_val,
            item_cfg[CONF_RESOLVED_INDEX],
            item_cfg[CONF_RESOLVED_SUB],
        )
    )
    for raw_val, desc in enum_map.items():
        cg.add(sel.add_enum_option(raw_val, desc))
        cg.add(
            hub.register_enum_translation(
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                raw_val,
                desc,
            )
        )

    cg.add(
        hub.register_select(
            sel,
            item_cfg[CONF_RESOLVED_INDEX],
            item_cfg[CONF_RESOLVED_SUB],
            interval_ms,
            node_id_val,
        )
    )


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    items = config.get(CONF_SELECTS) or config.get(CONF_SENSORS)
    if items is not None:
        default_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
        default_node_id = config.get(CONF_NODE_ID)
        for item_cfg in items:
            await _register_one_select(
                hub, item_cfg, default_interval_ms, default_node_id
            )
    else:
        await _register_one_select(
            hub,
            config,
            config[CONF_UPDATE_INTERVAL].total_milliseconds,
            config.get(CONF_NODE_ID),
        )
