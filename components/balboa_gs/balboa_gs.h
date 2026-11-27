#pragma once

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace balboa_gs {

// Protokoll för VL801D (Balboa GS "DZ"): 75 klockpulser per cykel,
// bit 0-71 är displaydata, bit 72-75 är knappdata. Cykeln upprepas ca var 10:e ms.
static const uint8_t DISPLAY_BITS = 71;
static const uint8_t TOTAL_BITS = 75;
static const uint8_t BUFFER_SIZE = 76;
static const uint32_t NEW_CYCLE_US = 5000;  // tystnad som markerar ny cykel
static const uint8_t BIT_TIME_MENU = 30;
static const uint8_t BIT_MODE_PROG = 65;  // 1 = inte i Mode/Prog-menyn

// Knappkoder, bit 72 är mest signifikant (från Shuraxxx-forken).
enum ButtonCode : uint8_t {
  BTN_NONE = 0b0000,
  BTN_MODE = 0b1000,
  BTN_TEMP_UP = 0b1110,
  BTN_TEMP_DOWN = 0b1111,
  BTN_LIGHT = 0b1011,
  BTN_PUMP1 = 0b1001,
  BTN_PUMP2 = 0b1010,
  BTN_BLOWER = 0b1101,
  BTN_TIME = 0b1100,
};

struct BalboaGSStore {
  ISRInternalGPIOPin clock;
  ISRInternalGPIOPin data;
  ISRInternalGPIOPin button;

  volatile uint32_t last_edge_us{0};
  volatile uint8_t bit_counter{0};
  volatile bool capturing{false};
  volatile bool ready{false};
  volatile uint8_t buffer[BUFFER_SIZE]{};

  volatile uint8_t pending_button{BTN_NONE};
  volatile uint8_t repeat_left{0};

  volatile uint32_t frames{0};
  volatile uint32_t overflows{0};

  static void gpio_intr(BalboaGSStore *arg);
};

struct BitSensor {
  uint8_t bit;
  binary_sensor::BinarySensor *sensor;
};

class BalboaGS : public Component {
 public:
  void set_clock_pin(InternalGPIOPin *pin) { this->clock_pin_ = pin; }
  void set_data_pin(InternalGPIOPin *pin) { this->data_pin_ = pin; }
  void set_button_pin(InternalGPIOPin *pin) { this->button_pin_ = pin; }
  void set_button_repeat(uint8_t repeat) { this->button_repeat_ = repeat; }
  void set_probe_setpoint(bool probe) { this->probe_setpoint_ = probe; }
  void set_setpoint_flag_bit(uint8_t bit) { this->setpoint_flag_bit_ = bit; }

  void set_water_temperature_sensor(sensor::Sensor *s) { this->water_temperature_sensor_ = s; }
  void set_target_temperature_sensor(sensor::Sensor *s) { this->target_temperature_sensor_ = s; }
  void set_display_sensor(text_sensor::TextSensor *s) { this->display_sensor_ = s; }
  void set_raw_bits_sensor(text_sensor::TextSensor *s) { this->raw_bits_sensor_ = s; }
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void add_bit_sensor(uint8_t bit, binary_sensor::BinarySensor *s) { this->bit_sensors_.push_back({bit, s}); }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Används från YAML (template-knappar och template-number)
  void press_button(uint8_t code);
  void set_target_temperature(float temperature);
  float get_target_temperature() const { return this->target_temperature_; }
  float get_water_temperature() const { return this->water_temperature_; }

 protected:
  void decode_frame_(const uint8_t *bits);
  void handle_temperature_steps_();
  static char lookup_char_(uint8_t segments);

  InternalGPIOPin *clock_pin_{nullptr};
  InternalGPIOPin *data_pin_{nullptr};
  InternalGPIOPin *button_pin_{nullptr};
  uint8_t button_repeat_{1};
  bool probe_setpoint_{true};
  uint8_t setpoint_flag_bit_{40};  // "TempMenu", 0 = av

  BalboaGSStore store_{};

  sensor::Sensor *water_temperature_sensor_{nullptr};
  sensor::Sensor *target_temperature_sensor_{nullptr};
  text_sensor::TextSensor *display_sensor_{nullptr};
  text_sensor::TextSensor *raw_bits_sensor_{nullptr};
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  std::vector<BitSensor> bit_sensors_;

  // Avstudsning: en ram måste se likadan ut flera gånger i rad innan den används
  uint8_t last_frame_[BUFFER_SIZE]{};
  uint8_t stable_count_{0};
  static const uint8_t STABLE_FRAMES = 3;

  float water_temperature_{NAN};
  float target_temperature_{NAN};
  std::string last_display_;
  std::string last_raw_bits_;
  uint32_t last_frame_ms_{0};
  bool connected_{false};

  // Ändring av måltemperatur via knapptryck
  int8_t temp_direction_{0};  // -1 ner, +1 upp
  uint8_t temp_presses_left_{0};
  uint32_t last_temp_press_ms_{0};
  static const uint32_t TEMP_PRESS_INTERVAL_MS = 500;

  uint32_t first_frame_ms_{0};
  bool setpoint_probed_{false};

  // Avgör om displayen visar inställd temperatur:
  //  - panelen blinkar i inställningsläget: värde, släckt, samma värde = inställd temp
  //  - vattentemperaturen godtas först när den visats stadigt i 4 s utan blink
  //    och inte direkt efter egna Temp-tryck
  void check_water_candidate_();
  void publish_target_(float t);
  uint32_t last_blank_ms_{0};
  uint32_t last_temp_button_ms_{0};
  bool candidate_valid_{false};
  bool prev_was_blank_{false};
  bool prev_was_temp_{false};
  float last_value_{NAN};
  float value_before_blank_{NAN};
  float candidate_value_{NAN};
  uint32_t candidate_first_ms_{0};
  static const uint32_t BLINK_WINDOW_MS = 1500;
  static const uint32_t OWN_PRESS_WINDOW_MS = 4000;  // vattentemp godtas inte direkt efter egna tryck
  static const uint32_t WATER_CONFIRM_MS = 3000;
};

}  // namespace balboa_gs
}  // namespace esphome
