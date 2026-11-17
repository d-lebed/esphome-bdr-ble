# esphome-bdr-ble

ESPHome external component (`bdr_ble`) for **BDR Thermea** boilers and heat pumps equipped with the **CU-GH08 / EHC-08 / MK3** Bluetooth Low Energy module (**De Dietrich Strateo / Alezio**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, **Oertli**).

## Features

- **Two-Stage TEA Authentication**: Performs both the BLE transport unlock (`Service 3`) and the CAN-BUS `RUB v2.01` unlock (`authLevel = 4`) automatically upon connection.
- **Compile-Time OBD Catalog (`obd_catalog.json`)**: Includes all 3,092 datapoints and 204 enum tables from the official `obd.xml` Object Dictionary.
  - Specify sensors by **short menu code** (`AC005`, `AM027`, `BM000`), **OBD signal name** (`varApEnergyConsumptionTotal`), or **hex index** (`0x5044:00`).
  - Automatically populates `unit_of_measurement`, `accuracy_decimals`, `device_class`, `state_class`, `icon`, `scale`, and default `name` at compile time.
  - **Zero firmware bloat**: only the datapoints explicitly listed in your YAML are compiled into the ESP32 binary.
- **Interval-Based Batch Polling**: Automatically groups datapoints sharing the same `update_interval` (e.g. `10s` for temperatures/pressure, `60s` for energy counters) into `GetListRequest` (`0x08`) batch polling cycles, and automatically disables polling for datapoints reported as unsupported by the appliance.
- **Appliance Snapshot & Interactive YAML Constructor (`docs/configurator.html`)**: Reads `0x502B:01..15` (`varApMenuDescriptor`) upon connection to log the exact `BDR_SNAPSHOT:` list of datapoints supported by your boiler, which can be pasted into [`docs/configurator.html`](./docs/configurator.html) alongside controller board model (`EHC-08`, `CU-08`, `CU-OH02`, etc.) and `BDR_OBD` Function Group filters.

See [`example.yaml`](./example.yaml), [`docs/configurator.html`](./docs/configurator.html), and [`docs/PROTOCOL_SPECIFICATION.md`](./docs/PROTOCOL_SPECIFICATION.md) for full details.
