"""Compile-time BDR Thermea OBD dictionary loader and resolver for ESPHome."""

from __future__ import annotations

import json
from pathlib import Path
import re
from typing import Any

import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_PRESSURE,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_VOLUME,
    DEVICE_CLASS_VOLUME_FLOW_RATE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
)

_CATALOG_CACHE: dict[str, Any] | None = None

# Mapping from OBD type string to C++ enum BdrDataType integer value
DATA_TYPE_MAP: dict[str, int] = {
    "U8": 0,
    "I8": 1,
    "U16": 2,
    "I16": 3,
    "U32": 4,
    "I32": 5,
    "Enum": 6,
    "VisibleString": 7,
    "OctetString": 8,
    "TimeOfDay": 9,
}

NUMERIC_DATA_TYPES = {"U8", "I8", "U16", "I16", "U32", "I32", "Enum"}
TEXT_DATA_TYPES = {"Enum", "VisibleString", "OctetString", "TimeOfDay"}

_HEX_INDEX_RE = re.compile(
    r"^(?:0x)?([0-9a-fA-F]{4})(?::(?:0x)?([0-9a-fA-F]{1,2}))?$"
)


def _load_catalog() -> dict[str, Any]:
    global _CATALOG_CACHE
    if _CATALOG_CACHE is not None:
        return _CATALOG_CACHE

    catalog_path = Path(__file__).parent / "obd_catalog.json"
    with catalog_path.open("r", encoding="utf-8") as f:
        raw = json.load(f)

    by_code: dict[str, dict[str, Any]] = {}
    by_name: dict[str, dict[str, Any]] = {}
    by_idx_sub: dict[tuple[int, int], dict[str, Any]] = {}
    by_idx: dict[int, dict[str, Any]] = {}

    for dp in raw["datapoints"]:
        idx = dp["index"]
        sub = dp["sub"]
        by_idx_sub[(idx, sub)] = dp
        if idx not in by_idx or sub == 0:
            by_idx[idx] = dp

        name_key = dp["name"].lower()
        if name_key:
            by_name[name_key] = dp

        code_key = dp.get("code", "").lower()
        if code_key:
            # Prefer non-deprecated datapoint when duplicate FriendlyName exists
            existing = by_code.get(code_key)
            if existing is None or (existing.get("deprecated") and not dp.get("deprecated")):
                by_code[code_key] = dp

    _CATALOG_CACHE = {
        "by_code": by_code,
        "by_name": by_name,
        "by_idx_sub": by_idx_sub,
        "by_idx": by_idx,
        "enums": raw.get("enums", {}),
    }
    return _CATALOG_CACHE


def resolve_parameter(
    param: str | int, sub_index_override: int | None = None
) -> dict[str, Any]:
    """Resolve a parameter specified by FriendlyName (AC005), OBD Name (varAp...), or hex Index (0x5044:00)."""
    cat = _load_catalog()
    param_str = str(param).strip()

    inline_sub: int | None = None
    m = _HEX_INDEX_RE.match(param_str)
    if m:
        idx = int(m.group(1), 16)
        explicit_sub = int(m.group(2), 16) if m.group(2) is not None else None
        if sub_index_override is not None:
            explicit_sub = sub_index_override
        dp = (
            cat["by_idx_sub"].get((idx, explicit_sub))
            if explicit_sub is not None
            else None
        ) or cat["by_idx"].get(idx)
        if dp is not None:
            res = dict(dp)
            if explicit_sub is not None:
                res["sub"] = explicit_sub
            return res
        # Allow raw unregistered hex index:sub_index with default U16/I16 fallback if explicitly given
        raise cv.Invalid(
            f"Unknown OBD hex index 0x{idx:04X}:0x{sub:02X}. "
            f"Use a valid FriendlyName (e.g. 'AC005'), OBD Name (e.g. 'varApTOutside'), or known hex index."
        )

    if ":" in param_str:
        base_str, sub_str = param_str.split(":", 1)
        param_str = base_str.strip()
        try:
            inline_sub = int(sub_str.strip(), 0)
        except ValueError as err:
            raise cv.Invalid(f"Invalid sub_index in '{param}': {sub_str}") from err

    key = param_str.lower()
    dp = cat["by_code"].get(key) or cat["by_name"].get(key)
    if dp is None:
        raise cv.Invalid(
            f"Unknown BDR OBD parameter '{param}'. "
            f"Specify a valid menu code (e.g. 'AC005', 'AM027', 'BM000'), "
            f"OBD signal name (e.g. 'varApEnergyConsumptionTotal'), or hex '0x5044:00'."
        )

    res = dict(dp)
    if sub_index_override is not None:
        res["sub"] = sub_index_override
    elif inline_sub is not None:
        res["sub"] = inline_sub
    return res


def get_enum_mapping(enum_name: str) -> dict[int, str]:
    """Return {int_value: description} for a given OBD EnumName."""
    cat = _load_catalog()
    raw_map = cat["enums"].get(enum_name, {})
    out: dict[int, str] = {}
    for k, v in raw_map.items():
        try:
            out[int(k, 0)] = v
        except ValueError:
            continue
    return dict(sorted(out.items()))


def infer_accuracy_decimals(gain: float) -> int:
    if gain <= 0:
        return 0
    if gain < 0.005:
        return 3
    if gain < 0.05 or abs(gain - 0.25) < 1e-6:
        return 2
    if gain < 0.5:
        return 1
    return 0


