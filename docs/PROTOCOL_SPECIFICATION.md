# De Dietrich Strateo / BDR Thermea BLE Protocol Specification

Technical specification of the Bluetooth Low Energy (BLE) and `RUB v2.01` (CANopen over BLE) communication protocol observed and verified via live BLE testing against a **De Dietrich Strateo R32 (`EHC-08_1234567890`)** heat pump using ESPHome (ESP32-C6).

---

## 1. Boiler BLE Identity & Advertising

- **BLE MAC Address**: `AA:BB:CC:DD:EE:FF` (`PUBLIC` address type `0`)
- **Advertised Name**: `EHC-08_1234567890` (Appliance code `EHC-08`, serial `1234567890`)
- **Manufacturer Specific Data**: `0x42 0x44 0x52` (`"BDR"` — BDR Thermea Group)
- **Advertised Service UUID**: `f8fc98e4-5919-4a5c-852e-dfe04ad383c0`

---

## 2. Complete GATT Service & Characteristic Table

Reading or writing any characteristic without BLE Gap Bonding returns GATT error `0x05` (`ESP_GATT_INSUF_AUTHENTICATION`) / `0x0F` (`ESP_GATT_INSUF_ENCRYPTION`).

### Service 1: DirectChannel (BLE Data Tunnel)
- **Service UUID**: `f8fc98e4-5919-4a5c-852e-dfe04ad383c0` (start handle `16`, end handle `24`)
  - **`request`** — `496b1b03-cebc-4d59-9c32-14ea88c266f9` (handle `18`, `WRITE` `0x08`)
    Client writes segmented BLE frames here (Client -> Boiler).
  - **`response`** — `ab9af948-fb86-492d-820d-bdea2dcb7ecf` (handle `20`, `NOTIFY` `0x10`, CCCD handle `21`)
    Boiler sends segmented BLE response frames here (Boiler -> Client).
  - **`auxChannel`** — `cff32957-5279-4075-bf3d-8e583d418c0d` (handle `23`, `WRITE | NOTIFY` `0x18`, CCCD handle `24`)
    Secondary/control notifications subscribed during DirectChannel setup.

### Service 2: Error Management
- **Service UUID**: `86a38a67-dede-4a85-a116-c8ac36bf3cb2` (start handle `25`, end handle `28`)
  - **`errorManagement`** — `5b5dcf8d-57f0-455e-b88e-e8e080031032` (handle `27`, `NOTIFY` `0x10`, CCCD handle `28`)
    Notifies 4-byte error frames when a BLE transport or lock error occurs:
    - `[0, 0, 0, 0]`: `Error no error`
    - `[0, 0, 3, 2]`: `Received BLE frame CRC is not valid`
    - `[0, 1, 3, 2]`: `BLE rx buffer overflow`
    - `[1, 1, 3, 2]`: `Timeout while waiting for next BLE segment (10s)`
    - `[0, 2, 3, 2]`: `Received BLE frame payload ID is not valid`
    - `[1, 0, 1, 3]`: `Timeout while waiting for RUB response after sending a RUB request (1s)`
    - `[2, 0, 1, 3]`: `RUB returned a RUB error frame`
    - `[0, 0, 2, 3]`: `Segmented RUB received segment has not the expected segment identifier`
    - `[1, 0, 2, 3]`: `Timeout while waiting for RUB response next segment (400ms)`
    - `[1, 0, 3, 3]`: `Timeout while waiting for RUB request segment ack (400ms)`
    - `[2, 0, 3, 3]`: `Segmented RUB received segment ack has not the expected segment identifier`
    - `[3, 0, 0, 4]`: `BLE services locked`

### Service 3: BLE Service Unlock (Challenge-Response)
- **Service UUID**: `6a37b97e-779d-457f-8182-edf334edd01f` (start handle `29`, end handle `39`)
  - **`identificationInfoRequest` / `unlockTrigger`** — `94e54dc4-f69a-4c0c-8a6f-cc8f3135c665` (handle `31`, `WRITE` `0x08`)
  - **`unlockResponse`** — `69092300-a182-4fea-803b-24ada406da8a` (handle `33`, `NOTIFY` `0x10`, CCCD handle `34`)
  - **`unlockRequest`** — `f1ca5aeb-02cd-44a8-972b-44b6e7250d0b` (handle `36`, `WRITE` `0x08`)
  - **`identificationInfoResponse`** — `e2c47825-6dff-4d79-b74a-5bdc4f4661cf` (handle `38`, `NOTIFY` `0x10`, CCCD handle `39`)

