#pragma once

#include <driver/i2c_master.h>
#include "esphome/components/i2c/i2c_bus.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome::onx_touch_bus {

// ESP-IDF stores the clock on each device handle. The expander and the
// touchscreen share the physical bus and its ESP-IDF transaction lock.
class TouchBus : public Component, public i2c::I2CBus {
 public:
  void set_bus(i2c::InternalI2CBus *bus) { this->bus_ = bus; }
  float get_setup_priority() const override { return setup_priority::BUS - 1; }

  void setup() override {
    i2c_master_bus_handle_t controller{};
    auto result = i2c_master_get_bus_handle(this->bus_->get_port(), &controller);
    if (result == ESP_OK) {
      i2c_device_config_t device{};
      device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
      device.device_address = ADDRESS;
      device.scl_speed_hz = 400000;
      result = i2c_master_bus_add_device(controller, &device, &this->touch_);
    }
    if (result != ESP_OK)
      ESP_LOGW("onx_touch_bus", "400 kHz touch setup failed; using the shared bus: %s", esp_err_to_name(result));
  }

  i2c::ErrorCode write_readv(uint8_t address, const uint8_t *write, size_t write_size, uint8_t *read,
                            size_t read_size) override {
    if (address != ADDRESS || !this->touch_ || (write_size == 0 && read_size == 0))
      return this->bus_->write_readv(address, write, write_size, read, read_size);

    esp_err_t result;
    if (write_size && read_size) {
      result = i2c_master_transmit_receive(this->touch_, write, write_size, read, read_size, 10);
    } else if (write_size) {
      result = i2c_master_transmit(this->touch_, write, write_size, 10);
    } else {
      result = i2c_master_receive(this->touch_, read, read_size, 10);
    }
    if (result == ESP_OK) return i2c::ERROR_OK;
    if (result == ESP_ERR_TIMEOUT) return i2c::ERROR_TIMEOUT;
    return i2c::ERROR_NOT_ACKNOWLEDGED;
  }

 private:
  static constexpr uint8_t ADDRESS = 0x15;
  i2c::InternalI2CBus *bus_{nullptr};
  i2c_master_dev_handle_t touch_{nullptr};
};

}  // namespace esphome::onx_touch_bus
