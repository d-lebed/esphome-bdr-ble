# Komponent BLE ESPHome dla BDR Thermea / De Dietrich / Baxi / Remeha (`bdr_ble`)

[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-38bdf8?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl)
[![Interactive YAML Configurator](https://img.shields.io/badge/configurator-Interactive%20YAML%20Builder-22c55e?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=pl)
[![Release](https://img.shields.io/github/v/release/d-lebed/esphome-bdr-ble?style=flat-square)](https://github.com/d-lebed/esphome-bdr-ble/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?style=flat-square&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/lebiadz)

🌐 **Język:** [English](./README.md) | [Deutsch](./README.de.md) | [Français](./README.fr.md) | [Nederlands](./README.nl.md) | **Polski** | [Italiano](./README.it.md) | [Español](./README.es.md)

**Zewnętrzny komponent ESPHome (`bdr_ble`)** do monitorowania i sterowania pompami ciepła, kotłami kondensacyjnymi oraz podgrzewaczami c.w.u. grupy **BDR Thermea** (**De Dietrich**, **Baxi**, **Remeha**, **Brötje**, **Chappée**, **Oertli**) w systemie **Home Assistant** przez **Bluetooth Low Energy (BLE)**.

Komunikuje się bezpośrednio z wbudowanym w urządzenie **modułem Bluetooth MK3 / EHC-08 / CU-GH08** (tym samym interfejsem BLE, z którego korzystają oficjalne aplikacje serwisowe *De Dietrich PRO*, *Baxi Service Tool* oraz *Remeha Smart Service*) przy użyciu dowolnej płytki **ESP32** (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **bez konieczności zakupu drogiej bramki `GTW-08` ani prowadzenia przewodów**, pozostawiając fizyczne złącze **OpenTherm / R-Bus** całkowicie wolne dla dotychczasowego termostatu pokojowego.

---

## 📖 Dokumentacja i Interaktywny Konfigurator YAML

- **📘 [Pełna dokumentacja i instrukcja instalacji (GitHub Pages — Polski)](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl)**
  - [Obsługiwane kotły i pompy ciepła (`EHC-*`, `CU-*`, `SCB-*`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl#compatibility)
  - [Wymagania i parowanie BLE](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl#requirements)
  - [Wykrywanie obsługiwanych czujników w 3 krokach (`BDR_SNAPSHOT`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl#discovery-workflow)
  - [Opis konfiguracji (`sensor`, `binary_sensor`, `text_sensor`, `number`, `select`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl#configuration)
  - [FAQ i rozwiązywanie problemów](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl#faq)
- **⚡ [Interaktywny Konfigurator YAML ESPHome (z polskimi nazwami parametrów)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=pl)**
  - Przeglądaj wszystkie **3 092 punkty danych OBD BDR Thermea** (po polsku, niemiecku lub angielsku), filtruj według modelu płyty głównej (`EHC-08`, `CU-08` itd.) lub wklej log `BDR_SNAPSHOT:` z ESPHome, aby wygenerować gotowy kod YAML.
- **🔬 [Specyfikacja protokołu BLE BDR Thermea / De Dietrich (Angielski)](./docs/PROTOCOL_SPECIFICATION.md)**
- **📋 [Historia zmian (Changelog)](./CHANGELOG.md)**

---

## Najważniejsze funkcje

- **100% bezprzewodowo przez BLE:** Zaciski `R-Bus` / `OpenTherm` / `L-Bus` na płycie kotła pozostają całkowicie wolne.
- **Dynamiczne wykrywanie UUID GATT, parowanie PIN i obsługa `GTW-IoT`:**
  - Automatycznie wykrywa uchwyty charakterystyk GATT po 128-bitowych UUID przy połączeniu (`EHC-08`, `EHC-16`, `CU-*`, `GTW-22`, `GTW-IoT`).
  - Obsługuje zarówno **wbudowany moduł BLE (`MK3` / `GTW-22`) z 6-cyfrowym kodem PIN**, jak i **zewnętrzny hub `GTW-IoT` bez kodu PIN** (wystarczy pominąć `pin:` w YAML).
  - Automatyczne odnawianie klucza po usunięciu najstarszego powiązania (limit 3 urządzeń) oraz obsługa 60-sekundowej blokady bramki (`90s → 180s → 300s → 600s`).
  - Konfigurowalny poziom dostępu (`auth_level: 1..4`: `1` = Użytkownik, `2` = Instalator Standard, `3` = Serwis, `4` = OEM/Lab).
- **Odczyt i zapis parametrów:**
  - `sensor`, `binary_sensor` i `text_sensor`: Odczyt temperatur, ciśnienia wody, flag binarnych (`HM012`, `HM013`, `AM015`), statusów tekstowych oraz liczników zużytej i wyprodukowanej energii (kWh).
  - `number` i `select`: Zmiana nastaw liczbowych (np. temperatura komfortowa/eko c.w.u., nastawy zasilania) oraz trybów pracy (`WriteConfirmedRequest 0x04`) z automatycznym pobraniem zakresów min/max/krok z tabeli `0x502B`.
- **Szybki odczyt grupowy (`GetList`) i `Auto-Skip`:** Rozwiązuje kody menu (`AM027`, `DM001`, `AC005`, `DP070`) na etapie kompilacji, grupuje zapytania w pakiety `GetListRequest (0x08)` i automatycznie pomija nieobsługiwane parametry (`Auto-Skip`).

---

## Kompatybilność sprzętowa (Przetestowane vs. Nieprzetestowane)

- **✅ Fizycznie przetestowane i zweryfikowane:**
  - **De Dietrich Strateo R32** (płyta sterująca `EHC-08`, wbudowane BLE `01.00` z 6-cyfrowym kodem PIN, protokół RUB `02.01`).
  - **Remeha Elga Ace All-in-One** (hybrydowa pompa ciepła `EHC-15` przez zewnętrzny **Remeha Home Hub `GTW-IoT`** w trybie bez PIN).
- **⚠️ Kompatybilność teoretyczna / nieprzetestowana (wspólna platforma MK3 BLE / CAN-IP):**
  - Inne urządzenia BDR Thermea wyposażone w **moduł MK3 HMI / BLE** (`EHC-04`, `EHC-05`, `EHC-07`, `EHC-09`, `EHC-14`, `CU-08` / `CU-GH08`, `CU-09`, `CU-16`, `CU-13`, `CU-OH02`, `CU-HW01`, `CU-EB`, `EEC-01 / SCB-*` marek **De Dietrich**, **Baxi**, **Remeha**, **Brötje**, **Chappée** oraz **Oertli**) używają tego samego serwisu BLE GATT oraz słownika `obd_catalog.json` w oficjalnej aplikacji serwisowej, ale **NIE zostały fizycznie przetestowane przez autora**.

---

## Szybki start (Quick Start)

Zobacz [`example.yaml`](./example.yaml) oraz **[pełną dokumentację](https://d-lebed.github.io/esphome-bdr-ble/?lang=pl#quick-start)**:

```yaml
external_components:
  - source: github://d-lebed/esphome-bdr-ble@v1.7.0
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

## Wyłączenie odpowiedzialności i informacje prawne

Ten projekt jest **niezależnym, nieoficjalnym opracowaniem inżynierii wstecznej**, stworzonym wyłącznie w celach edukacyjnych oraz zapewnienia interoperacyjności pomiędzy urządzeniami grzewczymi grupy **BDR Thermea** a systemami **Home Assistant** / **ESPHome**.

- **Przetestowano wyłącznie na jednym modelu:** Wszystkie testy przeprowadzono wyłącznie na pojedynczej pompie ciepła **De Dietrich Strateo R32 (`EHC-08`)** należącej do autora. Pozostałe modele wymienione w katalogu nie były testowane.
- **Ryzyko utraty gwarancji:** Podłączanie nieautoryzowanego sprzętu/oprogramowania lub uwierzytelnianie z uprawnieniami serwisowymi/instalatorskimi (`auth_level: 4`) może zostać zapisane w pamięci sterownika i **może skutkować utratą gwarancji producenta lub instalatora**.
- **Ryzyko związane z zapisem parametrów:** Zapisywanie parametrów (`number:` / `select:`) modyfikuje ustawienia sterownika w pamięci nieulotnej (EEPROM/Flash). Zapis nieprawidłowych wartości lub zbyt częsty zapis z poziomu automatyzacji może doprowadzić do zużycia pamięci EEPROM lub niebezpiecznej pracy instalacji grzewczej.
- **Brak gwarancji i wyłączenie odpowiedzialności:** Oprogramowanie jest dostarczane w stanie **„TAK JAK JEST” („AS IS”)**, bez jakiejkolwiek gwarancji. **W żadnym wypadku autor ani współtwórcy nie ponoszą odpowiedzialności za jakiekolwiek szkody bezpośrednie, pośrednie ani następcze** (w tym uszkodzenie kotła/pompy ciepła, zalanie, zamarznięcie instalacji, brak ogrzewania/c.w.u. lub utratę gwarancji). **Korzystasz z oprogramowania wyłącznie na własne ryzyko.**
- **Brak powiązań i znaki towarowe:** Projekt nie jest powiązany, autoryzowany ani sponsorowany przez **BDR Thermea Group B.V.**, **De Dietrich Thermique**, **Baxi**, **Remeha**, **August Brötje GmbH**, **Chappée** ani **Oertli**. Wszystkie nazwy marek i produktów należą do ich właścicieli i zostały użyte wyłącznie w celach informacyjnych.
- **Prawo do interoperacyjności i licencja:** Opublikowano zgodnie z **Dyrektywą 2009/24/WE**, **art. 75 polskiej Ustawy o prawie autorskim i prawach pokrewnych** oraz wyrokiem **TSUE w sprawie C-406/10**. Kod źródłowy jest udostępniany na licencji [Apache License 2.0](./LICENSE).
