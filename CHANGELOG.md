# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Configurator Access Level Filter, View Tabs & `localStorage` Persistence (`docs/configurator.html`):**
  - Added `auth_level` (`L1 End User` .. `L4 Lab/OEM`) safety filter, row badges, and automatic minimum required `auth_level` calculation in the YAML generator.
  - Added segmented view tabs (`All Matching`, `Selected Only`, `Writable`, `Read-Only`) for easier selection review.
  - Added automatic `localStorage` state persistence across page reloads and a `Reset Defaults` button.
  - Enriched `obd_catalog.json` with `lvl` (`1..4`) metadata from `0x502B:01..15` and added `@<level>` tags to `BDR_SNAPSHOT:` log output.
- Published the interactive ESPHome YAML Constructor and SEO-optimized documentation site on GitHub Pages (`https://d-lebed.github.io/esphome-bdr-ble/`).

---

## [1.0.0] - 2026-11-18

### Added
- **Parameter Writing Support (`number` & `select` platforms):**
  - Added `number` platform (`components/bdr_ble/number/`) for writable numeric parameters (`U8`, `I8`, `U16`, `I16`, `U32`, `I32`) with automatic unit/step/range inference and dynamic `min_value`/`max_value`/`step` updates from the boiler's `0x502B:01..05` menu descriptors.
  - Added `select` platform (`components/bdr_ble/select/`) for writable `Enum` parameters with compile-time option population from `obd_catalog.json`.
  - Implemented `WriteConfirmedRequest` (`Function = 0x04`) queue in `BdrBleComponent` with automatic read-back on `WritePosAck` (`Function = 0x05`) and state revert on `WriteNegAck` (`Function = 0x06`).
  - Updated `docs/configurator.html` to default writable `Parameter` datapoints to `number (rw)` / `select (rw)` and generate `number:` and `select:` YAML blocks.

### Changed
- **Modular C++ Architecture:**
  - Extracted pure protocol constants, CRC-16/MODBUS, 32-round TEA cipher, and datapoint encode/decode helpers into `bdr_protocol.h` and `bdr_protocol.cpp`.
  - Split `BdrBleComponent` into `bdr_ble.cpp` (lifecycle, BLE GATT/GAP state machine, TX framing, and polling/write scheduler) and `bdr_ble_parser.cpp` (RX notification reassembly, RUB authentication, `0x502B` snapshot discovery, `Auto-Skip`, and entity state dispatching).
  - Extracted `BdrBleNumber` and `BdrBleSelect` into dedicated platform submodules (`number/bdr_ble_number.h/.cpp` and `select/bdr_ble_select.h/.cpp`) using `Parented<BdrBleComponent>`.
  - Tracked `waiting_for_get_list_` separately from single-item reads/writes so a single write timeout never falsely downgrades `GetListRequest` batch polling.

### Fixed
- **State Machine, Transport & Write Hardening:**
  - Added timeout/retry handling for `STATE_SVC3_WAIT_RESULT` and `STATE_RUB_WAIT_AUTH_ACK`, plus `STATE_AUTH_FAILED` cooldown recovery and a consecutive-timeout session watchdog (`MAX_CONSECUTIVE_TIMEOUTS = 10`).
  - Added `InFlightType` request/response correlation (`IN_FLIGHT_SNAPSHOT`, `IN_FLIGHT_READ`, `IN_FLIGHT_WRITE`, `IN_FLIGHT_GET_LIST`) and state-guarded BLE notification handlers.
  - Hardened parameter writes: reject writes when not in `STATE_READY`, cap and clear `write_queue_` on disconnect, validate numeric range and sentinel bounds in `encode_numeric_datapoint()`, enforce `"rw": true` at compile time in `number:` and `select:`, and automatically read back parameter state if a write confirmation times out.
  - Added `HANDLE_SVC2_CH2` (`27`) Error Management notification handling (automatically re-unlocking on `[3, 0, 0, 4]` `"BLE services locked"`), RX segment sequence and staleness checks, SDO abort code filtering before `Auto-Skip`, and `OctetString` hex formatting for `text_sensor`.