def infer_sensor_metadata(dp: dict[str, Any]) -> dict[str, Any]:
    """Infer ESPHome unit_of_measurement, accuracy_decimals, device_class, state_class, and icon from OBD."""
    unit_raw = dp.get("unit", "")
    cls_raw = dp.get("cls", "")
    gain = float(dp.get("gain", 1.0))

    meta: dict[str, Any] = {
        "accuracy_decimals": infer_accuracy_decimals(gain),
        "state_class": (
            STATE_CLASS_TOTAL_INCREASING
            if cls_raw == "Counter"
            else STATE_CLASS_MEASUREMENT
        ),
    }

    unit_map: dict[str, tuple[str, str | None, str | None]] = {
        "°C": ("°C", DEVICE_CLASS_TEMPERATURE, "mdi:thermometer"),
        "K": ("K", None, "mdi:thermometer-lines"),
        "kWh": ("kWh", DEVICE_CLASS_ENERGY, "mdi:lightning-bolt"),
        "Wh": ("Wh", DEVICE_CLASS_ENERGY, "mdi:lightning-bolt"),
        "kW": ("kW", DEVICE_CLASS_POWER, "mdi:flash"),
        "W": ("W", DEVICE_CLASS_POWER, "mdi:flash"),
        "bar": ("bar", DEVICE_CLASS_PRESSURE, "mdi:gauge"),
        "kPa": ("kPa", DEVICE_CLASS_PRESSURE, "mdi:gauge"),
        "Pa": ("Pa", DEVICE_CLASS_PRESSURE, "mdi:gauge"),
        "%": ("%", None, "mdi:percent"),
        "l/m": ("L/min", DEVICE_CLASS_VOLUME_FLOW_RATE, "mdi:water-pump"),
        "l": ("L", DEVICE_CLASS_VOLUME, "mdi:water"),
        "Rpm": ("rpm", None, "mdi:fan"),
        "Hz": ("Hz", DEVICE_CLASS_FREQUENCY, "mdi:sine-wave"),
        "A": ("A", DEVICE_CLASS_CURRENT, "mdi:current-ac"),
        "µA": ("µA", DEVICE_CLASS_CURRENT, "mdi:current-dc"),
        "V": ("V", DEVICE_CLASS_VOLTAGE, "mdi:flash-triangle"),
        "Hours": ("h", DEVICE_CLASS_DURATION, "mdi:timer-outline"),
        "Min": ("min", DEVICE_CLASS_DURATION, "mdi:timer-outline"),
        "Sec": ("s", DEVICE_CLASS_DURATION, "mdi:timer-outline"),
        "mSec": ("ms", DEVICE_CLASS_DURATION, "mdi:timer-outline"),
        "Days": ("d", DEVICE_CLASS_DURATION, "mdi:calendar-clock"),
    }

    if unit_raw in unit_map:
        u_str, dev_cls, icon = unit_map[unit_raw]
        meta["unit_of_measurement"] = u_str
        if dev_cls is not None:
            meta["device_class"] = dev_cls
        if icon is not None:
            meta["icon"] = icon
        if unit_raw in ("kWh", "Wh"):
            meta["state_class"] = STATE_CLASS_TOTAL_INCREASING
    elif unit_raw:
        meta["unit_of_measurement"] = unit_raw

    desc = dp.get("label", "") or dp.get("desc", "") or dp.get("name", "")
    meta["default_name"] = desc.strip()
    return meta


def infer_number_metadata(dp: dict[str, Any]) -> dict[str, Any]:
    """Infer ESPHome number metadata (min_value, max_value, step, unit, device_class, icon) from OBD."""
    base = infer_sensor_metadata(dp)
    dtype = dp.get("type", "U16")
    unit_raw = dp.get("unit", "")
    gain = float(dp.get("gain", 1.0))
    step = gain if gain > 0 else 1.0

    # Sensible default bounds based on unit & signedness (overridden by YAML or 0x502B descriptor at runtime)
    is_signed = dtype in ("I8", "I16", "I32")
    if unit_raw == "%":
        min_val, max_val = (0.0, 100.0)
    elif unit_raw in ("°C", "K"):
        min_val, max_val = (-30.0 if is_signed else 0.0, 90.0)
    elif unit_raw == "bar":
        min_val, max_val = (0.0, 5.0)
    elif dtype == "U8":
        min_val, max_val = (0.0, round(254 * gain, 4))
    elif dtype == "I8":
        min_val, max_val = (round(-127 * gain, 4), round(127 * gain, 4))
    elif dtype == "U16":
        min_val, max_val = (0.0, round(10000 * gain, 4))
    elif dtype == "I16":
        min_val, max_val = (round(-3200 * gain, 4), round(10000 * gain, 4))
    else:
        min_val, max_val = (0.0 if not is_signed else -10000.0, 100000.0)

    meta: dict[str, Any] = {
        "min_value": min_val,
        "max_value": max_val,
        "step": step,
        "default_name": base["default_name"],
        "icon": base.get("icon", "mdi:tune"),
    }
    if "unit_of_measurement" in base:
        meta["unit_of_measurement"] = base["unit_of_measurement"]
    if "device_class" in base:
        meta["device_class"] = base["device_class"]
    return meta
