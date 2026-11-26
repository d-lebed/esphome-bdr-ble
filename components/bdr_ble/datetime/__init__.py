"""Datetime platform for writable BDR Thermea (De Dietrich / Baxi / Remeha) BLE TimeOfDay parameters."""

from __future__ import annotations

import inspect
import logging
from typing import Any

from esphome import automation
import esphome.codegen as cg
from esphome.components import datetime
import esphome.config_validation as cv
from esphome.const import (
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_ID,
    CONF_TYPE,
    ENTITY_CATEGORY_CONFIG,
)
from esphome.core import CORE

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
    CONF_RESOLVED_SUB,
    apply_default_name,
    iter_platform_items,
    make_platform_schema,
    resolve_item_datapoint,
)

DEPENDENCIES = ["bdr_ble"]
_LOGGER = logging.getLogger(__name__)

CONF_DATETIMES = "datetimes"

BdrBleDateTime = bdr_ble_ns.class_(
    "BdrBleDateTime",
    datetime.DateTimeEntity,
    cg.Parented.template(BdrBleComponent),
)
ClearDateTimeAction = bdr_ble_ns.class_("ClearDateTimeAction", automation.Action)

_REGISTER_ACTION_KWARGS = (
    {"synchronous": True}
    if "synchronous" in inspect.signature(automation.register_action).parameters
    else {}
)


@automation.register_action(
    "bdr_ble.clear_datetime",
    ClearDateTimeAction,
    automation.maybe_simple_id(
        {
            cv.Required(CONF_ID): cv.use_id(BdrBleDateTime),
        }
    ),
    **_REGISTER_ACTION_KWARGS,
)
async def clear_datetime_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


def _preprocess_datetime_item(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise cv.Invalid("Datetime configuration must be a mapping")

    # Preserve ESPHome's datetime entity 'type: DATETIME' without forwarding it
    # as an OBD type override to resolve_item_datapoint().
    sanitized = dict(value)
    entity_type = sanitized.pop(CONF_TYPE, "DATETIME")
    if str(entity_type).upper() != "DATETIME":
        raise cv.Invalid(
            f"Unsupported datetime type '{entity_type}'. "
            f"BDR CiA 301 TimeOfDay parameters require type: DATETIME."
        )

    cfg, dp = resolve_item_datapoint(
        sanitized,
        "Datetime",
        "e.g. 'parZoneStartTimeHoliday', 'DP170', '0x3423:01', or 'AP050'",
    )
    dtype = dp["type"]
    if dtype != "TimeOfDay":
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) has type '{dtype}' (not TimeOfDay). "
            f"Use 'number:', 'select:', or 'text:' instead of 'datetime:'."
        )
    if not dp.get("rw", False):
        raise cv.Invalid(
            f"OBD TimeOfDay datapoint '{cfg[CONF_PARAMETER]}' ({dp['name']}) is read-only ({dp.get('cls', 'Signal')}). "
            f"Use 'text_sensor:' instead of 'datetime:'."
        )

    meta = infer_sensor_metadata(dp)
    apply_default_name(cfg, meta["default_name"])

    cfg[CONF_TYPE] = "DATETIME"
    if CONF_ICON not in cfg:
        cfg[CONF_ICON] = "mdi:calendar-clock"
    if CONF_ENTITY_CATEGORY not in cfg:
        cfg[CONF_ENTITY_CATEGORY] = ENTITY_CATEGORY_CONFIG

    cfg[CONF_RESOLVED_INDEX] = dp["index"]
    cfg[CONF_RESOLVED_SUB] = dp["sub"]
    return cfg


_DATETIME_ITEM_BASE = datetime.datetime_schema(BdrBleDateTime).extend(
    {
        cv.Required(CONF_PARAMETER): cv.string,
        cv.Optional(CONF_SUB_INDEX): cv.int_range(min=0, max=255),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Required(CONF_RESOLVED_INDEX): cv.int_,
        cv.Required(CONF_RESOLVED_SUB): cv.int_,
    }
)

CONFIG_SCHEMA = make_platform_schema(
    _preprocess_datetime_item,
    _DATETIME_ITEM_BASE,
    CONF_DATETIMES,
    default_interval="30s",
    allow_sensors_alias=True,
)


async def to_code(config: dict[str, Any]) -> None:
    cg.add_define("USE_BDR_BLE_DATETIME")
    if "time" not in CORE.config:
        _LOGGER.warning(
            "Using 'datetime: platform: bdr_ble' without a 'time:' component configured. "
            "ESPHome exchanges DateTimeEntity values as UTC epoch seconds, so add "
            "'time: - platform: homeassistant' (or 'sntp:' with 'timezone:') to keep "
            "Home Assistant's picker aligned with the boiler's local clock."
        )

    hub = await cg.get_variable(config[CONF_BDR_BLE_ID])
    for item_cfg, interval_ms, node_id_val in iter_platform_items(
        config, CONF_DATETIMES
    ):
        dt_var = await datetime.new_datetime(item_cfg)
        await cg.register_parented(dt_var, hub)

        cg.add(
            dt_var.set_datapoint_meta(
                node_id_val,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
            )
        )
        cg.add(
            hub.register_datetime(
                dt_var,
                item_cfg[CONF_RESOLVED_INDEX],
                item_cfg[CONF_RESOLVED_SUB],
                interval_ms,
                node_id_val,
            )
        )
