#pragma once
#include "esphome.h"
#include "esphome/components/ble_client/ble_client.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace esphome {
namespace litra {

static const char *TAG = "litra.light";
static const uint16_t LITRA_INPUT_CHAR_HANDLE = 0x18;
static const uint16_t LITRA_INPUT_CCCD_HANDLE = 0x19;
static const uint16_t LITRA_REPORT_11_HANDLE  = 0x22;
static const uint16_t LITRA_HID_CTRL_HANDLE   = 0x29;

class LitraController : public ble_client::BLEClientNode, public Component {
public:
  LitraController(ble_client::BLEClient *parent) {
    this->set_ble_client_parent(parent);
    parent->register_ble_node(this);
  }

  static LitraController *instance(ble_client::BLEClient *parent) {
    static LitraController *inst = new LitraController(parent);
    return inst;
  }

  void setup() override {
    ESP_LOGI(TAG, "Litra BLE Controller initialized");
  }

  void loop() override {
    if (this->tx_queue_.empty()) return;
    if (millis() - this->last_tx_ms_ < 25) return;

    if (this->parent_ == nullptr || this->parent_->get_conn_id() == 0xFFFF) {
      this->tx_queue_.clear();
      return;
    }

    auto next_pkt = this->tx_queue_.front();
    this->tx_queue_.erase(this->tx_queue_.begin());
    this->last_tx_ms_ = millis();

    esp_ble_gattc_write_char(
        this->parent_->get_gattc_if(),
        this->parent_->get_conn_id(),
        LITRA_REPORT_11_HANDLE,
        next_pkt.size(),
        next_pkt.data(),
        ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE);
  }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) override {
    switch (event) {
      case ESP_GATTC_OPEN_EVT: {
        if (param->open.status == ESP_GATT_OK) {
          esp_ble_set_encryption(param->open.remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
        }
        break;
      }
      case ESP_GATTC_SEARCH_CMPL_EVT: {
        esp_ble_gattc_register_for_notify(gattc_if, this->parent_->get_remote_bda(), LITRA_INPUT_CHAR_HANDLE);
        uint8_t notify_en[2] = {0x01, 0x00};
        esp_ble_gattc_write_char_descr(
            gattc_if, param->search_cmpl.conn_id, LITRA_INPUT_CCCD_HANDLE,
            sizeof(notify_en), notify_en, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
        uint8_t exit_suspend[1] = {0x01};
        esp_ble_gattc_write_char(
            gattc_if, param->search_cmpl.conn_id, LITRA_HID_CTRL_HANDLE,
            sizeof(exit_suspend), exit_suspend, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
        break;
      }
      case ESP_GATTC_DISCONNECT_EVT: {
        this->tx_queue_.clear();
        this->last_on_state_ = false;
        break;
      }
      default:
        break;
    }
  }

  void update_light(float brightness_level, float color_temp_fraction) {
    if (this->parent_ == nullptr || this->parent_->get_conn_id() == 0xFFFF) return;

    this->tx_queue_.clear();

    if (brightness_level <= 0.001f) {
      if (this->last_on_state_) {
        enqueue_command(0x04, 0x1C, 0x00);
        this->last_on_state_ = false;
      }
      return;
    }

    if (!this->last_on_state_) {
      enqueue_command(0x04, 0x1C, 0x01);
      this->last_on_state_ = true;
    }

    // Map 0.0-1.0 brightness to Litra Beam's 20-250 lumen range
    uint8_t bri = (uint8_t)(20.0f + (brightness_level * 230.0f));
    bri = std::max<uint8_t>(20, std::min<uint8_t>(250, bri));
    enqueue_brightness(0x04, 0x4C, bri);

    // color_temp_fraction: 0.0 = min_mireds (6500K), 1.0 = max_mireds (2700K)
    // Litra Beam hardware only accepts exact 100K increments (2700, 2800 ... 6500)
    float raw_kelvin = 6500.0f - (color_temp_fraction * (6500.0f - 2700.0f));
    uint16_t kelvin = (uint16_t)(std::round(raw_kelvin / 100.0f) * 100.0f);
    kelvin = std::max<uint16_t>(2700, std::min<uint16_t>(6500, kelvin));
    enqueue_temperature(0x04, 0x9C, kelvin);
  }

private:
  std::vector<std::array<uint8_t, 19>> tx_queue_;
  uint32_t last_tx_ms_ = 0;
  bool last_on_state_ = false;

  void enqueue_command(uint8_t feature, uint8_t function, uint8_t value) {
    std::array<uint8_t, 19> payload = {0xFF, feature, function, value};
    this->tx_queue_.push_back(payload);
  }

  void enqueue_brightness(uint8_t feature, uint8_t function, uint8_t brightness) {
    std::array<uint8_t, 19> payload = {0xFF, feature, function, 0x00, brightness};
    this->tx_queue_.push_back(payload);
  }

  void enqueue_temperature(uint8_t feature, uint8_t function, uint16_t kelvin) {
    std::array<uint8_t, 19> payload = {0xFF, feature, function, (uint8_t)(kelvin >> 8), (uint8_t)(kelvin & 0xFF)};
    this->tx_queue_.push_back(payload);
  }
};

} // namespace litra
} // namespace esphome
