# ESPHome BDR Thermea / De Dietrich BLE Component (`bdr_ble`)

An ESPHome external component (`bdr_ble`) for **BDR Thermea Group** heat pumps, condensing boilers, and thermodynamic water heaters equipped with the built-in **MK3 / EHC-08 / CU-GH08 Bluetooth Low Energy** interface (**De Dietrich**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, **Oertli**).

Communicates directly with the appliance over Bluetooth Low Energy using a standard ESP32 board (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **no proprietary gateway (`GTW-08`, `SMART TC`, etc.) or additional hardware wiring is required**, and your existing R-Bus / OpenTherm room thermostat connection remains untouched.

> **Interactive ESPHome YAML Constructor (GitHub Pages):** **[https://d-lebed.github.io/esphome-bdr-ble/](https://d-lebed.github.io/esphome-bdr-ble/)**

---

## Key Features

- **No Additional Hardware Required (100% Wireless over BLE):**
  Uses the built-in Bluetooth Low Energy radio of any ESP32 placed within BLE range of the boiler/heat pump, leaving your physical `R-Bus` / `OpenTherm` / `L-Bus` terminals completely free.
- **Full Two-Stage TEA Authentication & Automatic Passkey Bonding:**
  Automatically performs BLE GAP Passkey Entry bonding using the 6-digit PIN from `secrets.yaml` (`io_capability: keyboard_only`), unlocks GATT **Service 3** (`6a37b97e-...`) via Little-Endian 32-round TEA (`0x15EFA43F`), and authenticates on the internal **CAN-BUS (`RUB v2.01`, `auth_level: 4`)** via Big-Endian 32-round TEA.
- **Compile-Time OBD Object Dictionary (`obd_catalog.json`) with Zero Firmware Bloat:**
  Includes all **3,092 datapoints**, **204 enum tables**, **195 Function Groups**, and **14 controller board profiles** from the BDR Thermea CANopen Object Dictionary (`v1.51`):
  - Configure sensors by **short menu code** (`AM027`, `AM016`, `DM001`, `AC005`), **OBD signal name** (`varApTotalEnergyDelivery`), or **raw hex index** (`0x5044:00`).
  - Automatically infers `unit_of_measurement`, `accuracy_decimals`, `device_class`, `state_class`, `icon`, `scale` (`Gain`), and human-readable English `name` at compile time.
  - **Only the datapoints explicitly listed in your YAML are compiled into the ESP32 binary** — zero RAM/Flash overhead for unused parameters.
- **High-Speed Batch Polling (`GetListRequest 0x08`) & Auto-Skip:**
  - Automatically groups datapoints sharing the same `update_interval` (e.g., `10s` for temperatures & water pressure, `60s` for cumulative energy counters) into multi-item **`GetListRequest` (`Function = 0x08`)** batches (up to 16 items per round-trip).
  - **Automatic Unsupported Datapoint Pruning (`Auto-Skip`):** If a configured datapoint is reported as unsupported by your specific boiler (`status != 0x01` or `SDO Abort`), `bdr_ble` logs a warning once and automatically removes it from future polling batches.
- **Dynamic Appliance Snapshot (`0x502B`) & Interactive HTML Constructor ([`docs/configurator.html`](./docs/configurator.html)):**
  - Upon connecting, `bdr_ble` reads `0x502B:01..15` (`varApMenuDescriptor`) from the boiler and logs `BDR_SNAPSHOT:XXXX:YY,...` lines listing every parameter, live signal, and counter physically active on your unit.
  - Paste those `BDR_SNAPSHOT:` lines into the standalone interactive [`docs/configurator.html`](./docs/configurator.html) page (or filter by controller board model `EHC-08`, `CU-08`, `CU-OH02`, etc. and `BDR_OBD` Function Groups) to generate ready-to-copy ESPHome YAML.

---

## Tested Hardware & Compatibility

### Tested & Verified On
- **De Dietrich Strateo** air-to-water split heat pump with integrated DHW tank and electric backup heater:
  - Controller board: **`EHC-08`** (advertised BLE name `EHC-08_<serial>`, manufacturer data `"BDR"`)
  - BLE firmware: `01.00`, BLE protocol: `01.03`, RUB protocol: `02.01`
  - ESPHome node: **ESP32-C6** (`esp32-c6-devkitm-1`, ESP-IDF framework)

### Expected to Work With (Same BDR Thermea MK3 / CAN-IP BLE Platform)
All BDR Thermea appliances equipped with the **MK3 HMI / BLE module** and controller boards from the `EHC-*`, `CU-GH*` (`CU-*`), `CU-OH*`, `CU-HW*`, `CU-EB`, and `EEC-01 / SCB-*` families share the same BLE GATT profile (`f8fc98e4-5919-4a5c-852e-dfe04ad383c0`), TEA authentication (`0x15EFA43F`), and `RUB v2.01` / CANopen Object Dictionary:

| Controller Board | Category | Brand & Commercial Appliance Models |
| :--- | :--- | :--- |
| **`EHC-08` / `EHC-06`** | Air-to-Water Heat Pump (Split, Integrated DHW) | **De Dietrich** Strateo · **Baxi** Alya FS Slim, BC Smart iR32, PBS-i FS Slim · **Remeha** Mercuria Ace, Eria Tower Ace S |
| **`EHC-04`** | Air-to-Water Heat Pump (Split / Wall / Tower) | **De Dietrich** Alezio S, Alezio S V200, Alezio S Compact · **Remeha** Eria Tower, Mercuria, Tzerra Hybrid · **Baxi** PBS-i 2, Platinum BC iPlus · **Brötje** BLW Split / Mono |
| **`EHC-05`** | Air-to-Water Heat Pump | **De Dietrich** HPI-M, HPI-S · **Baxi** Platinum BC iMax · **Brötje** BLW Mono-P / Split-P |
| **`EHC-07` / `EHC-12` / `EHC-15`** | Hybrid Heat Pump (Split / Monobloc / All-in-One) | **Remeha** Elga Ace H4/6, Elga Ace Monoblock, Elga Ace All-In-One · **Baxi** Luna Hybrid Alya H |
| **`EHC-09` / `EHC-10` / `EHC-11`** | R32 Monobloc & Split Heat Pump | **De Dietrich** Alezio M · **Baxi** Alya FS & WH A, BC Mural iR32 · **Brötje** BLW Eco 10.1–16.1, BLW Mono P6.1–P9.1 |
| **`EHC-14` / `EHC-16`** | R290 & Commercial Monobloc Heat Pump | **Baxi** Auriga HP / P, UIMB Monobloc 2, UIMB BAXI Connect+ · **Remeha** Effenca Control HT 20–30, Confida 10 |
| **`CU-08` (`CU-GH08`)** | Gas Condensing Boiler | **De Dietrich** Evodens AMC / Pro, Innovens Pro MCA 160, Elidens C140 · **Remeha** Calenta Ace, Quinta Ace, Gas 120 Ace |
| **`CU-09` (`CU-GH09`)** | Gas Condensing Boiler | **De Dietrich** Naneo S, PMC-S · **Remeha** Tzerra Ace |
| **`CU-15` / `CU-16` / `CU-17` / `CU-21`** | Gas Condensing Boiler | **De Dietrich** MPX2, MCR2, Miduens · **Baxi** Luna Compact / Classic / Style / Century, Platinum iPlus · **Remeha** Tzerra & Calenta Ace-Matic · **Brötje** WGB.1, WBS.1, WBC |
| **`CU-06` / `CU-13` / `CU-20` / `CU-22`** | Commercial Gas Boiler | **De Dietrich** C 340–640 · **Remeha** Gas 220 Ace, Gas 320 Ace, Quinta · **Baxi** Bios iPlus, Luna MP · **Brötje** WGB 50.1 |
| **`CU-OH01` / `CU-OH02` / `CU-OH04`** | Oil Condensing Boiler | **De Dietrich** Modulens O Pro, Neovo, Essencio · **Baxi** Argenta GT/GTI/GTA Condens · **Brötje** BOK 19–40 · **Chappée** Sempra Nova |
| **`CU-HW01`** | Thermodynamic DHW Water Heater | **De Dietrich** Kaliko TWH · **Baxi** SPC Plus, BC ACS R290 |
| **`CU-EB`** | Electrical Boiler | **BDR Thermea** Electrical Heater Engine |
| **`EEC-01` / `SCB-*`** | Multi-Zone / Cascade / Solar Controller | **De Dietrich** VM Diematic Evolution · **Baxi** Multi Zone Controller · **Brötje** IWR RMS-W · **SCB-04 / 10 / 15 / 17** |

*(If you have tested this component with another BDR Thermea boiler or heat pump model, please open an issue or pull request to add it to the tested list!)*

---

## Important Requirements & Pairing Guide

### 1. ESP-IDF Framework & BLE Configuration
- **Use the `esp-idf` framework** (`framework: type: esp-idf`).
- **Set `io_capability: keyboard_only` under `esp32_ble:`**:
  By default, ESPHome sets `io_capability: none`, which causes ESP-IDF to immediately reject the boiler's BLE Passkey Entry bonding request (`fail_reason = 0x51`).
- **Do NOT enable `bluetooth_proxy` on the same ESP32:**
  When `bluetooth_proxy` is active and connected to Home Assistant, `esp32_ble_tracker` switches to raw advertisement forwarding and stops firing local `ESPBTDevice` discovery callbacks required by `ble_client`.

### 2. Initial BLE Pairing Procedure
1. On your boiler / heat pump control panel (HMI), enable **Bluetooth pairing mode** so the 6-digit PIN code is displayed on the boiler screen.
2. Put that 6-digit PIN into your ESPHome `secrets.yaml`:
   ```yaml
   strateo_ble_pin: 123456
   ```
3. Flash the ESP32. Upon connecting, `bdr_ble` automatically initiates BLE bonding, replies with your PIN, stores the bond in ESP32 NVS flash, and unlocks Service 3 + CAN-BUS `RUB v2.01`.
4. **Coexistence with the Mobile App:**
   The boiler's BLE module accepts only **one** active BLE connection at a time. Include the `ble_client` switch (`Boiler BLE Connect`) in your YAML — turning that switch **Off** in Home Assistant cleanly disconnects the ESP32 so you can connect with the official **De Dietrich PRO / Baxi / Remeha Service Tool** mobile app whenever needed.

---

## Quick Start

Add the external component to your ESPHome YAML configuration (see [`example.yaml`](./example.yaml) for a complete configuration):

```yaml
external_components:
  - source: github://d-lebed/esphome-bdr-ble
    components: [bdr_ble]

esp32_ble:
  io_capability: keyboard_only

esp32_ble_tracker:
  scan_parameters:
    active: true

ble_client:
  - mac_address: "00:A0:50:XX:XX:XX"
    id: boiler_ble

bdr_ble:
  id: boiler_hub
  ble_client_id: boiler_ble
  pin: !secret strateo_ble_pin
  node_id: 1
  auth_level: 4

switch:
  - platform: ble_client
    ble_client_id: boiler_ble
    name: "Boiler BLE Connect"
    restore_mode: RESTORE_DEFAULT_ON
    entity_category: config
```

---

## Configuration Reference

### 1. Hub (`bdr_ble`)

```yaml
bdr_ble:
  id: boiler_hub
  ble_client_id: boiler_ble       # Required: ID of the ble_client entry
  pin: !secret strateo_ble_pin    # Optional: 6-digit BLE pairing passkey (0..999999)
  node_id: 1                      # Optional: Default CANopen Node ID (default: 1 = main Control Unit)
  auth_level: 4                   # Optional: TEA authorization level (default: 4 = Service/Installer)
```

---

### 2. Numeric Sensors (`sensor`)

`sensor:` supports both **grouped batch syntax** (`sensors: [...]` sharing a top-level `update_interval`) and **single sensor syntax**. All sensors sharing the same `update_interval` are automatically combined into `GetListRequest (0x08)` batch reads (up to 16 datapoints per packet).

You can reference any datapoint by:
- **Short menu code** (`AM027`, `AM016`, `DM001`, `AM019`, `AC005`, `HM001`, ...),
- **OBD signal name** (`varApTotalEnergyDelivery`, `varApTflow`, ...), or
- **Hex `Index:SubIndex`** (`0x5044:00`, `0x501E:00`).

```yaml
sensor:
  # Fast polling group (every 10 seconds in a single GetListRequest batch)
  - platform: bdr_ble
    update_interval: 10s
    sensors:
      - parameter: AM027
        name: "Strateo Outside Temperature"
      - parameter: AM016
        name: "Strateo Flow Temperature"
      - parameter: DM001
        name: "Strateo DHW Temperature"
      - parameter: AM019
        name: "Strateo Water Pressure"

  # Slow polling group (every 60 seconds in a single GetListRequest batch)
  - platform: bdr_ble
    update_interval: 60s
    sensors:
      - parameter: AC005
        name: "Strateo Energy CH Consumption"
      - parameter: AC006
        name: "Strateo Energy DHW Consumption"
      - parameter: AC007
        name: "Strateo Energy Total Consumption"
      - parameter: AC026
        name: "Strateo Energy Backup Consumption"
      - parameter: AC008
        name: "Strateo Energy CH Delivery"
      - parameter: AC009
        name: "Strateo Energy DHW Delivery"
      - parameter: AC019
        name: "Strateo Energy Backup Delivery"
      - parameter: varApTotalEnergyDelivery
        name: "Strateo Energy Total Production"
```

#### Optional Per-Sensor Overrides
Every item inside `sensors:` (or a standalone `- platform: bdr_ble` sensor entry) automatically populates its metadata from `obd_catalog.json`, but you can override any standard ESPHome sensor option:
- `parameter` (**Required**): Menu code (`AC005`), OBD name (`varAp...`), or hex (`0x5044:00`).
- `name` (*Optional*): Entity name in Home Assistant (defaults to the clean English label from `obd_catalog.json`, e.g., `"Outdoor temperature"`).
- `sub_index` (*Optional*): Override CANopen `SubIndex` (`0..255`, e.g., for zone or cascade array parameters).
- `node_id` (*Optional*): Override CANopen `NodeId` (`0..255`, defaults to hub `node_id: 1`).
- `scale` (*Optional*): Override the multiplier (`Gain`) from `obd_catalog.json`.
- Standard ESPHome `sensor` options: `unit_of_measurement`, `accuracy_decimals`, `device_class`, `state_class`, `icon`, `entity_category`, `filters`, etc.

---

### 3. Text Sensors (`text_sensor`)

Provides:
1. **`status`**: Diagnostic text sensor reporting the current BLE connection, pairing, TEA unlock, snapshot discovery, and ready state.
2. **Decoded `Enum`, `VisibleString`, and `TimeOfDay` OBD datapoints**: When an `Enum` datapoint (such as `AM012` `varApStatus` or `AM014` `varApSubStatus`) is configured under `text_sensor:`, only that specific enum's translation table is compiled into the firmware so raw integer values are automatically translated into human-readable strings.

```yaml
text_sensor:
  - platform: bdr_ble
    status:
      name: "Strateo BLE Status"

  - platform: bdr_ble
    update_interval: 30s
    sensors:
      - parameter: AM012
        name: "Strateo Appliance Status"
      - parameter: AM014
        name: "Strateo Appliance Sub-Status"
```

### 4. Writable Numeric Parameters (`number`)

Supports editing numeric boiler / heat pump parameters (`U8`, `I8`, `U16`, `I16`, `U32`, `I32`, such as DHW setpoints, heating curve slopes, or zone temperature setpoints).

- Automatically populates `unit_of_measurement`, `device_class`, `icon`, `step` (`Gain`), and initial `min_value` / `max_value` from `obd_catalog.json` (`entity_category: config`, default `mode: box`).
- **Dynamic Boiler Limits from `0x502B`:** When the boiler connects and `0x502B:01..05` (`varApMenuDescriptor`) is read, `bdr_ble` automatically updates each `number` entity's `min_value`, `max_value`, and `step` to the exact hardware limits reported by your boiler.
- Changing a value in Home Assistant sends a **Confirmed Write Request (`Function = 0x04`)** over BLE and immediately reads back the parameter upon receiving `Write Pos Ack (`0x05`)` (or reverts on `Write Neg Ack (`0x06`)`).

```yaml
number:
  - platform: bdr_ble
    update_interval: 30s
    numbers:
      - parameter: DP070
        name: "DHW Comfort Setpoint"
        min_value: 35
        max_value: 65
        step: 0.5
        mode: box
      - parameter: CP010
        name: "Zone Flow Temperature Setpoint"
```

---

### 5. Writable Enum Parameters (`select`)

Supports editing enumerated boiler / heat pump parameters (`MetaDataPointEnum`, such as operating modes, DHW modes, or schedule selections).

- Automatically populates the dropdown `options:` list at compile time from the parameter's `Enum` definition in `obd_catalog.json` (`entity_category: config`, default icon `mdi:tune-variant`).
- Selecting a new option in Home Assistant translates the option label back to its raw integer enum code, sends a **Confirmed Write Request (`Function = 0x04`)**, and reads back the confirmed value from the boiler.

```yaml
select:
  - platform: bdr_ble
    update_interval: 30s
    selects:
      - parameter: DP200
        name: "DHW Operating Mode"
      - parameter: CP320
        name: "Heating Zone Operating Mode"
```

---

## Step-by-Step Workflow: How to Discover & Configure Supported Sensors for Your Boiler

Because BDR Thermea's Object Dictionary (`obd_catalog.json`) contains **3,092 datapoints** across dozens of heat pump, gas boiler, oil boiler, and cascade configurations, each individual appliance only implements the subset of parameters that match its hardware and configuration (`CN1` / `CN2`).

Here is the recommended **3-step workflow** to discover and configure the exact sensors supported by your specific boiler or heat pump:

### Step 1: Flash a Minimal Config (Without Sensors) & Capture the Boiler Snapshot
1. First, flash your ESP32 with just the `bdr_ble` hub and the `status` text sensor (no `sensor:` entries needed yet):
   ```yaml
   external_components:
     - source: github://d-lebed/esphome-bdr-ble
       components: [bdr_ble]

   esp32_ble:
     io_capability: keyboard_only

   esp32_ble_tracker:
     scan_parameters:
       active: true

   ble_client:
     - mac_address: "00:A0:50:XX:XX:XX"
       id: boiler_ble

   bdr_ble:
     id: boiler_hub
     ble_client_id: boiler_ble
     pin: !secret strateo_ble_pin
     node_id: 1
     auth_level: 4

   text_sensor:
     - platform: bdr_ble
       status:
         name: "Boiler BLE Status"
   ```
2. Open the **ESPHome logs**. As soon as `bdr_ble` pairs and completes the two-stage TEA unlock, it automatically queries the boiler's internal menu descriptor table (`0x502B:01..15` `varApMenuDescriptor`) and prints a `BDR_SNAPSHOT:` block:
   ```text
   [I][bdr_ble:695]: === Appliance Snapshot (148 supported datapoints from 0x502B:01..15) ===
   [I][bdr_ble:714]: BDR_SNAPSHOT:500A:00,500C:00,5013:00,5016:00,501E:00,5044:00,5045:00,...
   [I][bdr_ble:714]: BDR_SNAPSHOT:5046:00,507A:00,5085:00,5086:00,5600:00,5601:00,...
   [I][bdr_ble:716]: === End of Appliance Snapshot (paste BDR_SNAPSHOT lines into configurator.html) ===
   ```

### Step 2: Paste the Snapshot into the Interactive Configurator & Pick Your Sensors
1. Open the **[Interactive ESPHome YAML Constructor (GitHub Pages)](https://d-lebed.github.io/esphome-bdr-ble/)** (or open [`docs/configurator.html`](./docs/configurator.html) locally in your browser).
2. Copy the `BDR_SNAPSHOT:...` lines from your ESPHome log (you can copy them raw, even with `[I][bdr_ble:...]` prefixes and timestamps) and paste them into the **`Paste BDR_SNAPSHOT:... lines from ESPHome logs`** input box.
3. The table immediately filters down to **only the exact parameters, live measurements (`Signal`), and counters (`Counter`) reported by your boiler's `0x502B` descriptor**.
4. Check the sensors you want, choose their polling interval (`5s`, `10s`, `30s`, `60s`, `300s`), and choose your YAML formatting options in the right-hand toolbar:
   - **`Include name:`** — generates clean English sensor names (e.g. `name: "Energy consumed in standby mode"` or `name: "Outdoor temperature"`).
   - **`Prefix name: with menu code (AM027 ...)`** — optional toggle if you prefer the short installer menu code prepended to the sensor name (off by default).
5. Click **Copy YAML** and paste the generated `sensor:` / `text_sensor:` blocks into your ESPHome configuration.

### Step 3: Optionally Add Extra Parameters Manually (Protected by `Auto-Skip`)
Some internal CANopen datapoints (such as certain aggregate counters like `varApTotalEnergyDelivery` `0x5089:00` that don't have a front-panel `AM`/`AC` menu item) may still be supported by your boiler's firmware even if they are not listed in the user menu table `0x502B`:
1. In [`docs/configurator.html`](./docs/configurator.html), click **Clear Snapshot** (or select your controller board model such as `EHC-08`, `CU-08`, `CU-OH02`, or uncheck *"Only with menu code"*) to browse all datapoints for your appliance family and Function Groups (`FG1001_A_41`, `FG1008_X_00`, `FG2001_A_00`, etc.).
2. Add any additional parameters you want to test to your YAML.
3. **Automatic Safety Net (`Auto-Skip`):** If any manually added parameter turns out not to be supported by your boiler (`GetListResponse` status `!= 0x01` or `SDO Abort`), `bdr_ble` will log a warning once on the first read and **automatically disable polling for that datapoint** so it never slows down your BLE batches.

---

## Protocol & Reverse-Engineering Documentation

- **[Live Interactive ESPHome YAML Constructor (GitHub Pages)](https://d-lebed.github.io/esphome-bdr-ble/)** ([source](./docs/configurator.html))
- [BDR Thermea / De Dietrich BLE Protocol Specification](./docs/PROTOCOL_SPECIFICATION.md)
- [Changelog](./CHANGELOG.md)

---

## Disclaimer & Legal Notice

This project is an **independent reverse-engineering effort** created solely for the purpose of achieving interoperability between **BDR Thermea Group** heating appliances (**De Dietrich**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, **Oertli**) and **Home Assistant** / **ESPHome**. All technical observations and live protocol analyses were performed on a **De Dietrich Strateo (`EHC-08`)** heat pump owned by the author.

- **No Affiliation:** This project is **not** affiliated with, endorsed by, sponsored by, or authorized by **BDR Thermea Group B.V.**, **De Dietrich Thermique**, **Baxi**, **Remeha**, **August Brötje GmbH**, **Chappée**, or **Oertli**.
- **Trademarks:** *"BDR Thermea"*, *"De Dietrich"*, *"Strateo"*, *"Alezio"*, *"Evodens"*, *"Innovens"*, *"Diematic"*, *"Baxi"*, *"Remeha"*, *"Brötje"*, *"Chappée"*, and *"Oertli"* are trademarks or registered trademarks of their respective owners. They are used in this repository strictly for descriptive and identification purposes (*nominative fair use*) to indicate hardware compatibility.
- **Right to Interoperability:** This publication is based on:
  - **Directive 2009/24/EC** of the European Parliament and of the Council on the legal protection of computer programs (specifically the right to observe, study, and reverse-engineer protocols for the purpose of achieving interoperability);
  - **Article 75** of the Polish Act on Copyright and Related Rights (*Ustawa o prawie autorskim i prawach pokrewnych*);
  - **CJEU Case C-406/10 (*SAS Institute Inc. v. World Programming Ltd*)**, establishing that neither the functionality of a computer program nor the communication protocol / data format used by it constitutes a form of expression protected by copyright.
- **Original Code:** All C++, Python, and HTML/JavaScript source code in this repository is an original implementation written for ESPHome and contains no proprietary binary code from BDR Thermea.
- **Use at Your Own Risk:** This software is provided **"AS IS"**, without warranty of any kind. Connecting third-party software or hardware to your heating appliance is done **entirely at your own risk**. The author assumes no responsibility or liability for any hardware damage, malfunction of the heating or hot water system, property damage, or **loss of manufacturer warranty**.

## License

Apache License 2.0 — see [LICENSE](./LICENSE).
