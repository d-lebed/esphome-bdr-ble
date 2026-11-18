# AGENTS.md — Contributor & AI Agent Guide

This repository contains the **`bdr_ble`** ESPHome external component for **BDR Thermea Group** heat pumps, condensing boilers, and water heaters (**De Dietrich**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, **Oertli**) equipped with the **MK3 / EHC-08 / CU-GH08 Bluetooth Low Energy** interface communicating over the proprietary **DirectChannel / RUB v2.01 (CANopen over BLE)** protocol.

---

## 1. Mandatory Rules for Every Change

1. **Safe Parameter Operations:**
   - Outside of the session unlock handshake, `bdr_ble` only sends `ReadRequest` (`Function = 0x00`), `GetListRequest` (`Function = 0x08`), and user-initiated `WriteConfirmedRequest` (`Function = 0x04` from `number` / `select` entities).
   - Never send factory reset commands (`0x4000`) or firmware update frames (`Function = 0x0A..0x13`).
2. **Always Update `CHANGELOG.md`:**
   - **Every** user-visible, protocol, architectural, bug-fix, or configuration change **must** include a corresponding entry under the `## [Unreleased]` section of [`CHANGELOG.md`](./CHANGELOG.md) (`Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, or `Security`) in the same commit or branch before finishing the task.
   - When cutting a new Git tag / GitHub Release (`vX.Y.Z`), move the contents of `## [Unreleased]` into `## [X.Y.Z] - YYYY-MM-DD`, update comparison links at the bottom of `CHANGELOG.md`, and leave an empty `## [Unreleased]` section at the top.
3. **Keep Documentation, Configurator & Example YAML in Sync:**
   - When adding, renaming, or modifying configuration options, entities, or catalog fields:
     - Update [`README.md`](./README.md)
     - Update [`example.yaml`](./example.yaml)
     - Update [`docs/PROTOCOL_SPECIFICATION.md`](./docs/PROTOCOL_SPECIFICATION.md) and [`docs/configurator.html`](./docs/configurator.html)
   - **Never place `.html` or documentation files inside `components/bdr_ble/`**: ESPHome bundles the entire `components/bdr_ble/` directory into remote build archives. Keep `configurator.html` and `PROTOCOL_SPECIFICATION.md` strictly inside `docs/`.
4. **Language Conventions:**
   - All repository files, code comments, log messages, commit messages, pull requests, and release notes **must be written in English**.
5. **Commit Conventions:**
   - Follow **Conventional Commits** (`feat: ...`, `fix: ...`, `refactor: ...`, `docs: ...`, `chore: ...`).
   - Develop and test changes on the `dev` branch before rebasing/merging into `main` for tagged releases (`@vX.Y.Z`).

---

## 2. Repository Layout & Architecture

```text
components/bdr_ble/
├── __init__.py                         # Hub schema (ble_client_id, pin, node_id, auth_level)
├── obd.py                              # Compile-time OBD dictionary resolver & ESPHome metadata inference
├── obd_catalog.json                    # Compact JSON catalog (3,092 datapoints, 204 enums, FGs, models)
├── bdr_protocol.h / .cpp               # Protocol constants, CRC16, 32-round TEA cipher, and value codecs
├── bdr_ble.h / .cpp                    # BdrBleComponent lifecycle, BLE GATT/GAP state machine, TX & scheduler
├── bdr_ble_parser.cpp                  # RX DirectChannel reassembly, RUB auth, 0x502B snapshot & state dispatch
├── binary_sensor/
│   └── __init__.py                     # Boolean/two-state status binary_sensor platform (single & grouped syntax)
├── number/
│   ├── __init__.py                     # Writable numeric parameter platform (single & grouped syntax)
│   └── bdr_ble_number.h / .cpp         # BdrBleNumber entity class (Parented<BdrBleComponent>)
├── select/
│   ├── __init__.py                     # Writable enum parameter platform (single & grouped syntax)
│   └── bdr_ble_select.h / .cpp         # BdrBleSelect entity class (Parented<BdrBleComponent>)
├── sensor/
│   └── __init__.py                     # Numeric/Enum sensor platform (single & grouped batch syntax)
└── text_sensor/
    └── __init__.py                     # Status & Enum/VisibleString/TimeOfDay text_sensor platform
docs/
├── PROTOCOL_SPECIFICATION.md           # Full BLE GATT, Service 3, RUB v2.01, 0x08 GetList & 0x502B spec
└── configurator.html                   # Standalone HTML+JS ESPHome YAML constructor (model, FG & snapshot filters)
```

### Zero Firmware Bloat Architecture
- `obd_catalog.json` is loaded **only on the build host in Python (`obd.py`)** during `esphome compile`.
- `sensor/__init__.py` and `text_sensor/__init__.py` resolve each configured `parameter` at compile time and emit lightweight `cg.add(hub.register_sensor(...))` calls containing only `(index, sub_index, data_type, scale, interval_ms, node_id)`.
- For `Enum` text sensors, only the enum entries for the specific configured datapoint are emitted via `cg.add(hub.register_enum_translation(...))`.

---

## 3. BDR Thermea BLE Protocol Essentials (Do Not Break!)

1. **BLE GAP Bonding & Notification Subscription Order:**
   - `esp32_ble:` must use `io_capability: keyboard_only`.
   - In `ESP_GATTC_OPEN_EVT`, call `this->parent()->pair()` and reply to `ESP_GAP_BLE_PASSKEY_REQ_EVT` with `this->pin_`.
   - In `ESP_GATTC_SEARCH_CMPL_EVT`, call `esp_ble_gattc_register_for_notify` for handles `{33, 38, 20, 23, 27}` **before** setting `this->node_state = ClientState::ESTABLISHED`.
   - Wait for `ESP_GAP_BLE_AUTH_CMPL_EVT` before writing `0x0001` to CCCD handles `{34, 39, 21, 24, 28}`.
2. **Zero-Length GATT Write on Handle `31` (Service 3 Trigger):**
   - ESP-IDF's `esp_ble_gattc_write_char` rejects `value_len == 0` with `ESP_ERR_INVALID_ARG`.
   - Always trigger Service 3 challenge using Bluedroid's `BTA_GATTC_WriteCharValue(bta_conn_id, 31, 2, 0, &dummy, 0)` where `bta_conn_id = (conn_id << 8) | (gattc_if & 0xFF)`.
3. **32-Round TEA Cipher (`0x15EFA43F`):**
   - **Block input (`v0, v1`)**: `[auth_level, auth_level]` (`[4, 4]`).
   - **128-bit Key (`k0..k3`)**: `[0x15EFA43F, id1, id2, auth_level]`.
   - **Service 3 (Handle `33` -> Handle `36`)**: `id1, id2` are unpacked **Little-Endian** from the 8-byte challenge, and the 9-byte response is `[auth_level, v0_LE(4B), v1_LE(4B)]`.
   - **RUB v2.01 (`02 01 00 02 01 03` -> `02 01 00 02 02 03`)**: `id1, id2` are unpacked **Big-Endian** from `payload[6..13]`, and the 8-byte key is packed **Big-Endian** `[v0_BE(4B), v1_BE(4B)]`.
4. **DirectChannel Framing & CRC-16/MODBUS (Handles `18` / `20`):**
   - Inner frame: `[0x01, ...payload..., crc_hi, crc_lo]` where `crc` is reflected **CRC-16/MODBUS** (`init = 0xFFFF`, `poly = 0xA001`) stored Big-Endian (`crc >> 8`, `crc & 0xFF`).
   - BLE segmentation prefix byte: `0x00, 0x01, ...` for non-final chunks and **`0xFF`** for the final (or single) chunk.
5. **Appliance Snapshot (`0x502B:01..15`) & Batch Polling (`0x08` / `0x09`):**
   - After `RUB v2.01` auth succeeds, `STATE_READING_SNAPSHOT` reads `0x502B` (`SubIndex 1..15`), parses 15-byte menu descriptor entries (`count` at `data[0..1]`, `index` LE at `off+2..3`, `sub_index` at `off+12`), and logs `BDR_SNAPSHOT:XXXX:YY,...`.
   - In `STATE_READY`, groups with `> 1` supported items use `GetListRequest` (`Function = 0x08`), and any item returning `status != 0x01` in `GetListResponse` (`Function = 0x09`) or `SDO Abort (0x02)` is marked `unsupported = true` (`Auto-Skip`).

---

## 4. Validation & Testing Checklist

Before committing changes:
1. Validate configuration and compile with ESPHome (`validate_config` / `compile`) to verify:
   - Python `CONFIG_SCHEMA`, `obd.py` parameter resolution, and `to_code` generation succeed.
   - C++ code compiles cleanly under ESP-IDF (`esp32` / `esp32c6`).
2. Verify `CHANGELOG.md` (`## [Unreleased]`), `README.md`, `docs/PROTOCOL_SPECIFICATION.md`, `docs/configurator.html`, and `example.yaml` are in sync.
