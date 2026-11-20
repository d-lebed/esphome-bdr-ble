"""Text platform for writable BDR Thermea (De Dietrich / Baxi / Remeha) BLE string parameters."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
from esphome.components import text
import esphome.config_validation as cv
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_MAX_LENGTH,
    CONF_MIN_LENGTH,
    CONF_MODE,
    CONF_TYPE,
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
from ..obd import infer_sensor_metadata
from ..schema_helpers import (
    CONF_RESOLVED_INDEX,
    CONF_RESOLVED_MAX_LEN,
    CONF_RESOLVED_SUB,
    apply_default_name,
    iter_platform_items,
    make_platform_schema,
    resolve_item_datapoint,
)

DEPENDENCIES = ["bdr_ble"]

CONF_TEXTS = "texts"

BdrBleText = bdr_ble_ns.class_(
    "BdrBleText", text.Text, cg.Parented.template(BdrBleComponent)
)


def _preprocess_text_item(value: Any) -> dict[str, Any]:
    cfg, dp = resolve_item_datapoint(
        value,
        "Text",
        "e.g. 'parZoneFriendlyName', 'parZoneFriendlyNameUserActivity', or 'AP089'",
    )
    dtype = dp["type"]
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
    apply_default_name(cfg, meta["default_name"])

    if CONF_ICON not in cfg:
        cfg[CONF_ICON] = "mdi:form-textbox"
    if CONF_ENTITY_CATEGORY not in cfg:
        cfg[CONF_ENTITY_CATEGORY] = ENTITY_CATEGORY_CONFIG
    if CONF_MODE not in cfg:
        cfg[CONF_MODE] = "TEXT"

    min_len = int(cfg.get(CONF_MIN_LENGTH, 0))
    max_len = max(1, min(255, int(cfg.get(CONF_MAX_LENGTH, dp.get("len", 20)))))
    if min_len > max_len:
        raise cv.Invalid(
            f"min_length ({min_len}) cannot be greater than max_length ({max_len}) "
            f"for parameter '{cfg[CONF_PARAMETER]}'"
        )

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    cfg[CONF_RESOLVED_MAX_LEN] = max_len
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
        cv.Optional(CONF_TYPE): cv.string,
        cv.Optional(CONF_MIN_LENGTH, default=0): cv.int_range(min=0, max=255),
        cv.Optional(CONF_MAX_LENGTH): cv.int_range(min=1, max=255),
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
        cv.Required(CONF_RESOLVED_MAX_LEN): cv.int_,
    }
)

CONFIG_SCHEMA = make_platform_schema(
    _preprocess_text_item,
    _TEXT_ITEM_BASE,
    CONF_TEXTS,
    default_interval="30s",
    allow_sensors_alias=True,
)


async def to_code(config: dict[str, Any]) -> None:
    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    for item_cfg, interval_ms, node_id_val in iter_platform_items(config, CONF_TEXTS):
        min_len = int(item_cfg.get(CONF_MIN_LENGTH, 0))
        max_len = int(item_cfg[CONF_RESOLVED_MAX_LEN])

        txt = await text.new_text(item_cfg, min_length=min_len, max_length=max_len)
        await cg.register_parented(txt, hub)

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
