# Componente BLE ESPHome para Baxi / De Dietrich / BDR Thermea / Remeha / Brötje (`bdr_ble`)

[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-38bdf8?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/?lang=es)
[![Interactive YAML Configurator](https://img.shields.io/badge/configurator-Interactive%20YAML%20Builder-22c55e?style=flat-square)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=es)
[![Release](https://img.shields.io/github/v/release/d-lebed/esphome-bdr-ble?style=flat-square)](https://github.com/d-lebed/esphome-bdr-ble/releases)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-support-FFDD00?style=flat-square&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/lebiadz)

🌐 **Idioma:** [English](./README.md) | [Deutsch](./README.de.md) | [Français](./README.fr.md) | [Nederlands](./README.nl.md) | [Polski](./README.pl.md) | [Italiano](./README.it.md) | **Español**

Un **componente externo de ESPHome (`bdr_ble`)** para monitorizar y controlar bombas de calor (aerotermia), calderas de condensación y calentadores termodinámicos del grupo **BDR Thermea** (**Baxi**, **De Dietrich**, **Remeha**, **Brötje**, **Chappée**, **Oertli**) en **Home Assistant** mediante **Bluetooth Low Energy (BLE)**.

Se comunica directamente con la **interfaz Bluetooth integrada MK3 / EHC-08 / CU-GH08** del equipo (la misma interfaz BLE utilizada por las aplicaciones oficiales *Baxi Service Tool*, *De Dietrich PRO* y *Remeha Smart Service*) usando cualquier placa **ESP32** estándar (`ESP32`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`) — **sin pasarela propietaria `GTW-08` ni cableado**, dejando los bornes **OpenTherm / R-Bus** libres para tu termostato de ambiente.

---

## 📖 Documentación y Configurador YAML Interactivo

- **📘 [Documentación completa y Guía de instalación (GitHub Pages — Español)](https://d-lebed.github.io/esphome-bdr-ble/?lang=es)**
  - [Calderas y Aerotermia compatibles (`EHC-*`, `CU-*`, `SCB-*`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=es#compatibility)
  - [Requisitos y Emparejamiento BLE](https://d-lebed.github.io/esphome-bdr-ble/?lang=es#requirements)
  - [Detección de sensores en 3 pasos (`BDR_SNAPSHOT`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=es#discovery-workflow)
  - [Referencia de configuración (`sensor`, `binary_sensor`, `text_sensor`, `number`, `select`)](https://d-lebed.github.io/esphome-bdr-ble/?lang=es#configuration)
  - [FAQ y Solución de problemas](https://d-lebed.github.io/esphome-bdr-ble/?lang=es#faq)
- **⚡ [Configurador YAML ESPHome Interactivo (con nombres de parámetros en español)](https://d-lebed.github.io/esphome-bdr-ble/configurator.html?lang=es)**
  - Explora los **3.092 puntos de datos OBD de BDR Thermea**, filtra por modelo de placa (`EHC-08`, `CU-08`, etc.) o pega tu log `BDR_SNAPSHOT:` desde ESPHome para generar código YAML listo para usar.
- **🔬 [Especificación del protocolo BLE BDR Thermea / De Dietrich (Inglés)](./docs/PROTOCOL_SPECIFICATION.md)**
- **📋 [Changelog](./CHANGELOG.md)**

---

## Características principales

- **100 % inalámbrico por BLE:** Los terminales físicos `R-Bus` / `OpenTherm` / `L-Bus` quedan completamente libres.
- **Descubrimiento dinámico de UUID GATT, Emparejamiento PIN y soporte `GTW-IoT`:**
  - Resuelve dinámicamente todos los handles GATT por UUID de 128 bits al conectar (`EHC-08`, `EHC-16`, `CU-*`, `GTW-22`, `GTW-IoT`).
  - Soporta tanto el **módulo BLE integrado (`MK3` / `GTW-22`) con PIN de 6 dígitos** como el **hub externo `GTW-IoT` sin PIN** (omite simplemente `pin:` en el YAML).
  - Recuperación automática ante claves eliminadas (límite de 3 dispositivos emparejados) y espera inteligente ante el bloqueo de 60 segundos (`90s → 180s → 300s → 600s`).
  - Nivel de acceso configurable (`auth_level: 1..4`: `1` = Usuario, `2` = Instalador Estándar, `3` = SAT/Servicio, `4` = Lab/OEM).
- **Soporte de Lectura y Escritura:**
  - `sensor`, `binary_sensor` y `text_sensor`: Temperaturas, presión de agua, estados binarios (`HM012`, `HM013`, `AM015`), estados de texto y contadores de energía consumida/producida (kWh).
  - `number` y `select`: Modificación de consignas numéricas (ej. consigna ACS confort/eco, curva de calefacción) y modos de funcionamiento (`WriteConfirmedRequest 0x04`) con límites min/max/paso obtenidos automáticamente de `0x502B`.
- **Lectura por lotes (`GetList`) y Auto-Skip:** Resuelve códigos de menú (`AM027`, `DM001`, `AC005`, `DP070`) al compilar, agrupa lecturas mediante `GetListRequest (0x08)` y omite automáticamente los puntos no soportados (`Auto-Skip`).

---

## Inicio rápido (Quick Start)

Consulta [`example.yaml`](./example.yaml) y la **[Documentación completa](https://d-lebed.github.io/esphome-bdr-ble/?lang=es#quick-start)**:

```yaml
external_components:
  - source: github://d-lebed/esphome-bdr-ble@v1.6.0
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

## Descargo de responsabilidad y Aviso legal

Este proyecto es un **desarrollo independiente y no oficial de ingeniería inversa**, creado únicamente con fines educativos y de interoperabilidad entre equipos **BDR Thermea Group** y **Home Assistant** / **ESPHome**.

- **Probado en un solo modelo:** Todas las pruebas se realizaron exclusivamente en una bomba de calor **De Dietrich Strateo R32 (`EHC-08`)** propiedad del autor.
- **Riesgo de pérdida de garantía:** Conectar hardware/software de terceros o autenticarse con permisos de instalador (`auth_level: 4`) puede quedar registrado en la placa y **anular la garantía del fabricante o instalador**.
- **Sin garantía y Uso bajo tu propio riesgo:** Este software se proporciona **"TAL CUAL" ("AS IS")**, sin garantía de ningún tipo. **El uso de este software es totalmente bajo tu propia responsabilidad.** Código bajo licencia [Apache License 2.0](./LICENSE).