### Service 4: Version & Protocol Info
- **Service UUID**: `ff8bb5b1-bd43-4353-a671-d440fa48e5b4` (start handle `40`, end handle `46`)
  - **`bleSoftwareVersion`** — `a74a724b-0c9d-4731-9df6-096714c6689e` (handle `42`, `READ` `0x02`)
    Value read from boiler: `30 31 2E 30 30` (`"01.00"`)
  - **`rubProtocol`** — `03af035d-4eb8-40ec-80f7-cc83dc7e9971` (handle `44`, `READ` `0x02`)
    Value read from boiler: `30 32 2E 30 31` (`"02.01"`)
  - **`bleProtocol`** — `dfa85876-e12c-4b41-8c85-8b5d36c17737` (handle `46`, `READ` `0x02`)
    Value read from boiler: `30 31 2E 30 33` (`"01.03"`)

---

## 3. Layer 0: BLE Gap Bonding, Dynamic GATT Handles & Gateway Rules

1. **Dynamic GATT Handle Resolution by UUID**:
   While the 128-bit service and characteristic UUIDs are identical across all BDR Thermea BLE modules (`MK3`, `GTW-22`, `GTW-IoT`), attribute handle numbers vary depending on the board's GATT table layout (for example, `DirectChannel` TX/RX are at handles `18` / `20` on `EHC-08`, but at handles `14` / `16` on `EHC-16`). `bdr_ble` queries the GATT attribute table by 128-bit UUID upon `ESP_GATTC_SEARCH_CMPL_EVT` (`resolve_gatt_handles_()`) to dynamically resolve all characteristic and `0x2902` CCCD handles.
