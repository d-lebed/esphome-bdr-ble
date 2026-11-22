"""Shared schema validation and Flash enum codegen helpers for BDR Thermea BLE platforms."""

from __future__ import annotations

from collections.abc import Callable, Iterator
import re
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INTERNAL,
    CONF_NAME,
    CONF_TYPE,
    CONF_UPDATE_INTERVAL,
)
from esphome.core import CORE

from . import (
    CONF_BDR_BLE_ID,
    CONF_NODE_ID,
    CONF_PARAMETER,
    CONF_SENSORS,
    CONF_SUB_INDEX,
    BdrBleComponent,
)
from .obd import get_enum_mapping, resolve_parameter

CONF_SCALE = "scale"
CONF_DEVICE_LIMITS = "device_limits"
CONF_RESOLVED_INDEX = "_obd_index"
CONF_RESOLVED_SUB = "_obd_sub"
CONF_RESOLVED_TYPE = "_obd_type"
CONF_RESOLVED_GAIN = "_obd_gain"
CONF_RESOLVED_ENUM = "_obd_enum"
CONF_RESOLVED_MAX_LEN = "_obd_max_len"


def resolve_item_datapoint(
    value: Any, platform_label: str, example_hint: str
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Validate mapping, resolve OBD parameter, and reject composite Struct tables."""
    if not isinstance(value, dict):
        raise cv.Invalid(f"{platform_label} configuration must be a mapping")
    if CONF_PARAMETER not in value:
        raise cv.Invalid(f"Missing required '{CONF_PARAMETER}' ({example_hint})")

    cfg = dict(value)
    sub_override = cfg.get(CONF_SUB_INDEX)
    if sub_override is not None:
        sub_override = cv.int_range(min=0, max=255)(sub_override)

    raw_type = cfg.get(CONF_TYPE)
    if raw_type is not None:
        raw_type = str(raw_type).strip()

    dp = resolve_parameter(cfg[CONF_PARAMETER], sub_override, raw_type)
    if dp["type"] == "Struct":
        raise cv.Invalid(
            f"OBD parameter '{cfg[CONF_PARAMETER]}' ({dp['name']}) is a composite Struct table "
            f"and cannot be polled as a single entity."
        )
    return cfg, dp


def apply_default_name(cfg: dict[str, Any], default_name: str) -> None:
    if CONF_NAME not in cfg and CONF_ID not in cfg and not cfg.get(CONF_INTERNAL, False):
        cfg[CONF_NAME] = default_name


def make_platform_schema(
    preprocess_fn: Callable[[Any], dict[str, Any]],
    item_base_schema: cv.Schema,
    primary_list_key: str,
    default_interval: str = "30s",
    allow_sensors_alias: bool = True,
) -> cv.Schema:
    """Build a platform CONFIG_SCHEMA supporting both single-entity and grouped batch syntax."""
    single_schema = cv.All(
        preprocess_fn,
        item_base_schema.extend(
            {
                cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
                cv.Optional(
                    CONF_UPDATE_INTERVAL, default=default_interval
                ): cv.positive_time_period_milliseconds,
            }
        ),
    )

    group_item_schema = cv.All(
        preprocess_fn,
        item_base_schema.extend(
            {
                cv.Optional(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
            }
        ),
    )

    group_dict: dict[Any, Any] = {
        cv.GenerateID(CONF_BDR_BLE_ID): cv.use_id(BdrBleComponent),
        cv.Optional(CONF_NODE_ID): cv.int_range(min=0, max=255),
        cv.Optional(
            CONF_UPDATE_INTERVAL, default=default_interval
        ): cv.positive_time_period_milliseconds,
    }

    if allow_sensors_alias and primary_list_key != CONF_SENSORS:
        group_dict[cv.Optional(primary_list_key)] = cv.ensure_list(group_item_schema)
        group_dict[cv.Optional(CONF_SENSORS)] = cv.ensure_list(group_item_schema)
        group_schema = cv.All(
            cv.Schema(group_dict),
            cv.has_exactly_one_key(primary_list_key, CONF_SENSORS),
        )
    else:
        group_dict[cv.Required(primary_list_key)] = cv.ensure_list(group_item_schema)
        group_schema = cv.Schema(group_dict)

    def _validate(config: Any) -> dict[str, Any]:
        if isinstance(config, dict) and (
            primary_list_key in config
            or (allow_sensors_alias and CONF_SENSORS in config)
        ):
            return group_schema(config)
        return single_schema(config)

    return cv.All(_validate)


def iter_platform_items(
    config: dict[str, Any], primary_list_key: str
) -> Iterator[tuple[dict[str, Any], int, int]]:
    """Yield `(item_cfg, interval_ms, node_id_val)` for single or grouped platform configs."""
    default_interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
    default_node_id = config.get(CONF_NODE_ID)

    if primary_list_key in config:
        items = config[primary_list_key]
    elif CONF_SENSORS in config:
        items = config[CONF_SENSORS]
    else:
        items = None

    if items is not None:
        for item_cfg in items:
            interval = item_cfg.get(CONF_UPDATE_INTERVAL)
            interval_ms = (
                interval.total_milliseconds
                if interval is not None
                else default_interval_ms
            )
            node_id = item_cfg.get(CONF_NODE_ID, default_node_id)
            node_id_val = 0xFF if node_id is None else node_id
            yield item_cfg, interval_ms, node_id_val
    else:
        node_id_val = 0xFF if default_node_id is None else default_node_id
        yield config, default_interval_ms, node_id_val


def _escape_c_string(s: str) -> str:
    escaped = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def emit_flash_enum_table(enum_name: str) -> tuple[cg.RawExpression, int]:
    """Emit a single static constexpr EnumEntry array in Flash (.rodata) per unique enum_name."""
    enum_map = get_enum_mapping(enum_name)
    safe_id = re.sub(r"[^a-zA-Z0-9_]", "_", enum_name)
    symbol_name = f"bdr_enum_{safe_id}"

    emitted: set[str] = CORE.data.setdefault("bdr_ble_emitted_enums", set())
    if symbol_name not in emitted:
        emitted.add(symbol_name)
        entries_str = ", ".join(
            f"{{{int(val)}, {_escape_c_string(desc)}}}"
            for val, desc in enum_map.items()
        )
        stmt = (
            f"static constexpr esphome::bdr_ble::EnumEntry {symbol_name}[] = "
            f"{{{entries_str}}};"
        )
        cg.add_global(cg.RawStatement(stmt))

    return cg.RawExpression(symbol_name), len(enum_map)