- **CANopen Array Datapoint Default `SubIndex` (`IsArray="True"`):** Fixed default `sub_index` in `obd_catalog.json` and `docs/configurator.html` for all 916 `IsArray="True"` parameters and signals (such as `DP070`, `DP080`, `DP200`, `CP000`–`CP780`, `CM030`–`CM260`) from `0x00` (which in CANopen is the 1-byte array length header) to `0x01` (Zone 1 / Primary instance, matching the `0x502B` appliance descriptors).
- **Interactive YAML Constructor (`docs/configurator.html`):**
  - Display all `Enum` values and `FunctionGroup` badges in full without truncating after 5 items (`...`).
  - Improved responsive two-pane grid layout (`55% / 45%` on standard monitors, `52% / 48%` on wide monitors, and vertical stacking on narrow screens `< 1024px`) with `pre-wrap` YAML rendering so wide monitors no longer require horizontal scrolling in the YAML pane.

---

## [0.3.0] - 2026-11-18

### Added
- **Initial Release of `bdr_ble` ESPHome External Component:**
  - Full BLE client implementation for BDR Thermea appliances equipped with the **MK3 / EHC-08 / CU-GH08** Bluetooth Low Energy module (**De Dietrich Strateo / Alezio / Evodens / Naneo / Modulens / Kaliko**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, **Oertli**).
  - Automatic BLE GAP Passkey Entry pairing (`pin:` configuration option in `bdr_ble:`) and two-stage 32-round TEA (`0x15EFA43F`) unlock handshake:
    - Stage 1: GATT Service 3 (`6a37b97e-779d-457f-8182-edf334edd01f`) Little-Endian TEA unlock using Bluedroid zero-length trigger write on handle `31`.
    - Stage 2: DirectChannel (`f8fc98e4-5919-4a5c-852e-dfe04ad383c0`) `RUB v2.01` Big-Endian TEA unlock (`auth_level: 4`).
  - Multi-segment `DirectChannel` TX/RX framing with reflected CRC-16/MODBUS (`init = 0xFFFF`, `poly = 0xA001`) verification and dynamic ATT MTU chunking.
- **Compile-Time CANopen Object Dictionary (`obd_catalog.json` & `obd.py`):**
  - Embedded catalog of **3,092 datapoints**, **204 enum tables**, **195 Function Groups (`FG0001..FGFFF0`)**, English labels (`BDR_OBD_Translations_en-GB`), and **14 controller board profiles** (`EHC-08`, `EHC-04`, `EHC-05`, `EHC-07/12/15`, `EHC-09/10/11`, `EHC-14/16`, `CU-08`, `CU-09`, `CU-15/16/17/21`, `CU-06/13/20/22`, `CU-OH01/02/04`, `CU-HW01`, `CU-EB`, `EEC-01/SCB`).
  - Resolve sensors by short menu code (`AM027`, `DM001`, `AC005`), OBD signal name (`varApTotalEnergyDelivery`), or hex index (`0x5044:00`) with automatic inference of `unit_of_measurement`, `accuracy_decimals`, `device_class`, `state_class`, `icon`, `scale`, and default `name`.
  - Zero firmware bloat: only the datapoints explicitly listed in YAML are compiled into the ESP32 binary.
- **Batch `GetListRequest (0x08)` Polling & Automatic Unsupported Datapoint Pruning (`Auto-Skip`):**
  - Automatically groups datapoints sharing the same `update_interval` into `GetListRequest` (`Function = 0x08`) batch requests (up to 16 items per batch).
  - Automatically disables polling (`Auto-Skip`) for any datapoint reported as unsupported by the boiler (`status != 0x01` in `GetListResponse` or `Function = 0x02` SDO Abort) with a one-time warning in the ESPHome log.
- **Dynamic Appliance Snapshot (`0x502B:01..15`) & Interactive HTML YAML Constructor (`docs/configurator.html`):**
  - Queries `0x502B:01..15` (`varApMenuDescriptor`) upon connection, parses 15-byte menu descriptors, and logs `BDR_SNAPSHOT:XXXX:YY,...` lines listing all parameters, signals, and counters physically active on the connected boiler.
  - Self-contained [`docs/configurator.html`](./docs/configurator.html) constructor with controller board model filter, `BDR_OBD` Function Group filter, live `BDR_SNAPSHOT:` log paste filter, subsystem/physical-type filters, and instant ESPHome YAML generation.
- **Comprehensive Protocol Specification (`docs/PROTOCOL_SPECIFICATION.md`):**
  - Complete documentation of the GATT table, TEA cipher variants, `DirectChannel` segmentation, `RUB v2.01` single and batch `GetList` framing, `0x502B` menu descriptor layout.

[Unreleased]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/d-lebed/esphome-bdr-ble/compare/v0.3.0...v1.0.0
[0.3.0]: https://github.com/d-lebed/esphome-bdr-ble/releases/tag/v0.3.0