2. **Two BLE Hardware Routes (PIN vs. PIN-less `GTW-IoT`)**:
   - **Onboard BLE (`MK3` / `GTW-22` / `EHC-*` / `CU-*`)**: Advertises as `EHC-...` or `CU-...` and requires **BLE LE Secure Connections Passkey Entry** using a 6-digit PIN (displayed on the boiler's HMI screen or printed on the internal BLE sticker). Set `esp32_ble: io_capability: keyboard_only` and specify `pin: !secret strateo_ble_pin` (wrap PINs starting with `0` in quotes in `secrets.yaml`).
   - **External `GTW-IoT` Home Hub**: Advertises as `GTW-IOT_<serial>` and operates over an **unencrypted plain BLE connection** (button pairing on the hub, followed directly by the Service 3 + `RUB v2.01` TEA handshakes). Omit `pin:` under `bdr_ble:` to enable plain BLE mode.
3. **Gateway (`GTW-Blue`) Bonding Slot Eviction & 60s Lockout**:
   - **Maximum 3 Paired Devices**: The internal BLE gateway stores at most **3 bonds** and evicts the oldest when a 4th device pairs. If the ESP32's stored bond is evicted, encryption fails (`0x61`). `bdr_ble` detects this, removes the stale bond from ESP32 NVS via `esp_ble_remove_bond_device()`, and performs a fresh Passkey pairing on the next attempt.
   - **60-Second Lockout Window (`0x56 Repeated Attempts`)**: After a failed pairing attempt, the gateway rejects all pairing requests for **60 seconds**. Retrying every few seconds extends the lockout indefinitely. `bdr_ble` pauses `ble_client` (`set_enabled(false)`) with exponential backoff (`90s → 180s → 300s → 600s`) so the lockout window expires cleanly.
4. **`bluetooth_proxy` Conflict**:
   When `bluetooth_proxy` is enabled and Home Assistant connects to it, `esp32_ble_tracker` switches to RAW advertisement forwarding mode and stops firing local `ESPBTDevice` discovery callbacks required by `ble_client`.
   **Solution**: Do not enable `bluetooth_proxy` on the same ESP32 that runs `ble_client` for the boiler.

---

## 4. Layer 1: TEA Authentication Algorithm (`0x15EFA43F`)

Both the GATT Service 3 BLE Unlock and the CAN-BUS `RUB v2.01` Unlock use the same 32-round TEA (Tiny Encryption Algorithm) core:

- **Constants**:
  - `DELTA = 0x9E3779B9`
  - `KEY_CONST = 0x15EFA43F`
  - `authLevel = 4` (`0x04` for installer/service access)
- **Inputs**:
  - `id1` (`uint32`): `serverSerial` (4 bytes from boiler)
  - `id2` (`uint32`): `timeChallenge` (4 bytes from boiler)
  - `authLevel` (`uint8`): `4`

### Reference Python Implementation of Both Variants

```python
import struct

def bdr_tea_encrypt(auth_level: int, id1: int, id2: int) -> tuple[int, int]:
    """32-round BDR Thermea TEA authentication cipher."""
    v0 = auth_level & 0xFF
    v1 = auth_level & 0xFF
    k0 = 0x15EFA43F
    k1 = id1 & 0xFFFFFFFF
    k2 = id2 & 0xFFFFFFFF
    k3 = auth_level & 0xFF
    delta = 0x9E3779B9
    total = 0

    for _ in range(32):
        total = (total + delta) & 0xFFFFFFFF
        v0 = (v0 + ((((v1 << 4) + k0) ^ (v1 + total)) ^ ((v1 >> 5) + k1))) & 0xFFFFFFFF
        v1 = (v1 + ((((v0 << 4) + k2) ^ (v0 + total)) ^ ((v0 >> 5) + k3))) & 0xFFFFFFFF

    return v0, v1

def compute_ble_gatt_unlock_payload(challenge_8bytes: bytes, auth_level: int = 4) -> bytes:
    """
    Variant A: Little-Endian 9-byte payload
    Written to Service 3 characteristic f1ca5aeb-02cd-44a8-972b-44b6e7250d0b.
    """
    id1, id2 = struct.unpack("<II", challenge_8bytes)
    v0, v1 = bdr_tea_encrypt(auth_level, id1, id2)
    return struct.pack("<BII", auth_level & 0xFF, v0, v1)

def compute_rub_unlock_key(server_serial_4b: bytes, time_challenge_4b: bytes, auth_level: int = 4) -> bytes:
    """
    Variant B: Big-Endian 8-byte AuthorizationKey
    Sent inside RUB unlock frame: 02 01 00 02 02 03 FF 04 <8-byte key>.
    """
    id1 = struct.unpack(">I", server_serial_4b)[0]
    id2 = struct.unpack(">I", time_challenge_4b)[0]
    v0, v1 = bdr_tea_encrypt(auth_level, id1, id2)
    return struct.pack(">II", v0, v1)
```

---

## 5. Complete Connection & Unlock Sequence

### Phase 1: GATT Service 3 Unlock (`6a37b97e-779d-457f-8182-edf334edd01f`)
Verified on live **De Dietrich Strateo (`EHC-08_1234567890`)**:
1. Register for notifications (`esp_ble_gattc_register_for_notify`) in `ESP_GATTC_SEARCH_CMPL_EVT`, and after `ESP_GAP_BLE_AUTH_CMPL_EVT` write `0x0001` (`{0x01, 0x00}`) to CCCDs:
   - `69092300-a182-4fea-803b-24ada406da8a` (handle `33`, CCCD `34`) — **challenge notification**
   - `e2c47825-6dff-4d79-b74a-5bdc4f4661cf` (handle `38`, CCCD `39`) — **unlock status notification**
   - `ab9af948-fb86-492d-820d-bdea2dcb7ecf` (handle `20`, CCCD `21`) — **DirectChannel response**
   - `cff32957-5279-4075-bf3d-8e583d418c0d` (handle `23`, CCCD `24`) — **DirectChannel error / aux**
   - `5b5dcf8d-57f0-455e-b88e-e8e080031032` (handle `27`, CCCD `28`) — **Error management**
2. Write empty payload `b""` (**0 bytes**) to `94e54dc4-f69a-4c0c-8a6f-cc8f3135c665` (handle `31`).
   - **Important ESP-IDF quirk**: `esp_ble_gattc_write_char` rejects `value_len == 0` with `ESP_ERR_INVALID_ARG` (`258`). Call Bluedroid `BTA_GATTC_WriteCharValue(bta_conn_id, 31, 2, 0, &dummy, 0)` directly where `bta_conn_id = (conn_id << 8) | gattc_if`.
3. Receive 8-byte notification `[id1_LE(4B), id2_LE(4B)]` from `69092300-a182-4fea-803b-24ada406da8a` (handle `33`), where `id1` is the lower 4 bytes of the boiler's MAC (`50:12:34:56` -> `56 34 12 50` in LE) and `id2` is the random `timeChallenge`:
   - Example from live boiler: `NOTIFY handle=33 (8B): 56 34 12 50 17 90 8F 86`
4. Compute 9-byte response `compute_ble_gatt_unlock_payload(challenge_8bytes, auth_level=4)` and write it to `f1ca5aeb-02cd-44a8-972b-44b6e7250d0b` (handle `36`):
   - Example from live boiler: `TX handle=36 (9B): 04 01 4A 51 AF F3 ED 1D A9`
5. Receive 1-byte notification `[0x01]` from `e2c47825-6dff-4d79-b74a-5bdc4f4661cf` (handle `38`) (`0x01` = unlocked, `0x00` = NACK).

### Phase 2: BLE DirectChannel Framing & Segmentation (`f8fc98e4-5919-4a5c-852e-dfe04ad383c0`)
Every message sent to `496b1b03-cebc-4d59-9c32-14ea88c266f9` (handle 18) or received from `ab9af948-fb86-492d-820d-bdea2dcb7ecf` (handle 20) uses the following framing:

1. **Message Header**:
   - **Direct CAN/RUB channel mode**:
     `body = [0x01] + payload`
2. **CRC-16/MODBUS Suffix**:
   - Standard CRC-16/MODBUS (`init = 0xFFFF`, reflected polynomial `0xA001`) computed over `body`.
   - Appended as 2 bytes in **Big-Endian** order: `[crc >> 8, crc & 0xFF]`.
   - `frame = body + [crc_hi, crc_lo]`.
3. **BLE Segmentation**:
   - `max_chunk = segmentSize - 4` (minimum `segmentSize` is 5).
   - `frame` is split into `N = ceil(len(frame) / max_chunk)` chunks.
   - Each chunk `i` (`0 <= i < N`) is prefixed with 1 segment index byte:
     - `0xFF` if `i == N - 1` (the **last** or **only** segment),
     - `i` (`0x00, 0x01, ...`) if `i < N - 1`.
   - For a single-segment message:
     `ble_packet = [0xFF] + body + [crc_hi, crc_lo]`.

### Phase 3: RUB v2.01 Handshake over DirectChannel
Once Service 3 is unlocked (when `rubProtocol == "02.01"`):
1. **RUB Identification Info Request**:
   - Send `payload = 02 01 00 02 00 03` (6 bytes) over `DirectChannel` (`496b1b03`).
   - Wrapped single-segment packet on `496b1b03`:
     `body = 01 02 01 00 02 00 03` -> `CRC16_MODBUS(body) = 0x23D6` -> `[0xFF, 0x01, 0x02, 0x01, 0x00, 0x02, 0x00, 0x03, 0x23, 0xD6]`.
   - Receive 14-byte `payload` on `ab9af948`:
     `02 01 00 02 01 03 <serverSerial 4B> <timeChallenge 4B>`.
2. **RUB Unlock Request**:
   - Compute 8-byte `authKey = compute_rub_unlock_key(serverSerial, timeChallenge, auth_level=4)`.
   - Send 16-byte `payload`:
     `02 01 00 02 02 03 FF 04 <authKey 8B>`.
   - Receive 10-byte `payload`:
     `02 01 00 02 03 03 <b6> <status_b7> <b8> <b9>` where `status_b7 == 0x04` (`CAN-BUS Authenticated`).
3. **RUB Parameter Read**:
   - After unlocking, any CANopen datapoint can be queried by sending the 10-byte `payload`:
     `02 01 00 00 00 03 <NodeId> <Index_HI> <Index_LO> <SubIndex>`.
   - It receives the response `02 01 00 00 01 03 <NodeId> <Index_HI> <Index_LO> <SubIndex> <data...>`, where `payload[10..]` is the parameter value.

---

## 6. Layer 4: Direct RUB v2.01 Parameter Read/Write Format

When `rubProtocol == "02.01"`, any CANopen Object Dictionary datapoint `(node_id, index, sub_index)` can be accessed directly over `DirectChannel` (`496b1b03` write / `ab9af948` notify):

### Read Request Payload (10 bytes)
- `byte[0..2]`: `02 01 00` (RUB v2.01 header)
- `byte[3]`: `0x00` (Command group `0x00` = Parameter access)
- `byte[4]`: `0x00` (Command ID `0x00` = **Read Request**)
- `byte[5]`: `0x03` (Marker `0x03`)
- `byte[6]`: `node_id` (`0x01` = main appliance Control Unit)
- `byte[7..8]`: `index` (`uint16` Big-Endian: `(index >> 8) & 0xFF`, `index & 0xFF`)
- `byte[9]`: `sub_index` (`uint8`)

Wrapped into a single-segment BLE packet for `496b1b03-cebc-4d59-9c32-14ea88c266f9` (handle `18`):
```
FF  01  02 01 00 00 00 03 <NodeId> <Index_HI> <Index_LO> <SubIndex>  <CRC16_HI> <CRC16_LO>
```
where `CRC16` is CRC-16/MODBUS (`init = 0xFFFF`, poly `0xA001`) computed over the 11 bytes `01 02 01 00 00 00 03 <NodeId> <Index_HI> <Index_LO> <SubIndex>`, stored Big-Endian (`CRC16_HI = crc >> 8`, `CRC16_LO = crc & 0xFF`).

### Read Response Payload
After desegmenting from `ab9af948-fb86-492d-820d-bdea2dcb7ecf` (handle `20`), verifying the trailing 2-byte CRC-16/MODBUS, and stripping the leading `0x01`, the `payload` has the format:
- **Success (`payload[3] == 0x00 && payload[4] == 0x01`)**:
  `02 01 00 00 01 03 <NodeId> <Index_HI> <Index_LO> <SubIndex> <Data...>`
  where:
  - **`NodeId = 0x01`** is the main boiler/heat-pump Control Unit (`CU`) on De Dietrich Strateo (`EHC-08`).
  - `payload[10..]` (`<Data...>`) contains the raw datapoint bytes in **Little-Endian** order (`U8`/`I8` = 1 byte, `U16`/`I16` = 2 bytes LE, `U32`/`I32` = 4 bytes LE, `Enum` = 1 or 2 bytes LE, `TimeOfDay` = 6 bytes CiA 301 LE `[uint32_t ms_since_midnight, uint16_t days_since_1984]`).
  - For 16-bit signed temperatures (`I16`), `0x8000` (`-32768`) indicates sensor not connected / N/A.
- **Error / SDO Abort (`payload[3] == 0x00 && payload[4] == 0x02`)**:
  `02 01 00 00 02 03 <NodeId> <Index_HI> <Index_LO> <SubIndex> <SDO_Abort_Code_LE(4B)>`

### Write Request & Response Payload
- **Write Confirmed Request (`Function = 0x04`, 10-byte header + `data_len` bytes)**:
  `02 01 00 00 04 03 <NodeId> <Index_HI> <Index_LO> <SubIndex> <Data_LE...>`
  where `<Data_LE...>` is encoded in **Little-Endian** order:
  - `U8`, `I8`: **1 byte** (`raw & 0xFF`)
  - `Enum`: **1 or 2 bytes LE** according to the datapoint's wire length (`dp_len` in `0x502B`)
  - `U16`, `I16`: **2 bytes LE** (`raw & 0xFF`, `(raw >> 8) & 0xFF`)
  - `U32`, `I32`: **4 bytes LE**
  - `VisibleString`: fixed-length UTF-8 byte array null-padded (`0x00`) to `max_len` bytes (truncated safely on UTF-8 codepoint boundaries)
- **Write Positive Ack (`Function = 0x05`, 10 bytes)**:
  `02 01 00 00 05 03 <NodeId> <Index_HI> <Index_LO> <SubIndex>`
- **Write Negative Ack / SDO Abort (`Function = 0x06`, 14 bytes)**:
  `02 01 00 00 06 03 <NodeId> <Index_HI> <Index_LO> <SubIndex> <SDO_Abort_Code_LE(4B)>`

### Live Verified Trace from De Dietrich Strateo (`AA:BB:CC:DD:EE:FF`)
```text
TX handle=31 (0B): <empty>
NOTIFY handle=33 (8B): 56 34 12 50 17 90 8F 86
TX handle=36 (9B): 04 01 4A 51 AF F3 ED 1D A9
NOTIFY handle=38 (1B): 01                                      -> Service 3 Unlocked!

TX handle=18 (10B): FF 01 02 01 00 02 00 03 23 D6              -> RUB IdentificationInfoRequest
NOTIFY handle=20 (18B): FF 01 02 01 00 02 01 03 12 34 56 78 27 3D DA B6 B4 9B
TX handle=18 (20B): FF 01 02 01 00 02 02 03 FF 04 12 49 8E 19 99 0C B8 65 FA 90
NOTIFY handle=20 (14B): FF 01 02 01 00 02 03 03 FF 04 FF 01 54 38 -> CAN-BUS Authenticated (level=4)!

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 44 00 AF D8  -> Read 0x5044:00 (AC005 CH Energy Cons)
NOTIFY handle=20 (18B): FF 01 02 01 00 00 01 03 01 50 44 00 8E 00 00 00 ... -> 0x0000008E = 142 kWh

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 45 00 3F D9  -> Read 0x5045:00 (AC006 DHW Energy Cons)
NOTIFY handle=20 (18B): FF 01 02 01 00 00 01 03 01 50 45 00 64 00 00 00 ... -> 0x00000064 = 100 kWh

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 88 00 AF 8D  -> Read 0x5088:00 (Total Energy Cons)
NOTIFY handle=20 (18B): FF 01 02 01 00 00 01 03 01 50 88 00 F2 00 00 00 ... -> 0x000000F2 = 242 kWh

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 65 00 FF C0  -> Read 0x5065:00 (AC018 Backup Energy Cons)
NOTIFY handle=20 (18B): FF 01 02 01 00 00 01 03 01 50 65 00 9C 00 00 00 ... -> 0x0000009C = 156 kWh

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 1E 00 0F E2  -> Read 0x501E:00 (AM027 Outside Temp)
NOTIFY handle=20 (16B): FF 01 02 01 00 00 01 03 01 50 1E 00 E4 00 ...       -> 0x00E4 * 0.01 = 2.28 °C

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 13 00 9F E6  -> Read 0x5013:00 (AM016 Flow Temp)
NOTIFY handle=20 (16B): FF 01 02 01 00 00 01 03 01 50 13 00 C5 09 ...       -> 0x09C5 * 0.01 = 25.01 °C

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 1A 00 CF E0  -> Read 0x501A:00 (BM000 DHW Temp)
NOTIFY handle=20 (16B): FF 01 02 01 00 00 01 03 01 50 1A 00 62 15 ...       -> 0x1562 * 0.01 = 54.74 °C

TX handle=18 (14B): FF 01 02 01 00 00 00 03 01 50 16 00 CF E5  -> Read 0x5016:00 (AM019 Water Pressure)
NOTIFY handle=20 (15B): FF 01 02 01 00 00 01 03 01 50 16 00 0B ...          -> 0x0B * 0.1 = 1.1 bar
```

### Bulk / Batch Read: `GetListRequest` & `GetListResponse` (`Function = 0x08` / `0x09`)
To read multiple parameters in a single round-trip (up to ~1412–1518 bytes per response), `bdr_ble` uses the **`GetList` (`Function = 0x08`)** command:
- **`GetListRequest` payload** (`requestHeaderSize = 7`, `requestElementSize = 5`, `responseElementHeaderSize = 8`, `maxElements = 100`, `maxResponseSize = 1518`):
  - `byte[0..3]`: `02 00 00 00` (canonical CAN-IP v2.00 `GetListRequest` header)
  - `byte[4]`: `0x08` (`Function = 0x08` = `GetListRequest`)
  - `byte[5]`: `0x00`
  - `byte[6]`: `count` (`uint8` — number of items requested in this batch)
  - Followed by `count` items of **5 bytes** each (`[0x00, node_id, index_hi, index_lo, sub_index]`):
    - `item[0]`: `0x00` (Read function)
    - `item[1]`: `node_id` (`0x01`)
    - `item[2..3]`: `index` (`uint16` Big-Endian)
    - `item[4]`: `sub_index` (`uint8`)
- **`GetListResponse` payload**:
  - `byte[0..5]`: header (`02 01 00 00 09 03`, where `byte[4] = 0x09` is `GetListResponse`)
  - `byte[6]`: `count` (`uint8` — number of returned items)
  - Starting at `byte[7]`, `count` variable-length items:
    - `elem[0]`: `status` (`0x01` = Read Pos Ack, `0x02` = Read Neg Ack / SDO Abort)
    - `elem[1]`: `node_id` (`0x01`)
    - `elem[2..3]`: `index` (`uint16` Big-Endian)
    - `elem[4]`: `sub_index` (`uint8`)
    - `elem[5]`: `0x00`
    - `elem[6..7]`: `data_len` (`uint16` Big-Endian: `(elem[6] << 8) | elem[7]`)
    - `elem[8 .. 8 + data_len - 1]`: `data` (`data_len` bytes in Little-Endian; if `status == 0x02`, 4-byte SDO Abort code)
- **Smart SDO Abort Classification, `Auto-Skip` & Session Recovery**:
  - Permanent unsupported SDO abort codes (`0x06020000` Object does not exist, `0x06090011` Sub-index does not exist, `0x06010001` Write-only) mark the datapoint as `unsupported` (`Auto-Skip`).
  - `GetList`-incompatible SDO abort codes (`0x06040043`, `0x06070010`) switch that datapoint to individual `ReadRequest (0x00)` polling (`single_read_only`).
  - Transient CAN bus timeouts (`0x05040000`, `0x08000000`, `0x08000022`, `0x08000024`) keep the datapoint in the `GetList` batch for the next cycle.
  - If `0x06010000` (`Unsupported access to object`) is returned on a previously working datapoint (or one discovered in `0x502B` at `<= auth_level`), `bdr_ble` automatically re-authenticates the `RUB v2.01` session (`0xFF` broadcast unlock covers all CAN nodes).

---

### Dynamic Appliance Menu Discovery (`0x502B` `varApMenuDescriptor`)
Modern CAN-IP / BLE controllers (`EHC-*`, `CU-GH*`, `CU-OH*`, `CU-HW*`, `SCB-*`) dynamically report their active parameters, live signals, and counters via **`Index = 0x502B` (`varApMenuDescriptor`), `SubIndex = 1..15`**:

- **SubIndex Partitioning & Hardware Access Levels (`auth_level 1..5`)**:
  Each of the three categories (Parameters, Signals, Counters) is split across 5 `SubIndex` slots corresponding to the 5 authorization levels (`level = ((SubIndex - 1) % 5) + 1`):
  - **Level 1 — End User (`SubIndex 1, 6, 11`)**: Safe user-facing parameters (`DP070`, `DP080`, `DP200`, `CP320`, schedules), basic live signals (`AM016`, `AM019`, `AM027`, `BM000`, `DM001`), and primary counters (`AC001`, `AC003`, `AC005`–`AC007`, `AC018`, `AC019`, `AC032`, `DC002`–`DC005`, `PC000`, `PC005`).
  - **Level 2 — Installer Standard (`SubIndex 2, 7, 12`)**: Standard commissioning parameters (`CP000`, `CP230`, `DP120`) and thermal energy delivery counters (`AC002`, `AC004`, `AC008`–`AC010`, `varApTotalEnergyDelivery`).
  - **Level 3 — Installer Special / Service (`SubIndex 3, 8, 13`)**: Advanced heat pump / hydraulic parameters (`HP000`, `DP037`, `DP038`, `DP090`) and detailed service counters (`AC013`, `AC026`, `AC027`, `AC028`, `AC030`, `PC002`, `PC003`).
  - **Level 4 — Lab Testing / OEM (`SubIndex 4, 9, 14`)**: Low-level factory/OEM parameters and diagnostics.
  - **Level 5 — Controls Department Only (`SubIndex 5, 10, 15`)**: R&D reserved (returns empty/NACK on production units).
  - **Hardware Enforcement**: The boiler controller enforces `auth_level` on both reads and writes. If the RUB session is authenticated at `auth_level: 2`, any read or write request for a datapoint in `SubIndex 3, 8, 13` (Level 3) or `SubIndex 4, 9, 14` (Level 4) is rejected by the boiler with `status = 0x02` (`SDO Abort`).
- **Binary Payload Structure**:
  - `byte[0..1]`: `uint16_t count` (Little-Endian — number of 15-byte descriptors in this `SubIndex`)
  - `byte[2..3]`: reserved / flags (`2 bytes`)
  - Followed by `count` descriptors of **15 bytes** each (`total_len = 4 + count * 15`):
    - `desc[0]`: `uint8_t menu_item`
    - `desc[1]`: `uint8_t data_length`
    - `desc[2..3]`: **`uint16_t index`** (Little-Endian OBD Index, e.g. `0x501E`, `0x5044`)
    - `desc[4..7]`: `uint32_t min_value` (Little-Endian)
    - `desc[8..11]`: `uint32_t max_value` (Little-Endian)
    - `desc[12]`: **`uint8_t sub_index`** (OBD SubIndex)
    - `desc[13]`: `uint8_t increment`
    - `desc[14]`: `uint8_t read_write_level`
- Immediately after `RUB v2.01` authentication completes, `bdr_ble` reads `0x1F85:00` (CAN bus node count `N`, when `log_snapshot: true`) on the default node and probes `0x2001:02` (`parApDeviceInfoType`) on the default node and candidate slave nodes `3..min(N, 16)` to log the responding CAN node IDs and hardware type codes, then queries `0x502B:01..15` across all configured CAN `node_id`s (or explicit `snapshot_nodes:` in YAML) and outputs `BDR_SNAPSHOT:` / `BDR_SNAPSHOT[N<id>]:` lines in the ESPHome log so users can paste them into `docs/configurator.html` to filter the catalog to the exact datapoints active on their boiler. Learned `0x502B` `min`/`max`/`step` bounds for `number` entities and `[raw_min..raw_max]` bounds for `select` entities are persisted to ESP32 NVS flash (`ESPPreferenceObject`, unless `device_limits: false` is set) and restored at `setup_priority::DATA` so Home Assistant displays accurate hardware limits immediately after boot, while both `BdrBleNumber::control()` and `BdrBleSelect::control()` reject out-of-range writes locally before transmitting over BLE.

---

## 7. Key De Dietrich Strateo (`EHC-08`) Sensor Datapoints

The complete compiled catalog (`3,092` datapoints, `204` enum tables, `195` Function Groups, and `14` controller board profiles) is stored in [`components/bdr_ble/obd_catalog.json`](../components/bdr_ble/obd_catalog.json).
Note: Configuration parameters (`par...`) are in range `0x2000..0x3FFF`; live measured signals and counters (`var...`, `Class="Signal"` / `Class="Counter"`) are in range `0x4000..0x5FFF`.

| Index | SubIndex | Menu Code | Name | Description | Type | Gain | Unit |
|---|---|---|---|---|---|---|---|
| `0x5013` | `0x00` | `AM016` | `varApTflow` | Appliance flow temperature | `I16` | `0.01` | `°C` |
| `0x5014` | `0x00` | `AM017` | `varApTheatExchanger` | Heat exchanger temperature | `I16` | `0.01` | `°C` |
| `0x5015` | `0x00` | `AM018` | `varApTreturn` | Appliance return temperature | `I16` | `0.01` | `°C` |
| `0x5016` | `0x00` | `AM019` | `varApWaterPressure` | Hydraulic water pressure | `U8` | `0.1` | `bar` |
| `0x501A` | `0x00` | `BM000` | `varApDhwTemperature` | DHW temperature | `I16` | `0.01` | `°C` |
| `0x501B` | `0x00` | `AM024` | `varApPowerActual` | Actual relative power produced | `U16` | `0.01` | `%` |
| `0x501E` | `0x00` | `AM027` | `varApTOutside` | Outside temperature measured | `I16` | `0.01` | `°C` |
| `0x5083` | `0x00` | `AM056` | `varApFlowmeter` | Water flow rate | `U16` | `0.01` | `l/m` |
| `0x500F` | `0x00` | `AM012` | `varApStatus` | Status of appliance | `Enum` | `1` | — |
| `0x5011` | `0x00` | `AM014` | `varApSubStatus` | Sub-status of appliance | `Enum` | `1` | — |
| `0x5012` | `0x00` | `AM015` | `varApSystemPumpRunning` | System pump running | `Enum` | `1` | — |
| `0x4301` | `0x00` | `HM001` | `varHpHeatPumpTF` | Heat pump flow temp after condenser | `I16` | `0.01` | `°C` |
| `0x4302` | `0x00` | `HM002` | `varHpHeatPumpTR` | Heat pump return temp after condenser | `I16` | `0.01` | `°C` |
| `0x4303` | `0x00` | `HM003` | `varHpHeatPumpSetpoint` | System flow temperature setpoint | `I16` | `0.01` | `°C` |
| `0x4308` | `0x00` | `HM008` | `varHpCompressorState` | Compressor operating state | `Enum` | `1` | — |
| `0x4377` | `0x00` | `HM098` | `varHpOutdoorUnitOutsideTemperature` | ODU outdoor air temperature | `I16` | `0.01` | `°C` |
| `0x437A` | `0x00` | `HM099` | `varHpElectricalEnergy` | Electrical energy consumed by HP module | `U32` | `1` | `kWh` |
| `0x4385` | `0x00` | `HM110` | `varHpInterfaceFlowmeter` | Flow rate from ODU | `U16` | `0.01` | `l/m` |
| `0x4393` | `0x00` | `HM106` | `varHpCompressorFrequencyAccurate` | Compressor frequency | `I16` | `1` | `Hz` |
| `0x43B4` | `0x00` | `HM160` | `varHpActualProducedPower` | Generated thermal power from HP | `U16` | `0.1` | `kW` |
| `0x43B5` | `0x00` | `HM161` | `varHpActualConsumedPower` | Consumed electrical power from grid | `U16` | `0.1` | `kW` |
| `0x43C3` | `0x00` | `HM184` | `varHpCOP` | Instantaneous COP from heat pump | `I16` | `0.001` | — |
| `0x5601` | `0x00` | `DM001` | `varDhwTankTemperature` | DHW tank temperature (bottom) | `I16` | `0.01` | `°C` |
| `0x5606` | `0x00` | `DM006` | `varDhwTankTemperatureTop` | DHW tank temperature (top) | `I16` | `0.01` | `°C` |
| `0x5610` | `0x00` | `DM029` | `varDHWTemperatureSetpoint` | DHW tank temperature setpoint | `U16` | `0.01` | `°C` |
| `0x5404` | `0x01` | `CM030` | `varZoneTRoom` | Current room temperature for zone | `I16` | `0.1` | `°C` |
| `0x5405` | `0x01` | `CM040` | `varZoneTflow` | Zone flow water temperature | `I16` | `0.01` | `°C` |
| `0x5408` | `0x01` | `CM070` | `varZoneTemperatureSetpoint` | Zone flow temperature setpoint | `U16` | `0.01` | `°C` |
| `0x5044` | `0x00` | `AC005` | `varApChEnergyConsumption` | Electrical energy consumed for Central Heating | `U32` | `1` | `kWh` |
| `0x5045` | `0x00` | `AC006` | `varApDhwEnergyConsumption` | Electrical energy consumed for DHW | `U32` | `1` | `kWh` |
| `0x5046` | `0x00` | `AC007` | `varApCoolingEnergyConsumption` | Total electrical energy consumed (CH + DHW) | `U32` | `1` | `kWh` |
| `0x50AD` | `0x00` | `AC026` | `varApCtrPumpRunHours` | Electrical backup heater energy consumed | `U32` | `1` | `kWh` |
| `0x5085` | `0x00` | `AC008` | `varApChEnergyDelivery` | Thermal energy delivered for Central Heating | `U32` | `1` | `kWh` |
| `0x5086` | `0x00` | `AC009` | `varApDhwEnergyDelivery` | Thermal energy delivered for DHW | `U32` | `1` | `kWh` |
| `0x507A` | `0x00` | `AC019` | `varApBackupEnergyDelivery` | Thermal energy delivered by electric backup heater | `U32` | `1` | `kWh` |
| `0x5089` | `0x00` | — | `varApTotalEnergyDelivery` | Total thermal energy produced (CH + DHW) | `U32` | `1` | `kWh` |
| `0x502B` | `01..15` | — | `varApMenuDescriptor` | Binary array of 15-byte menu descriptor structs | `OctetString` | — | — |
| `0x582A` | `0x00` | `EM020` | `varApBatteryStatus` | Battery level and status | `Struct` | — | — |


