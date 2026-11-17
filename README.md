# Logitech Litra Beam BLE Bridge for Home Assistant (ESPHome)

A standalone **ESP32-S3** Bluetooth Low Energy (BLE) bridge built with **ESPHome** (ESP-IDF framework) that exposes a **Logitech Litra Beam** key light natively to **Home Assistant** with full Power, Brightness (20–250 lumens), and Color Temperature (2700K–6500K) controls—without requiring a host PC, USB tether, or Logi Tune software.

---

## Architecture Overview

1. **Dynamic MAC Discovery (`esp32_ble_tracker`):** Scans for advertisements named `"Litra Beam"` and dynamically updates the `ble_client` target MAC address (handling random static BLE address rotation).
2. **BLE Pairing & HID-over-GATT (HoGP) Handshake (`litra_ble.h`):**
   - Clears stale BLE bond keys on ESP32 boot (`priority: 200`) to prevent pairing deadlocks after power cycles or light resets.
   - Initiates link encryption (`ESP_BLE_SEC_ENCRYPT_NO_MITM`) upon connection.
   - Subscribes to Input Report notifications (`0x18` / CCCD `0x19`) and sends Exit Suspend (`0x01`) to the HID Control Point (`0x29`), mimicking standard OS HID drivers so the Litra Beam's HID++ firmware accepts output reports.
3. **Paced HID++ 2.0 Command Queue:**
   - Queues outgoing packets and transmits them at **25ms intervals** using `ESP_GATT_WRITE_TYPE_NO_RSP` to prevent ATT queue congestion and 3-second Link Supervision Timeouts (`reason 0x08`).
4. **Native Home Assistant UI (`color_temperature` platform):**
   - Uses ESPHome's native `color_temperature` light platform backed by two `template` float outputs that invoke `LitraController::instance(...)->update_light(...)`.

---

## Reverse-Engineered Protocol & GATT Map

### Discovered GATT Handles (Logitech Litra Beam)
| Characteristic / Descriptor | Handle | Notes |
| :--- | :--- | :--- |
| **Input Report (`0x2A4D`)** | `0x18` | HID++ notifications / ACKs from light |
| **Input Report CCCD (`0x2902`)** | `0x19` | Must be written `{0x01, 0x00}` after service discovery |
| **Output Report - Short (ID `0x01`)** | `0x1C` | Do not write 19-byte payloads here (returns GATT error `13`) |
| **Output Report - Medium (ID `0x02`)** | `0x1F` | Do not write 19-byte payloads here (returns GATT error `13`) |
| **Output Report - Long HID++ 2.0 (ID `0x11`)** | **`0x22`** | **Target write handle** (Report Ref descriptor at `0x23`) |
| **HID Control Point (`0x2A4C`)** | `0x29` | Write `{0x01}` (Exit Suspend) on connection |

### HID++ 2.0 Payload Structure (Over BLE GATT)
Over BLE GATT, the leading Report ID byte (`0x11`) is implied by writing to handle `0x22` and **must be omitted**, resulting in a **19-byte zero-padded payload**:

```text
[ 0xFF, Feature_Index, Function_ID, Param_0, Param_1, ... 0x00 (padded to 19 bytes) ]
```

* **Feature Index:** `0x04` (Illumination / Litra Beam control)
* **Power (`0x1C`):**
  * Off: `0xFF, 0x04, 0x1C, 0x00`
  * On: `0xFF, 0x04, 0x1C, 0x01`
* **Brightness (`0x4C`):**
  * Format: `0xFF, 0x04, 0x4C, 0x00, <lumens>`
  * Valid Range: `20` (`0x14`) to `250` (`0xFA`) lumens.
* **Color Temperature (`0x9C`):**
  * Format: `0xFF, 0x04, 0x9C, <kelvin_high_byte>, <kelvin_low_byte>`
  * Valid Range: `2700` (`0x0A8C`) to `6500` (`0x1964`) Kelvin.
  * **Hardware Constraint:** The Litra Beam firmware **only accepts exact 100K increments** (`2700`, `2800`, `2900` ... `6500`). Non-rounded Kelvin values return an HID++ error (`0xFF` in byte 2) and are ignored.

---

## Repository Structure

```text
├── litra-beam-controller.yaml   # Main ESPHome device configuration
├── litra_ble.h                  # Custom C++ BLE GATT & HID++ 2.0 controller
├── secrets.yaml.example         # Template for Wi-Fi and API encryption secrets
└── README.md                    # This documentation
```

---

## Setup & Installation

1. **Prepare Files:**
   Place both `litra-beam-controller.yaml` and `litra_ble.h` in your ESPHome configuration directory (e.g., `/config/esphome/`).
2. **Configure Secrets:**
   Ensure your `secrets.yaml` defines:
   * `wifi_ssid`
   * `wifi_password`
   * `hotspot_password`
   * `api_encryption_key`
3. **Put Litra Beam into Pairing Mode:**
   Hold the Bluetooth pairing button on the back of the Litra Beam until the status LED blinks rapidly.
4. **Compile & Flash:**
   * **Note:** Must use `framework: type: esp-idf` on an **ESP32-S3** (or ESP32 with sufficient RAM for simultaneous Wi-Fi + BLE GATT client).
   * Run `esphome run litra-beam-controller.yaml` (or click **Install** in the ESPHome dashboard).
5. **Home Assistant Integration:**
   * Add the discovered **Litra Beam Controller** device under **Settings > Devices & services > ESPHome**.
   * Click the **Litra Beam** entity name to open the detail pop-up containing the Power toggle, Brightness slider, and Color Temperature (`2700 K` – `6500 K`) slider.

---

## Operational Notes & Caveats

* **USB Power vs. Battery Sleep:** Keep the Litra Beam connected to USB power for instant responsiveness. When running solely on battery power, the Litra Beam puts its BLE radio into deep sleep after inactivity and must be woken by tapping its physical power button.
* **Single Host Limitation:** The Litra Beam supports only one active BLE host. Unpair or close Logi Tune on nearby PCs/phones so they do not compete with the ESP32-S3 for the connection.
* **Re-Pairing After Factory Reset:** If you factory-reset the Litra Beam, reboot the ESP32-S3 once so its `on_boot` routine clears the old bond keys and establishes a fresh pairing.
* **One-Way State Sync:** Physical button presses on the back of the light are not currently parsed back into Home Assistant; sending any command from Home Assistant overrides and resyncs the state.

---

## Future Enhancement Ideas

* **Two-Way State Sync:** Parse incoming `ESP_GATTC_NOTIFY_EVT` packets on handle `0x18` to sync physical button presses (power, brightness steps, color temp steps) back to `id(litra_beam_light)` in Home Assistant.
* **BLE Connection Binary Sensor:** Expose `this->parent_->connected()` via a template `binary_sensor` to monitor link health in Home Assistant.
