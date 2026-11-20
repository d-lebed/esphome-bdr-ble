# ESPHome BDR Thermea / Remeha / De Dietrich / Baxi / Brötje BLE-component (`bdr_ble`)

[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-38bdf8?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl)
[![Interactive YAML Configurator](https://img.shields.io/badge/configurator-Interactive%20YAML%20Builder-22c55e?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=nl)
[![Release](https://img.shields.io/github/v/release/d-lebed/esphome-bdr-ble?style=flat-square)](https://github.com/d-lebed/esphome-bdr-ble/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?style=flat-square&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/lebiadz)

🌐 **Taal:** [English](./README.md) | [Deutsch](./README.de.md) | [Français](./README.fr.md) | **Nederlands** | [Polski](./README.pl.md) | [Italiano](./README.it.md) | [Español](./README.es.md)

Een **externe ESPHome-component (`bdr_ble`)** voor het uitlezen en aansturen van warmtepompen, CV-ketels en warmtepompboilers van de **BDR Thermea Group** (**Remeha**, **De Dietrich**, **Baxi**, **Brötje**, **Chappée**, **Oertli**) in **Home Assistant** via **Bluetooth Low Energy (BLE)**.

Communiceert rechtstreeks met de ingebouwde **MK3 / EHC-08 / CU-GH08 Bluetooth-interface** van het toestel (dezelfde BLE-interface die gebruikt wordt door de officiële *Remeha Smart Service*, *De Dietrich PRO* en *Baxi Service Tool* apps) met elke standaard **ESP32** (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **geen dure `GTW-08` gateway of bekabeling nodig**, terwijl uw bestaande **OpenTherm / R-Bus** kamerthermostaat gewoon aangesloten blijft.

---

## 📖 Documentatie & Interactieve YAML-Configurator

- **📘 [Volledige documentatie & Installatiehandleiding (GitHub Pages — Nederlands)](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl)**
  - [Ondersteunde CV-ketels & Warmtepompen (`EHC-*`, `CU-*`, `SCB-*`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl#compatibility)
  - [BLE-koppeling & Vereisten](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl#requirements)
  - [Sensordetectie in 3 stappen (`BDR_SNAPSHOT`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl#discovery-workflow)
  - [Configuratiereferentie (`sensor`, `binary_sensor`, `text_sensor`, `number`, `select`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl#configuration)
  - [FAQ & Probleemoplossing](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl#faq)
- **⚡ [Interactieve ESPHome YAML-Configurator (met Nederlandse parameternamen)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=nl)**
  - Doorzoek alle **3.092 BDR Thermea OBD-datapunten**, filter op regelaarprint (`EHC-08`, `CU-08`, enz.) of plak uw `BDR_SNAPSHOT:`-log vanuit ESPHome om kant-en-klare YAML te genereren.
- **🔬 [BDR Thermea / De Dietrich BLE-protocolspecificatie (Engels)](./docs/PROTOCOL_SPECIFICATION.md)**
- **📋 [Changelog](./CHANGELOG.md)**

---

## Kenmerken

- **100% draadloos via BLE:** Uw fysieke `R-Bus` / `OpenTherm` / `L-Bus`-klemmen blijven volledig vrij.
- **Dynamische GATT UUID-detectie, Passkey-koppeling & `GTW-IoT`-ondersteuning:**
  - Zoekt bij elke verbinding alle GATT-handles en CCCD's dynamisch op via 128-bit UUID (`EHC-08`, `EHC-16`, `CU-*`, `GTW-22`, `GTW-IoT`).
  - Ondersteunt zowel de **ingebouwde BLE-module (`MK3` / `GTW-22`) met 6-cijferige pincode** als de **externe `GTW-IoT` home-hub zonder pincode** (laat `pin:` in de YAML simpelweg weg en druk kort op de knop van de hub).
  - Automatisch herstel bij een gewiste bond (max. 3 gekoppelde apparaten) en slimme wachttijd bij de 60-seconden blokkade (`90s → 180s → 300s → 600s` backoff).
  - Instelbaar toegangsniveau (`auth_level: 1..4`: `1` = Eindgebruiker, `2` = Installateur Standaard, `3` = Service, `4` = Lab/OEM).
- **Lezen & Schrijven:**
  - `sensor`, `binary_sensor` & `text_sensor`: Temperaturen, waterdruk, binaire statusvlaggen (`HM012`, `HM013`, `AM015`), status-enums en kWh-energietellers uitlezen.
  - `number` & `select`: Numerieke setpoints (bijv. SWW comfort/eco-temperatuur, stooklijn) en bedrijfsmodi wijzigen (`WriteConfirmedRequest 0x04`) met automatische min/max/stap-grenzen uit `0x502B`.
- **Snelle Bulk-uitlezing (`GetList`) & Auto-Skip:** Vertaalt menucodes (`AM027`, `DM001`, `AC005`, `DP070`) tijdens het compileren, bundelt reads via `GetListRequest (0x08)` en slaat niet-ondersteunde punten automatisch over (`Auto-Skip`).

---

## Snelstart (Quick Start)

Zie [`example.yaml`](./example.yaml) en de **[Volledige documentatie](https://d-lebed.github.io/esphome-bdr-ble/?lang=nl#quick-start)**:

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

text_sensor:
  - platform: bdr_ble
    status:
      name: "Boiler BLE Status"
```

---

## Disclaimer & Aansprakelijkheid

Dit project is een **onafhankelijk, onofficieel reverse-engineering-initiatief** voor interoperabiliteit tussen **BDR Thermea Group**-toestellen en **Home Assistant** / **ESPHome**.

- **Slechts op één model getest:** Fysiek uitsluitend getest op een **De Dietrich Strateo R32 (`EHC-08`)** warmtepomp van de auteur.
- **Risico op verlies van garantie:** Het verbinden van externe hardware/software of authenticeren op serviceniveau (`auth_level: 4`) kan door de regelaar worden geregistreerd en **uw fabrieksgarantie doen vervallen**.
- **Geen garantie & Gebruik op eigen risico:** Deze software wordt geleverd **"AS IS"**, zonder enige garantie. **Gebruik is volledig op eigen risico.** Broncode gelicentieerd onder [Apache License 2.0](./LICENSE).
