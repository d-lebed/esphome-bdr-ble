# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **On-Demand CAN Node & `0x502B` Discovery (`button` Platform & `bdr_ble.discover` Action):**
  - Added `components/bdr_ble/button/` (`button: platform: bdr_ble`) and the `bdr_ble.discover` ESPHome automation action (plus `BdrBleComponent::start_discovery()`).
  - Triggering discovery probes CANopen `0x1F85:00` (node count `N`) and `0x2001:02` (`parApDeviceInfoType`) on the default node and slave nodes `3..min(N, 16)`, logs all responding CAN node IDs with hardware type codes, automatically reads `0x502B` across **every** detected node, and outputs chunked `BDR_SNAPSHOT:` / `BDR_SNAPSHOT[N<id>]:` log lines on demand instead of dumping on every BLE reconnect.
- **Official-Sequence `0x502C` → `0x502D` Menu Structure Selection & Multi-Node `0x502B` Limits:**
  - Before reading `0x502B` on each CAN node, `bdr_ble` now reads `0x502C:00` (`varApSupportedMenuStructure`), verifies V2 (`0x02`) support, and writes `0x502D:00 = 0x02` (`varApUsedMenuStructureVersion`), preventing `0x08000000` (General Error) rejections on cold-rebooted controllers (such as Confida 40).
  - Filters `0x502B` sub-indices by configured `auth_level` (`((sub - 1) % 5) + 1 <= auth_level`) to avoid wasted NACK round-trips, retries timed-out or `0x08000000`-busy `0x502B` blocks once, and loads `0x502B` metadata quietly once per boot across all configured `node_id`s (with subsequent BLE reconnects entering `STATE_READY` immediately).
- **NVS Flash Persistence for Learned `0x502B` Limits (`number` & `select`):**
  - `BdrBleNumber` and `BdrBleSelect` now persist learned `0x502B` hardware limits (`min`/`max`/`step` for `number`, allowed `[raw_min..raw_max]` bounds for `select`) to ESP32 NVS flash (`ESPPreferenceObject`, written only when values change) and restore them in `setup()` (`setup_priority::DATA`) before Home Assistant connects to the Native API.
  - Added optional `device_limits: true/false` (default `true`) per entity on `number:` and `select:` so users can opt out when enforcing custom manual YAML bounds or full enum option lists.
- **Local Range Guard in `BdrBleNumber::control()`:**
  - Rejects non-finite and out-of-range numeric writes locally against `0x502B` hardware bounds (or configured YAML bounds when `device_limits: false`) and immediately restores the current valid state in Home Assistant without sending invalid writes over BLE.

### Changed
- **Deprecated `log_snapshot:` Hub Option:**
  - `BDR_SNAPSHOT` output is now triggered on demand via `button: platform: bdr_ble` or the `bdr_ble.discover` action instead of automatically on every BLE reconnect (`log_snapshot:` remains accepted with a deprecation warning for backward compatibility).
- **Encapsulated Entity Trait Updates & Subpackage Build Guards:**
  - Moved `0x502B` trait mutation and dynamic `select` option filtering out of `BdrDatapointRegistry` into `BdrBleNumber::apply_device_limits()` and `BdrBleSelect::apply_device_bounds()`.
  - Added component-specific `USE_BDR_BLE_NUMBER` and `USE_BDR_BLE_SELECT` compile defines so configuring an unrelated ESPHome `number:` or `select:` platform alongside `bdr_ble` never breaks header inclusion.

### Fixed
- **Active BLE Link Recovery, Dynamic RX Liveness Watchdog & PIN-Less GATT Discovery Timeout:**
  - Added clean GATT disconnect recovery (`STATE_CLOSING` via `BLEClientBase::disconnect()` with a 5 s fallback) so stalled `STATE_AUTH_FAILED` sessions, repeated multi-fragment TX stalls, silent zombie BLE links, and consecutive timeouts persisting past RUB re-authentication force a clean BLE reconnect instead of looping indefinitely on a wedged GATT connection.
  - Added a dynamic `STATE_READY` RX liveness watchdog (`max(180 s, 4 * shortest_poll_interval + 30 s)`, automatically disabled when no polled datapoints are active) to detect silent links without false positives on slow-polling or discovery-only configs.
  - Prevented GATT service discovery timeouts in PIN-less `GTW-IoT` mode (or after encryption has already succeeded) from triggering `handle_pairing_failure_()` and its 90 s BLE pairing lockout.
