# ESPHome BDR Thermea / Brötje / Remeha / De Dietrich / Baxi BLE-Komponente (`bdr_ble`)

[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-38bdf8?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/?lang=de)
[![Interactive YAML Configurator](https://img.shields.io/badge/configurator-Interactive%20YAML%20Builder-22c55e?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=de)
[![Release](https://img.shields.io/github/v/release/d-lebed/esphome-bdr-ble?style=flat-square)](https://github.com/d-lebed/esphome-bdr-ble/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?style=flat-square&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/lebiadz)

🌐 **Sprache:** [English](./README.md) | **Deutsch** | [Français](./README.fr.md) | [Nederlands](./README.nl.md) | [Polski](./README.pl.md) | [Italiano](./README.it.md) | [Español](./README.es.md)

Eine **externe ESPHome-Komponente (`bdr_ble`)** zur Überwachung und Steuerung von Wärmepumpen, Brennwertkesseln und Warmwasserspeichern der **BDR Thermea Group** (**Brötje**, **Remeha**, **De Dietrich**, **Baxi**, **Oertli**, **Chappée**) in **Home Assistant** über **Bluetooth Low Energy (BLE)**.

Die Komponente kommuniziert direkt mit der integrierten **MK3 / EHC-08 / CU-GH08 Bluetooth-Schnittstelle** des Heizgeräts (derselben BLE-Schnittstelle, die auch von den offiziellen Apps *Brötje Service App*, *Remeha Smart Service*, *De Dietrich PRO* und *Baxi Service Tool* genutzt wird) über einen handelsüblichen **ESP32** (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **ohne proprietäres `GTW-08`-Gateway und ohne zusätzliche Verkabelung**, während Ihr vorhandenes **OpenTherm / R-Bus**-Raumthermostat unverändert weiterläuft.

---

## 📖 Dokumentation & Interaktiver YAML-Konfigurator

- **📘 [Vollständige Dokumentation & Einrichtungsanleitung (GitHub Pages — Deutsch)](https://d-lebed.github.io/esphome-bdr-ble/?lang=de)**
  - [Unterstützte Heizkessel & Wärmepumpen (`EHC-*`, `CU-*`, `SCB-*`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=de#compatibility)
  - [BLE-Kopplung & Voraussetzungen](https://d-lebed.github.io/esphome-bdr-ble/?lang=de#requirements)
  - [3-Schritte-Sensorerkennung (`BDR_SNAPSHOT`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=de#discovery-workflow)
  - [Konfigurationsreferenz (`sensor`, `binary_sensor`, `text_sensor`, `number`, `select`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=de#configuration)
  - [FAQ & Fehlerbehebung](https://d-lebed.github.io/esphome-bdr-ble/?lang=de#faq)
- **⚡ [Interaktiver ESPHome YAML-Konfigurator (mit deutschen Parameternamen)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=de)**
  - Durchsuchen Sie alle **3.092 BDR Thermea OBD-Datenpunkte** (auf Deutsch, Englisch oder Polnisch), filtern Sie nach Reglerplatine (`EHC-08`, `CU-08` usw.) oder fügen Sie Ihr `BDR_SNAPSHOT:`-Log aus ESPHome ein, um fertiges YAML zu generieren.
- **🔬 [BDR Thermea / De Dietrich BLE-Protokollspezifikation (Englisch)](./docs/PROTOCOL_SPECIFICATION.md)**
- **📋 [Changelog](./CHANGELOG.md)**

---

## Highlights

- **100 % drahtlos über BLE:** Ihre physischen `R-Bus`- / `OpenTherm`- / `L-Bus`-Klemmen bleiben vollständig frei.
- **Dynamische GATT-UUID-Erkennung, Passkey-Kopplung & `GTW-IoT`-Unterstützung:**
  - Löst alle GATT-Handles und CCCDs beim Verbinden dynamisch per 128-Bit-UUID auf (kompatibel mit `EHC-08`, `EHC-16`, `CU-*`, `GTW-22` und `GTW-IoT`).
  - Unterstützt sowohl **integrierte BLE-Module (`MK3` / `GTW-22`) mit 6-stelliger PIN** als auch **externe `GTW-IoT`-Hubs ohne PIN** (`pin:` im YAML einfach weglassen).
  - Automatische Wiederherstellung bei gelöschten Bindungen (3-Geräte-Limit des Gateways) und Einhaltung der 60-Sekunden-Sperre (`90s → 180s → 300s → 600s` Backoff).
  - Konfigurierbare Zugriffsebene (`auth_level: 1..4`: `1` = Endbenutzer sicher, `2` = Fachhandwerker Standard, `3` = Service, `4` = Labor/OEM).
- **Lese- & Schreibunterstützung:**
  - `sensor`, `binary_sensor` & `text_sensor`: Temperaturen, Wasserdruck, Status-Flags (`HM012`, `HM013`, `AM015`), Status-Enums sowie kWh-Energieverbrauchs- und Wärmemengenzähler auslesen.
  - `number` & `select`: Numerische Sollwerte (z. B. Warmwasser-Komfort-/Eco-Sollwert, Vorlaufsollwerte) und Betriebsarten (`WriteConfirmedRequest 0x04`) mit automatischer Hardware-Grenzwertprüfung aus `0x502B` anpassen.
- **Schnelle Batch-Abfragen (`GetList`) & Auto-Skip:** Löst Menükurzbezeichnungen (`AM027`, `DM001`, `AC005`, `DP070`) zur Kompilierzeit auf, bündelt Abfragen über `GetListRequest (0x08)` und überspringt nicht unterstützte Datenpunkte automatisch (`Auto-Skip`).

---

## Hardware-Kompatibilität (Getestet vs. Ungetestet)

- **✅ Physisch getestet & verifiziert:**
  - **De Dietrich Strateo R32** (`EHC-08`-Reglerplatine, integriertes BLE `01.00` mit 6-stelliger PIN, RUB-Protokoll `02.01`).
  - **Remeha Elga Ace All-in-One** (`EHC-15` Hybrid-Wärmepumpe über externen **Remeha Home Hub `GTW-IoT`** im PIN-losen Modus).
- **⚠️ Theoretische / ungetestete Kompatibilität (gemeinsame MK3 BLE / CAN-IP Plattform):**
  - Weitere BDR Thermea Geräte mit **MK3 HMI / BLE-Modul** (`EHC-04`, `EHC-05`, `EHC-07`, `EHC-09`, `EHC-14`, `CU-08` / `CU-GH08`, `CU-09`, `CU-16`, `CU-13`, `CU-OH02`, `CU-HW01`, `CU-EB`, `EEC-01 / SCB-*` der Marken **Brötje**, **Remeha**, **De Dietrich**, **Baxi**, **Oertli** und **Chappée**) nutzen denselben BLE-GATT-Service und dasselbe `obd_catalog.json`-Wörterbuch in der offiziellen Service-App, wurden jedoch **vom Autor NICHT physisch getestet**.

---

## Schnellstart (Quick Start)

Siehe [`example.yaml`](./example.yaml) und die **[vollständige Dokumentation](https://d-lebed.github.io/esphome-bdr-ble/?lang=de#quick-start)**:

```yaml
external_components:
  - source: github://d-lebed/esphome-bdr-ble@v1.5.0
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

## Haftungsausschluss & Rechtliche Hinweise

Dieses Projekt ist eine **unabhängige, inoffizielle Reverse-Engineering-Entwicklung**, die ausschließlich zu Bildungs- und Interoperabilitätszwecken zwischen Heizgeräten der **BDR Thermea Group** und **Home Assistant** / **ESPHome** erstellt wurde.

- **Nur an einem Modell getestet:** Sämtliche Tests wurden ausschließlich an einer einzelnen **De Dietrich Strateo R32 (`EHC-08`)** Wärmepumpe des Autors durchgeführt. Andere im Katalog aufgeführte Modelle sind ungetestet.
- **Risiko des Garantieverlusts:** Die Verbindung von Drittanbieter-Hard-/Software oder die Authentifizierung mit Fachhandwerker-/Service-Berechtigungen (`auth_level: 4`) kann von der Gerätesteuerung protokolliert werden und **zum Verlust Ihrer Hersteller- oder Installateurgarantie führen**.
- **Gefahr beim Schreiben von Parametern:** Das Schreiben von Parametern (`number:` / `select:`) verändert interne Reglereinstellungen im nichtflüchtigen Speicher (EEPROM/Flash). Unsachgemäße Werte oder zu häufiges Schreiben durch Automationen können den Speicher verschleißen oder einen unsicheren Betrieb der Heizungsanlage verursachen.
- **Keine Gewährleistung & Haftungsausschluss:** Diese Software wird **„WIE BESEHEN“ („AS IS“)** ohne jegliche ausdrückliche oder stillschweigende Gewährleistung bereitgestellt. **In keinem Fall haften der Autor oder Mitwirkende für direkte, indirekte, zufällige oder Folgeschäden** (einschließlich Geräteschäden, Wasser-/Frostschäden, Personenschäden, Ausfall von Heizung/Warmwasser oder Garantieverlust). **Die Nutzung erfolgt vollständig auf eigene Gefahr.**
- **Keine Markenverbindung:** Nicht verbunden mit, unterstützt oder gesponsert von **BDR Thermea Group B.V.**, **August Brötje GmbH**, **Remeha**, **De Dietrich Thermique**, **Baxi**, **Chappée** oder **Oertli**. Alle Marken- und Produktnamen dienen ausschließlich der beschreibenden Identifikation.
- **Recht auf Interoperabilität & Lizenz:** Veröffentlicht gemäß **EU-Richtlinie 2009/24/EG**, **Artikel 75 des polnischen Urheberrechtsgesetzes** und **EuGH-Rechtssache C-406/10**. Originalquellcode lizenziert unter der [Apache License 2.0](./LICENSE).
