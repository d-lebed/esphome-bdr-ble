# Composant BLE ESPHome pour De Dietrich / BDR Thermea / Chappée / Baxi / Remeha (`bdr_ble`)

[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-38bdf8?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr)
[![Interactive YAML Configurator](https://img.shields.io/badge/configurator-Interactive%20YAML%20Builder-22c55e?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=fr)
[![Release](https://img.shields.io/github/v/release/d-lebed/esphome-bdr-ble?style=flat-square)](https://github.com/d-lebed/esphome-bdr-ble/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?style=flat-square&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/lebiadz)

🌐 **Langue :** [English](./README.md) | [Deutsch](./README.de.md) | **Français** | [Nederlands](./README.nl.md) | [Polski](./README.pl.md) | [Italiano](./README.it.md) | [Español](./README.es.md)

Un **composant externe ESPHome (`bdr_ble`)** permettant de surveiller et contrôler les pompes à chaleur, chaudières à condensation et chauffe-eau thermodynamiques du groupe **BDR Thermea** (**De Dietrich**, **Chappée**, **Baxi**, **Remeha**, **Brötje**, **Oertli**) dans **Home Assistant** via **Bluetooth Low Energy (BLE)**.

Communique directement avec l'**interface Bluetooth intégrée MK3 / EHC-08 / CU-GH08** de l'appareil (la même interface BLE que celle utilisée par les applications officielles *De Dietrich PRO*, *Baxi Service Tool* et *Remeha Smart Service*) à l'aide de n'importe quel **ESP32** standard (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **sans passerelle propriétaire `GTW-08` ni câblage**, tout en laissant vos borniers **OpenTherm / R-Bus** libres pour votre thermostat d'ambiance.

---

## 📖 Documentation & Configurateur YAML Interactif

- **📘 [Documentation complète & Guide d'installation (GitHub Pages — Français)](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr)**
  - [Chaudières & Pompes à chaleur supportées (`EHC-*`, `CU-*`, `SCB-*`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr#compatibility)
  - [Prérequis & Appairage BLE](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr#requirements)
  - [Détection des capteurs en 3 étapes (`BDR_SNAPSHOT`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr#discovery-workflow)
  - [Référence de configuration (`sensor`, `binary_sensor`, `text_sensor`, `number`, `select`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr#configuration)
  - [FAQ & Dépannage](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr#faq)
- **⚡ [Configurateur YAML ESPHome Interactif (avec noms des paramètres en français)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=fr)**
  - Parcourez les **3 092 points de données OBD BDR Thermea**, filtrez par modèle de carte (`EHC-08`, `CU-08`, etc.) ou collez votre log `BDR_SNAPSHOT:` depuis ESPHome pour générer du code YAML prêt à copier.
- **🔬 [Spécification du protocole BLE BDR Thermea / De Dietrich (Anglais)](./docs/PROTOCOL_SPECIFICATION.md)**
- **📋 [Changelog](./CHANGELOG.md)**

---

## Points forts

- **100 % sans fil via BLE :** Vos borniers physiques `R-Bus` / `OpenTherm` / `L-Bus` restent totalement libres.
- **Découverte dynamique des UUID GATT, Appairage PIN & support `GTW-IoT` :**
  - Résout dynamiquement tous les handles GATT par UUID 128 bits à la connexion (`EHC-08`, `EHC-16`, `CU-*`, `GTW-22`, `GTW-IoT`).
  - Supporte aussi bien le **module BLE intégré (`MK3` / `GTW-22`) avec code PIN à 6 chiffres** que le **hub externe `GTW-IoT` sans code PIN** (omettez simplement `pin:` dans le YAML).
  - Renouvellement automatique de la clé si l'ESP32 est évincé (limite de 3 appareils appairés) et respect du délai de blocage de 60 secondes (`90s → 180s → 300s → 600s`).
  - Niveau d'accès configurable (`auth_level: 1..4` : `1` = Utilisateur, `2` = Installateur Standard, `3` = SAV/Service, `4` = Labo/OEM).
- **Support Lecture & Écriture :**
  - `sensor`, `binary_sensor` & `text_sensor` : Températures, pression d'eau, états binaires (`HM012`, `HM013`, `AM015`), états texte et compteurs d'énergie consommée/produite (kWh).
  - `number` & `select` : Modification des consignes numériques (ex. consigne ECS confort/éco, loi d'eau) et des modes de fonctionnement (`WriteConfirmedRequest 0x04`) avec récupération automatique des limites min/max/pas depuis `0x502B`.
- **Lecture par lots (`GetList`) & Auto-Skip :** Résout les codes menus (`AM027`, `DM001`, `AC005`, `DP070`) à la compilation, regroupe les lectures via `GetListRequest (0x08)` et ignore automatiquement les points non supportés (`Auto-Skip`).

---

## Démarrage rapide (Quick Start)

Voir [`example.yaml`](./example.yaml) et la **[Documentation complète](https://d-lebed.github.io/esphome-bdr-ble/?lang=fr#quick-start)** :

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

## Avertissement & Limitation de responsabilité

Ce projet est un travail **indépendant et non officiel de rétro-ingénierie**, créé uniquement à des fins éducatives et d'interopérabilité entre les appareils **BDR Thermea Group** et **Home Assistant** / **ESPHome**.

- **Testé sur un seul modèle :** Tous les tests ont été réalisés exclusivement sur une pompe à chaleur **De Dietrich Strateo R32 (`EHC-08`)** appartenant à l'auteur.
- **Risque d'annulation de garantie :** La connexion de matériel/logiciel tiers ou l'authentification avec des droits installateur (`auth_level: 4`) peut être enregistrée par la carte électronique et **annuler votre garantie constructeur ou installateur**.
- **Aucune garantie & Zéro responsabilité :** Ce logiciel est fourni **« TEL QUEL » (« AS IS »)**, sans aucune garantie. **Vous utilisez ce logiciel entièrement à vos propres risques.**
- **Absence d'affiliation :** Non affilié à **BDR Thermea Group B.V.**, **De Dietrich Thermique**, **Chappée**, **Baxi**, **Remeha**, **August Brötje GmbH** ou **Oertli**. Code sous licence [Apache License 2.0](./LICENSE).