- **4 kB DirectChannel RX Reassembly Limit:**
  - Raised `DirectChannelTransport::MAX_RX_REASSEMBLY_LEN` from `2048` to `4096` bytes so large `0x502B` descriptor blocks (>2 kB, e.g. ~2.3 kB / 148 entries on Node 4) are no longer dropped during reassembly.
- **Un-Sign-Extended Negative Minimums in `0x502B` Descriptors:**
  - Properly sign-extends 8-bit (`I8`) and 16-bit (`I16`) signed bounds packed without sign-extension in the 32-bit `0x502B` descriptor field (e.g. `0x0000F63C` for `-2500` on Node 4 / SCB extension boards) instead of silently dropping those hardware bounds.

---

## [1.5.1] - 2026-11-22

### Fixed
- **GTW-IoT / DirectChannel `GetListRequest` & Session Stability (Issue #2):**
  - Removed unsupported CAN-IP v2.00 per-node channel upgrade (`02 00 00 02 04 00`) frames over BLE DirectChannel (`RUB v2.01` auth with `0xFF` already unlocks all nodes; sending `0x04` on `STATE_READY` entry timed out and could wedge `GTW-IoT` hubs until reboot).
  - Removed the invalid `GetListRequest` `v2.01` (`02 01 00 00 08 03`) fallback header; `GetListRequest (0x08)` now always uses the canonical `v2.00` (`02 00 00 00 08 00`) header and retries once before falling back to single-read polling.
  - Waits for chunked `BDR_SNAPSHOT` logging to finish before starting `STATE_READY` polling so snapshot output remains contiguous and gateways get a clean pause after `0x502B:15`.
  - Deferred post-write readbacks (`WritePosAck`/`WriteNegAck`/write timeout) and pending `RUB_AUTH_REQ` transmissions to `loop()` so fast BLE notifications arriving before `ESP_GATTC_WRITE_CHAR_EVT` never fail with `DirectChannel TX rejected (tx_in_progress=1)`.
  - Verified the descriptor handle in `ESP_GATTC_WRITE_DESCR_EVT` during `STATE_SUBSCRIBING` so duplicate CCCD writes cannot skip the next characteristic subscription.
  - Required strict `min < max` when adopting `0x502B` descriptor bounds so read-only signals/counters with `0..0` bounds never reject non-zero values, and added explicit warning logs on `ReadRequest` and confirmed `GetListRequest` timeouts.
- **Dynamic `0x502B` Select Option Filtering & Range Guard:**
  - Dynamically filters `select:` entity options from each appliance's live `0x502B` `min..max` descriptors at runtime (just like `number:` limits), avoiding hardcoded per-parameter enum subsets across different controllers while excluding non-writable runtime states (such as `Temporary` on `CP320` / `DP200`).
  - Added a runtime `0x502B` hardware range guard in `BdrBleSelect::control()` to reject out-of-range enum writes with a clear warning and restore the current valid state before transmitting.

---

## [1.5.0] - 2026-11-20

### Added
- **Chunked `BDR_SNAPSHOT` Logging & `log_snapshot` Option:**
  - Added `log_snapshot: true/false` (default `true`) on the `bdr_ble:` hub. Snapshot log lines (`BDR_SNAPSHOT:...`) are now emitted at most 2 lines per `loop()` tick on each BLE connection, eliminating the `>300 ms` `ble_client` main-loop blocking warning.
- **Raw Unregistered Hex Datapoint Type Override (`type:`):**
  - Added optional `type:` (`U8`, `I8`, `U16`, `I16`, `U32`, `I32`, `Enum`, `VisibleString`, `OctetString`, `TimeOfDay`) on platform items to allow polling/testing newly discovered `BDR_SNAPSHOT` hex indices not yet present in `obd_catalog.json`.
- **Multi-Node CAN Channel Authorization Upgrade (`FUNC = 0x04`):**
  - Automatically sends CAN-IP per-node channel upgrade (`02 00 00 02 04 00 <node_id> <auth_level>`) when a secondary `node_id` returns `SDO_ABORT_UNSUPPORTED_ACCESS (0x06010000)`.
- **Disconnect Staleness Reporting (`NAN`):**
  - Publishes `NAN` (`unavailable`) to numeric `sensor` entities if the BLE session remains disconnected for more than 90 seconds or enters pairing lockout backoff.
- **Host C++20 Unit Test Suite (`tests/test_bdr_protocol.cpp`):**
  - Added standalone host unit tests verifying CRC-16/MODBUS framing, 32-round TEA test vectors, numeric/bounds codecs, UTF-8 safe truncation, 6-byte CiA 301 `TimeOfDay` decoding, SDO abort classification, and `DirectChannelTransport` multi-segment framing.

### Changed
- **Modular 4-Layer C++ & Shared Python Schema Architecture:**
  - Split `components/bdr_ble/` into `bdr_protocol.h/.cpp` (pure codecs/crypto), `bdr_transport.h/.cpp` (`DirectChannelTransport` TX/RX segmentation & CRC), `bdr_registry.h/.cpp` (`BdrDatapointRegistry` global deduplication, `0x502B` descriptors, Flash enum lookup, and entity dispatch), `bdr_ble.h/.cpp` (BLE GATT/GAP lifecycle, Service 3 + RUB auth, and `ActiveRequest` transaction orchestration), and `schema_helpers.py`.
- **Zero-Heap Flash `EnumEntry` Tables:**
  - Replaced heap-allocated `std::map<uint64_t, std::string>` enum translation maps and `BdrBleSelect` option maps with `static constexpr EnumEntry` arrays in Flash (`.rodata`), emitted once per unique OBD enum table.
- **Global Cross-Group Datapoint Deduplication & Byte-Budgeted `GetList`:**
  - Deduplicates `(node_id, index, sub_index)` across all poll groups (promoting shared parameters to the fastest requested `update_interval` and preserving non-trivial `scale` factors) and caps `GetListRequest (0x08)` batches at both 16 items and `<= 240` estimated RX bytes.

### Fixed
- **`GetListResponse (0x09)` Request Correlation & Smart SDO Abort Classification:**
  - Validates incoming `GetListResponse` frames against the active `ActiveRequest` count and first datapoint, enforces a 500 ms post-timeout cooldown, and distinguishes permanent unsupported SDO aborts (`0x06020000`, `0x06090011`, `0x06010001`) from `GetList`-incompatible aborts (`0x06040043`, `0x06070010`) and transient CAN timeouts (`0x05040000`, `0x08000000`) so transient timeouts never permanently degrade batch polling into `single_read_only`.
- **Hardware `0x502B` Wire-Size & Min/Max Bounds Validation:**
  - Removed the hardcoded `0x30000000` `U32`/`I32` range heuristic in `decode_numeric_datapoint()` and replaced it with exact `0x502B` `dp_len` wire-length checking, hardware `[raw_min, raw_max]` range validation, and a single-cycle jump guard for `total_increasing` counters.
- **Zero `REG_FOR_NOTIFY after services released` Warnings on Connect:**
  - Holds `ClientState::CONNECTED` during GATT discovery and drives notification registration and CCCD descriptor writes deterministically via `ESP_GATTC_REG_FOR_NOTIFY_EVT` and `ESP_GATTC_WRITE_DESCR_EVT` after link encryption before transitioning to `ClientState::ESTABLISHED`.
- **Authoritative Write State Publishing & RUB Session Recovery:**
  - Removed optimistic `publish_state()` calls prior to write confirmation in `BdrBleNumber`, `BdrBleSelect`, and `BdrBleText`; writes can now be queued during initial connection/snapshot and publish state only upon authoritative hardware readback.
  - Automatically re-authenticates the `RUB v2.01` session when a previously working datapoint returns `SDO_ABORT_UNSUPPORTED_ACCESS (0x06010000)` instead of permanently disabling the datapoint.
- **`GTW-IoT` Service 3 Skip, 2-Byte `Select` Writes, 6-Byte `TimeOfDay` & UTF-8 Safe Truncation:**
  - Skips Service 3 unlock immediately when Service 3 UUIDs are absent in GATT DB (e.g., `GTW-IoT`).
  - Serializes 1-byte or 2-byte Little-Endian enum payloads in `BdrBleSelect::control()` according to parameter wire size.
  - Decodes 6-byte CANopen CiA 301 `TIME_OF_DAY` (`ms_since_midnight` + `days_since_1984`) for `TimeOfDay` parameters (`0x3421..0x3423`).
  - Truncates `BdrBleText` writes on valid UTF-8 codepoint boundaries (`utf8_safe_truncate_len`) and fixes `NameError` in `obd.py:resolve_parameter()` on unknown hex indices.
  - Marked `parZoneFriendlyNameShort` (`0x3410`) as read-only (`"rw": false`) in `obd_catalog.json` and `docs/configurator.html` (with a backward-compatible warning in `text:`) since `EHC-08` treats `0x3410` as a read-only hardware terminal identifier (`CIRCA`/`CIRCB`).

---

## [1.4.0] - 2026-11-19

### Added
- **Writable String Platform (`text`):**
  - Added `components/bdr_ble/text/` platform (`esphome::text::Text`) for reading and editing writable `VisibleString` parameters in Home Assistant — including zone friendly names (`parZoneFriendlyName` / `0x340F`), short zone names (`parZoneFriendlyNameShort` / `0x3410`), user activity names (`parZoneFriendlyNameUserActivity` / `0x340D:01..06`), cooling setpoint names (`parZoneFriendlyNameCoolingSetpoint` / `0x3411:01..06`), appliance name (`parApApplianceName` / `0x3047`), and installer name/phone (`AP089` / `AP090`).
  - Added exact `len` character limits from `BDR_OBD.xml` to all 51 `VisibleString` entries in `obd_catalog.json` and automatically formats fixed-length null-padded (`0x00`) SDO `WriteConfirmedRequest` frames with automatic readback on `WRITE_POS_ACK`.
  - Updated `docs/configurator.html` to expose writable `VisibleString` parameters in the **Writable** tab with `text (rw)` / `text_sensor (ro)` selection, character limit badges (`max Nch`), and `text:` YAML generation.

### Fixed
- **ESPHome 2026.11 `EntityBase::get_name()` Deprecation Warning:**
  - Replaced deprecated `EntityBase::get_name()` calls in `mark_datapoint_unsupported_()` with a backward-compatible `get_log_name()` SFINAE helper, eliminating `-Wdeprecated-declarations` warnings on ESPHome `2026.11.0+`.

---

## [1.3.1] - 2026-11-19

### Added
- **Human-Readable SDO Abort Code Diagnostics (`sdo_abort_code_str`):**
  - Logs the exact 32-bit CANopen SDO abort code and description (e.g., `0x06020000: Object does not exist in object dictionary`, `0x06090011: Sub-index does not exist`, `0x06010000: Unsupported access to object (check auth_level)`) in `GetListResponse (0x09)`, `SingleReadNegAck (0x02)`, and `WriteNegAck (0x06)`.

### Fixed
- **`0x502E` Boolean Status Flag `0xFF` Normalization:**
  - Normalizes `0xFF` (`255`) raw values to `1` (`Yes` / `On`) for boolean enum status flags (such as `0x502E:01..07`) so `sensor`, `text_sensor`, and `binary_sensor` entities all report consistent states when supported by the appliance.

---

## [1.3.0] - 2026-11-18

### Added
- **7-Language Localization (`EN`, `DE`, `FR`, `NL`, `PL`, `IT`, `ES`):**
  - Added localized README files (`README.de.md`, `README.fr.md`, `README.nl.md`, `README.pl.md`, `README.it.md`, `README.es.md`).
  - Added external UI translation bundles (`docs/i18n/index_i18n.js` and `docs/i18n/configurator_ui_i18n.js`) and on-demand catalog translation dictionaries (`docs/i18n/{de,fr,nl,pl,it,es}.js`) covering all 3,092 datapoints, 233 Function Groups, and 204 Enum tables without increasing ESPHome firmware size.
  - Added a flag dropdown language selector to `docs/index.html` and `docs/configurator.html` with `localStorage` persistence and localized YAML `name:` generation.
- **Interactive Scrollspy & Collapsible Submenus (`docs/index.html`):**
  - Added real-time scrollspy highlighting for the sidebar Table of Contents and top navigation bar, automatically expanding subsection (`<h3>`) submenus for the active section.
- **Gateway Stale Bond Eviction & 60s Lockout Backoff:**
  - Automatically detects and clears stale local BLE bonds (`esp_ble_remove_bond_device`) when the boiler's `GTW-Blue` gateway evicts the oldest of its 3 pairing slots.
  - Added exponential pairing backoff (`90s → 180s → 300s → 600s`) that pauses `ble_client` reconnect attempts so the gateway's 60-second `Repeated Attempts (0x56)` lockout window can expire cleanly.

### Fixed
- **Multi-Fragment `GetListRequest (0x08)` TX Pacing & Automatic Single-Read Fallback:**
  - Fixed `DirectChannel` multi-fragment TX to pace 20-byte chunks (`1B seq + 19B payload`) via `ESP_GATTC_WRITE_CHAR_EVT` and start with the canonical `v2.00` header (`02 00 00 00 08 00 <count>`), enabling fast 16-item batch polling out of the box.
  - Any scalar datapoint returning `status != 0x01` inside a batch `GetListResponse (0x09)` now automatically falls back to individual `ReadRequest (0x00)` polling instead of being disabled.

---

## [1.2.0] - 2026-11-18

### Added
- **Binary Sensor Platform (`binary_sensor`):**
  - Added `components/bdr_ble/binary_sensor/` platform for polling boolean / two-state status signals (`OffOn`, `InactiveActive`, `YesNo`, etc., such as `HM012`, `HM013`, `AM015`, `HM008`, `DM000`) as `binary_sensor` entities (`0 = OFF`, `!= 0 = ON`).
  - Updated `docs/configurator.html` to default read-only boolean-enum signals to `binary_sensor` and generate `binary_sensor:` YAML blocks.
- **Snapshot Array Element Expansion & Breakdown (`docs/configurator.html`):**
  - Dynamically synthesizes rows for array sub-elements (`SubIndex >= 2`, such as `CP080 [2]..[6]`, `CP140 [2]..[6]`, `AP111 [2]`) reported in `BDR_SNAPSHOT:` logs and emits `sub_index:` in the generated YAML.
  - Displays an exact breakdown of configurable items vs. hidden composite `Struct` schedule tables in the snapshot status badge.

---

## [1.1.0] - 2026-11-18

### Added
- **Configurator Access Level Filter, View Tabs & `localStorage` Persistence (`docs/configurator.html`):**
  - Added `auth_level` (`L1 End User` .. `L4 Lab/OEM`) safety filter, row badges, and automatic minimum required `auth_level` calculation in the YAML generator.
  - Added segmented view tabs (`All Matching`, `Selected Only`, `Writable`, `Read-Only`) for easier selection review.
  - Added automatic `localStorage` state persistence across page reloads and a `Reset Defaults` button.
  - Enriched `obd_catalog.json` with `lvl` (`1..4`) metadata from `0x502B:01..15` and added `@<level>` tags to `BDR_SNAPSHOT:` log output.
- **GitHub Pages Documentation & `auth_level` Reference:**
  - Published the interactive ESPHome YAML Constructor and SEO-optimized documentation site on GitHub Pages (`https://d-lebed.github.io/esphome-bdr-ble/`).
  - Documented the 5 hardware access levels (`auth_level: 1..5`) and their mapping to `0x502B` (`SubIndex 1..15`).

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

[Unreleased]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.5.1...HEAD
[1.5.1]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.5.0...v1.5.1
[1.5.0]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.4.0...v1.5.0
[1.4.0]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.3.1...v1.4.0
[1.3.1]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/d-lebed/esphome-bdr-ble/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/d-lebed/esphome-bdr-ble/compare/v0.3.0...v1.0.0
[0.3.0]: https://github.com/d-lebed/esphome-bdr-ble/releases/tag/v0.3.0
