# Componente BLE ESPHome per Baxi / De Dietrich / BDR Thermea / Remeha / Brötje (`bdr_ble`)

[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-38bdf8?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/?lang=it)
[![Interactive YAML Configurator](https://img.shields.io/badge/configurator-Interactive%20YAML%20Builder-22c55e?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=it)
[![Release](https://img.shields.io/github/v/release/d-lebed/esphome-bdr-ble?style=flat-square)](https://github.com/d-lebed/esphome-bdr-ble/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?style=flat-square&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/lebiadz)

🌐 **Lingua:** [English](./README.md) | [Deutsch](./README.de.md) | [Français](./README.fr.md) | [Nederlands](./README.nl.md) | [Polski](./README.pl.md) | **Italiano** | [Español](./README.es.md)

Un **componente esterno ESPHome (`bdr_ble`)** per monitorare e controllare pompe di calore, caldaie a condensazione e scaldacqua termodinamici del gruppo **BDR Thermea** (**Baxi**, **De Dietrich**, **Remeha**, **Brötje**, **Chappée**, **Oertli**) in **Home Assistant** tramite **Bluetooth Low Energy (BLE)**.

Comunica direttamente con l'**interfaccia Bluetooth integrata MK3 / EHC-08 / CU-GH08** dell'apparecchio (la stessa interfaccia BLE utilizzata dalle app ufficiali *Baxi Service Tool*, *De Dietrich PRO* e *Remeha Smart Service*) usando un comune **ESP32** (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **senza gateway proprietario `GTW-08` né cablaggi**, lasciando i morsetti **OpenTherm / R-Bus** liberi per il tuo termostato ambiente.

---

## 📖 Documentazione & Configuratore YAML Interattivo

- **📘 [Documentazione completa & Guida all'installazione (GitHub Pages — Italiano)](https://d-lebed.github.io/esphome-bdr-ble/?lang=it)**
  - [Caldaie e Pompe di calore supportate (`EHC-*`, `CU-*`, `SCB-*`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=it#compatibility)
  - [Requisiti & Associazione BLE](https://d-lebed.github.io/esphome-bdr-ble/?lang=it#requirements)
  - [Rilevamento sensori in 3 passi (`BDR_SNAPSHOT`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=it#discovery-workflow)
  - [Riferimento configurazione (`sensor`, `binary_sensor`, `text_sensor`, `number`, `select`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=it#configuration)
  - [FAQ & Risoluzione problemi](https://d-lebed.github.io/esphome-bdr-ble/?lang=it#faq)
- **⚡ [Configuratore YAML ESPHome Interattivo (con nomi dei parametri in italiano)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=it)**
  - Esplora tutti i **3.092 parametri OBD BDR Thermea**, filtra per modello di scheda (`EHC-08`, `CU-08`, ecc.) o incolla il log `BDR_SNAPSHOT:` da ESPHome per generare codice YAML pronto all'uso.
- **🔬 [Specifica del protocollo BLE BDR Thermea / De Dietrich (Inglese)](./docs/PROTOCOL_SPECIFICATION.md)**
- **📋 [Changelog](./CHANGELOG.md)**

---

## Caratteristiche principali

- **100% wireless via BLE:** I morsetti fisici `R-Bus` / `OpenTherm` / `L-Bus` restano completamente liberi.
- **Rilevamento dinamico UUID GATT, Pairing PIN e supporto `GTW-IoT`:**
  - Risolve dinamicamente tutti gli handle GATT tramite UUID a 128 bit alla connessione (`EHC-08`, `EHC-16`, `CU-*`, `GTW-22`, `GTW-IoT`).
  - Supporta sia il **modulo BLE integrato (`MK3` / `GTW-22`) con PIN a 6 cifre**, sia l'**hub esterno `GTW-IoT` senza PIN** (ometti semplicemente `pin:` nel file YAML).
  - Ripristino automatico in caso di rimozione del bond (limite di 3 dispositivi associati) e gestione del blocco di 60 secondi (`90s → 180s → 300s → 600s`).
  - Livello di accesso configurabile (`auth_level: 1..4`: `1` = Utente, `2` = Installatore Standard, `3` = Assistenza, `4` = Lab/OEM).
- **Supporto Lettura & Scrittura:**
  - `sensor`, `binary_sensor` & `text_sensor`: Temperature, pressione acqua, flag binari (`HM012`, `HM013`, `AM015`), stati testuali e contatori di energia consumata/prodotta (kWh).
  - `number` & `select`: Modifica setpoint numerici (es. temperatura ACS comfort/eco, curva climatica) e modalità operative (`WriteConfirmedRequest 0x04`) con limiti min/max/step rilevati automaticamente da `0x502B`.
- **Lettura batch (`GetList`) & Auto-Skip:** Risolve i codici menu (`AM027`, `DM001`, `AC005`, `DP070`) in fase di compilazione, raggruppa le letture tramite `GetListRequest (0x08)` e disattiva automaticamente i parametri non supportati (`Auto-Skip`).

---

## Avvio rapido (Quick Start)

Vedi [`example.yaml`](./example.yaml) e la **[Documentazione completa](https://d-lebed.github.io/esphome-bdr-ble/?lang=it#quick-start)**:

```yaml
external_components:
  - source: github://d-lebed/esphome-bdr-ble@v1.5.1
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

button:
  - platform: bdr_ble
    bdr_ble_id: boiler_hub
    name: "Discover CAN Nodes & Snapshot"

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

## Esclusione di responsabilità & Note legali

Questo progetto è uno **studio indipendente e non ufficiale di reverse-engineering** creato esclusivamente a scopo educativo e di interoperabilità tra gli apparecchi **BDR Thermea Group** e **Home Assistant** / **ESPHome**.

- **Testato su un solo modello:** Tutti i test sono stati eseguiti esclusivamente su una pompa di calore **De Dietrich Strateo R32 (`EHC-08`)** di proprietà dell'autore.
- **Rischio di invalidazione della garanzia:** Il collegamento di hardware/software di terze parti o l'autenticazione con privilegi di servizio (`auth_level: 4`) può essere registrato dalla scheda elettronica e **invalidare la garanzia del produttore o dell'installatore**.
- **Nessuna garanzia & Uso a proprio rischio:** Il software è fornito **"COSÌ COM'È" ("AS IS")**, senza alcuna garanzia. **L'utilizzo avviene interamente a proprio rischio.** Codice rilasciato sotto licenza [Apache License 2.0](./LICENSE).
