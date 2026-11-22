"""Select platform for writable BDR Thermea (De Dietrich / Baxi / Remeha) BLE enum parameters."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
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
from ..obd import get_enum_mapping, infer_sensor_metadata
from ..schema_helpers import (
    CONF_DEVICE_LIMITS,
    CONF_RESOLVED_ENUM,
    CONF_RESOLVED_INDEX,
    CONF_RESOLVED_SUB,
    apply_default_name,
    emit_flash_enum_table,
    iter_platform_items,
    make_platform_schema,
    resolve_item_datapoint,
)

DEPENDENCIES = ["bdr_ble"]

CONF_SELECTS = "selects"

BdrBleSelect = bdr_ble_ns.class_(
    "BdrBleSelect",
    select.Select,
    cg.Component,
    cg.Parented.template(BdrBleComponent),
)


def _preprocess_select_item(value: Any) -> dict[str, Any]:
    cfg, dp = resolve_item_datapoint(
        value, "Select", "e.g. 'AP111', 'CP020', or '0x3404:00'"
    )
    dtype = dp["type"]
    enum_name = dp.get("enum", "")
    if dtype != "Enum" or not enum_name:
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has type '{dtype}' (not an Enum). "
            f"Use 'number:' for numeric parameters instead of 'select:'."
        )
    if not dp.get("rw", False):
        raise cv.Invalid(
            f"OBD enum datapoint '{cfg[CONF_PARAMETER]}' ({dp['name']}) is read-only ({dp.get('cls', 'Signal')}). "
            f"Use 'text_sensor:' instead of 'select:'."
        )

    enum_map = get_enum_mapping(enum_name)
    if not enum_map:
        raise cv.Invalid(
            f"OBD enum '{enum_name}' for parameter '{cfg[CONF_PARAMETER]}' has no defined options."
        )

    meta = infer_sensor_metadata(dp)
    apply_default_name(cfg, meta["default_name"])

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
        cv.Optional(CONF_DEVICE_LIMITS, default=True): cv.boolean,
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_ENUM): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = make_platform_schema(
    _preprocess_select_item,
    _SELECT_ITEM_BASE,
    CONF_SELECTS,
    default_interval="30s",
    allow_sensors_alias=True,
)


async def to_code(config: dict[str, Any]) -> None:
    cg.add_define("USE_BDR_BLE_SELECT")
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    for item_cfg, interval_ms, node_id_val in iter_platform_items(config, CONF_SELECTS):
        enum_name = item_cfg[CONF_RESOLVED_ENUM]
        enum_map = get_enum_mapping(enum_name)
        options: list[str] = []
        seen: set[str] = set()
        for desc in enum_map.values():
            if desc not in seen:
                seen.add(desc)
                options.append(desc)

        sel = await select.new_select(item_cfg, options=options)
        await cg.register_component(sel, item_cfg)
        await cg.register_parented(sel, hub)

        table_expr, table_len = emit_flash_enum_table(enum_name)
        cg.add(
            sel.set_datapoint_meta(
                node_id_val,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
            )
        )
        cg.add(sel.set_enum_table(table_expr, table_len))
        cg.add(sel.set_use_device_limits(item_cfg[CONF_DEVICE_LIMITS]))
        cg.add(
            hub.register_select(
                sel,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                interval_ms,
                node_id_val,
            )
        )
        cg.add(
            hub.register_enum_table(
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                table_expr,
                table_len,
                node_id_val,
            )
        )
