# ESPHome BDR Thermea / De Dietrich / Baxi / Remeha BLE Component (`bdr_ble`)

[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-38bdf8?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/)
[![Interactive YAML Configurator](https://img.shields.io/badge/configurator-Interactive%20YAML%20Builder-22c55e?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html)
[![Release](https://img.shields.io/github/v/release/d-lebed/esphome-bdr-ble?style=flat-square)](https://github.com/d-lebed/esphome-bdr-ble/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?style=flat-square&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/lebiadz)

🌐 **Language:** **English** | [Deutsch](./README.de.md) | [Français](./README.fr.md) | [Nederlands](./README.nl.md) | [Polski](./README.pl.md) | [Italiano](./README.it.md) | [Español](./README.es.md)

An **ESPHome external component (`bdr_ble`)** for monitoring and controlling **BDR Thermea Group** heat pumps, condensing boilers, and thermodynamic water heaters (**De Dietrich**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, **Oertli**) in **Home Assistant** over **Bluetooth Low Energy (BLE)**.

Communicates directly with the appliance's built-in **MK3 / EHC-08 / CU-GH08 Bluetooth interface** (the same BLE interface used by the official *De Dietrich PRO*, *Baxi Service Tool*, and *Remeha Smart Service* mobile apps) using any standard **ESP32** (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **no proprietary `GTW-08` gateway or wiring required**, and your existing **OpenTherm / R-Bus** room thermostat remains untouched.

---

## 📖 Documentation & Interactive YAML Configurator

- **📘 [Full Documentation & Setup Guide (GitHub Pages)](https://d-lebed.github.io/esphome-bdr-ble/)**
  - [Supported Boilers & Heat Pumps (`EHC-*`, `CU-*`, `SCB-*`)](https://d-lebed.github.io/esphome-bdr-ble/#compatibility)
  - [BLE Pairing & Requirements Guide](https://d-lebed.github.io/esphome-bdr-ble/#requirements)
  - [3-Step Sensor Discovery Workflow (`BDR_SNAPSHOT`)](https://d-lebed.github.io/esphome-bdr-ble/#discovery-workflow)
  - [Configuration Reference (`sensor`, `text_sensor`, `number`, `select`)](https://d-lebed.github.io/esphome-bdr-ble/#configuration)
  - [FAQ & Troubleshooting](https://d-lebed.github.io/esphome-bdr-ble/#faq)
- **⚡ [Interactive ESPHome YAML Configurator](https://d-lebed.github.io/esphome-bdr-ble/configurator.html)**
  - Browse all **3,092 BDR Thermea OBD datapoints**, filter by controller board model (`EHC-08`, `CU-08`, etc.) or paste your live `BDR_SNAPSHOT:` log from ESPHome to generate ready-to-copy YAML.
- **🔬 [BDR Thermea / De Dietrich BLE Protocol Specification](./docs/PROTOCOL_SPECIFICATION.md)**
- **📋 [Changelog](./CHANGELOG.md)**

---

## Highlights

- **100% Wireless over BLE:** Leaves your physical `R-Bus` / `OpenTherm` / `L-Bus` terminals completely free.
- **Dynamic GATT UUID Discovery, Passkey Bonding & `GTW-IoT` Support:**
  - Dynamically resolves all GATT characteristic and CCCD handles by 128-bit UUID on connect (working across `EHC-08`, `EHC-16`, `CU-*`, `GTW-22`, and `GTW-IoT` attribute layouts).
  - Supports both **onboard BLE (`MK3` / `GTW-22`) with 6-digit PIN Passkey bonding** and **external `GTW-IoT` hubs without PIN** (simply omit `pin:` in YAML and press the hub button).
  - Automatically recovers from stale bonds (when a 4th paired device evicts the oldest of the gateway's 3 slots) and respects the gateway's 60-second lockout window (`90s → 180s → 300s → 600s` backoff).
  - Configurable hardware access level (`auth_level: 1..4`: `1` = End User Safe, `2` = Installer Standard, `3` = Service, `4` = Lab/OEM).
- **Read & Write Support:**
  - `sensor`, `binary_sensor` & `text_sensor`: Read temperatures, water pressure, boolean status flags (`HM012`, `HM013`, `AM015`), status enums, strings, and kWh energy consumption/delivery counters.
  - `number`, `select` & `text`: Edit numeric setpoints (e.g., DHW comfort/eco temperature, flow setpoints), enum operating modes, and string labels (zone names `parZoneFriendlyName`, user activity names `parZoneFriendlyNameUserActivity`, installer info `AP089`/`AP090`) via `WriteConfirmedRequest 0x04` with automatic hardware range discovery from `0x502B`.
- **Zero Firmware Bloat & High-Speed Batching:** Resolves menu codes (`AM027`, `DM001`, `AC005`, `DP070`) at compile time, batches requests via `GetListRequest (0x08)`, and automatically skips unsupported datapoints (`Auto-Skip`).

---

## Hardware Compatibility (Tested vs. Untested)

- **✅ Physically Tested & Verified:**
  - **De Dietrich Strateo R32** (`EHC-08` controller board, onboard BLE `01.00` with 6-digit PIN, RUB protocol `02.01`).
  - **Remeha Elga Ace All-in-One** (`EHC-15` hybrid heat pump/boiler via external **Remeha Home Hub `GTW-IoT`** in PIN-less mode).
- **⚠️ Theoretical / Untested Compatibility (Shared MK3 BLE / CAN-IP Platform):**
  - Other BDR Thermea appliances equipped with the **MK3 HMI / BLE module** or **GTW-IoT** (`EHC-04`, `EHC-05`, `EHC-07`, `EHC-09`, `EHC-14`, `EHC-16`, `CU-08` / `CU-GH08`, `CU-09`, `CU-16`, `CU-13`, `CU-OH02`, `CU-HW01`, `CU-EB`, `EEC-01 / SCB-*` across **De Dietrich**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, and **Oertli**) use the same BLE GATT service and `obd_catalog.json` dictionary in the official service app, but **have NOT been physically tested by the author**.

👉 See the **[Hardware Compatibility Table in the Documentation](https://d-lebed.github.io/esphome-bdr-ble/#compatibility)**.

---

## Quick Start

See [`example.yaml`](./example.yaml) and the **[Full Documentation](https://d-lebed.github.io/esphome-bdr-ble/#quick-start)** for complete instructions:

```yaml
external_components:
  - source: github://d-lebed/esphome-bdr-ble@v1.4.0
    components: [bdr_ble]

esp32:
  board: esp32-c6-devkitm-1
  variant: esp32c6
  framework:
    type: esp-idf

esp32_ble:
  io_capability: keyboard_only

esp32_ble_tracker:
  scan_parameters:
    active: true

ble_client:
  - mac_address: "AA:BB:CC:DD:EE:FF"
    id: boiler_ble

bdr_ble:
  id: boiler_hub
  ble_client_id: boiler_ble
  pin: !secret strateo_ble_pin  # Omit 'pin:' for external GTW-IoT hubs (no PIN); wrap leading-zero PINs in quotes ("012345") in secrets.yaml
  node_id: 1
  auth_level: 4

switch:
  - platform: ble_client
    ble_client_id: boiler_ble
    name: "Boiler BLE Connect"
    restore_mode: RESTORE_DEFAULT_ON
    entity_category: config

text_sensor:
  - platform: bdr_ble
    status:
      name: "Boiler BLE Status"
```

---

## Disclaimer & Limitation of Liability

This project is an **independent, unofficial reverse-engineering effort** created solely for educational and interoperability purposes between **BDR Thermea Group** heating appliances and **Home Assistant** / **ESPHome**.

- **Tested on One Model Only:** All testing was performed exclusively on a single **De Dietrich Strateo R32 (`EHC-08`)** heat pump owned by the author. Other models listed in the catalog are untested.
- **Risk of Voiding Warranty:** Connecting third-party software/hardware or authenticating with service/installer privileges (`auth_level: 4`) may be recorded by the appliance controller and **may void your manufacturer or installer warranty**.
- **Parameter Writing Hazard:** Writing parameters (`number:` / `select:`) modifies internal controller settings in non-volatile memory (EEPROM/Flash). Writing inappropriate values or writing parameters frequently from automations can wear out controller memory or cause unsafe/improper operation of the heating system.
- **No Warranty & Zero Liability:** This software is provided **"AS IS"**, without warranty of any kind, express or implied. **In no event shall the author or contributors be liable for any direct, indirect, incidental, special, or consequential damages** (including equipment damage, property/water/frost damage, personal injury, loss of heating/hot water, or loss of manufacturer warranty) arising out of the use of this software. **You use this software entirely at your own risk.**
- **No Affiliation & Trademarks:** Not affiliated with, endorsed by, or sponsored by **BDR Thermea Group B.V.**, **De Dietrich Thermique**, **Baxi**, **Remeha**, **August Brötje GmbH**, **Chappée**, or **Oertli**. All brand and product names are trademarks of their respective owners and are used strictly for descriptive identification (*nominative fair use*).
- **Right to Interoperability & License:** Published in accordance with **Directive 2009/24/EC**, **Article 75 of the Polish Copyright Act**, and **CJEU Case C-406/10**. Original source code licensed under the [Apache License 2.0](./LICENSE).

