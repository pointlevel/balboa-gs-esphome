#include "balboa_gs.h"

#include <cctype>
#include <cmath>
#include <cstring>

#include "esphome/core/log.h"

namespace esphome {
namespace balboa_gs {

static const char *const TAG = "balboa_gs";

// ---------------------------------------------------------------------------
// Avbrottsrutin: körs på varje flank på klocklinjen (D1).
// Stigande flank: läs en displaybit, eller skriv en knappbit (bit 72-75).
// Fallande flank: släpp knapplinjen.
// ---------------------------------------------------------------------------
void IRAM_ATTR BalboaGSStore::gpio_intr(BalboaGSStore *s) {
  const uint32_t now = micros();
  const bool clock_high = s->clock.digital_read();

  if (now - s->last_edge_us >= NEW_CYCLE_US) {
    // Lång tystnad = ny cykel börjar. Fånga bara om förra ramen är avkodad.
    s->bit_counter = 0;
    s->capturing = !s->ready;
  }
  s->last_edge_us = now;

  if (!clock_high) {
    s->button.digital_write(false);
    return;
  }

  const uint8_t n = s->bit_counter;

  if (s->pending_button != BTN_NONE && n > DISPLAY_BITS && n <= TOTAL_BITS) {
    const bool bit = (s->pending_button >> (TOTAL_BITS - n)) & 0x01;
    s->button.digital_write(bit);
    if (n == TOTAL_BITS) {
      if (s->repeat_left > 1) {
        s->repeat_left = s->repeat_left - 1;
      } else {
        s->repeat_left = 0;
        s->pending_button = BTN_NONE;
      }
    }
  }

  if (n <= DISPLAY_BITS) {
    if (s->capturing)
      s->buffer[n] = s->data.digital_read() ? 1 : 0;
  } else if (n == TOTAL_BITS) {
    if (s->capturing) {
      s->capturing = false;
      s->ready = true;
      s->frames = s->frames + 1;
    }
  } else if (n > TOTAL_BITS) {
    s->capturing = false;
    s->overflows = s->overflows + 1;
  }

  if (n < 250)
    s->bit_counter = n + 1;
}

// ---------------------------------------------------------------------------

void BalboaGS::setup() {
  this->clock_pin_->setup();
  this->data_pin_->setup();
  this->button_pin_->setup();
  this->button_pin_->digital_write(false);

  this->store_.clock = this->clock_pin_->to_isr();
  this->store_.data = this->data_pin_->to_isr();
  this->store_.button = this->button_pin_->to_isr();

  this->clock_pin_->attach_interrupt(&BalboaGSStore::gpio_intr, &this->store_, gpio::INTERRUPT_ANY_EDGE);

  if (this->connected_sensor_ != nullptr)
    this->connected_sensor_->publish_state(false);
}

void BalboaGS::dump_config() {
  ESP_LOGCONFIG(TAG, "Balboa GS (VL801D):");
  LOG_PIN("  Klocka: ", this->clock_pin_);
  LOG_PIN("  Displaydata: ", this->data_pin_);
  LOG_PIN("  Knappdata: ", this->button_pin_);
  ESP_LOGCONFIG(TAG, "  Knapptryck upprepas: %u ramar", this->button_repeat_);
  ESP_LOGCONFIG(TAG, "  Bit för inställningsläge: %u", this->setpoint_flag_bit_);
  LOG_SENSOR("  ", "Vattentemperatur", this->water_temperature_sensor_);
  LOG_SENSOR("  ", "Måltemperatur", this->target_temperature_sensor_);
  LOG_TEXT_SENSOR("  ", "Display", this->display_sensor_);
}

void BalboaGS::loop() {
  const uint32_t now = millis();

  if (this->store_.ready) {
    uint8_t bits[BUFFER_SIZE];
    for (uint8_t i = 0; i < BUFFER_SIZE; i++)
      bits[i] = this->store_.buffer[i];
    this->store_.ready = false;  // ISR får fånga nästa ram

    this->last_frame_ms_ = now;
    if (this->first_frame_ms_ == 0)
      this->first_frame_ms_ = now;
    if (!this->connected_) {
      this->connected_ = true;
      ESP_LOGI(TAG, "Tar emot data från panelen");
      if (this->connected_sensor_ != nullptr)
        this->connected_sensor_->publish_state(true);
    }

    // Avstudsning: kräv samma ram STABLE_FRAMES gånger i rad
    if (memcmp(bits, this->last_frame_, DISPLAY_BITS + 1) == 0) {
      if (this->stable_count_ < 255)
        this->stable_count_++;
    } else {
      memcpy(this->last_frame_, bits, DISPLAY_BITS + 1);
      this->stable_count_ = 1;
    }
    if (this->stable_count_ == STABLE_FRAMES)
      this->decode_frame_(bits);
  }

  if (this->connected_ && now - this->last_frame_ms_ > 2000) {
    this->connected_ = false;
    ESP_LOGW(TAG, "Ingen data från panelen på 2 s (ramar: %u, överskridna: %u)", this->store_.frames,
             this->store_.overflows);
    if (this->connected_sensor_ != nullptr)
      this->connected_sensor_->publish_state(false);
  }

  // Ta reda på måltemperaturen en gång efter start: ett tryck på Temp upp
  // visar inställd temperatur utan att ändra den.
  if (this->probe_setpoint_ && !this->setpoint_probed_ && this->connected_ && std::isnan(this->target_temperature_) &&
      now - this->first_frame_ms_ > 10000) {
    this->setpoint_probed_ = true;
    ESP_LOGI(TAG, "Läser av inställd temperatur (ett tryck på Temp upp)");
    this->press_button(BTN_TEMP_UP);
  }

  this->check_water_candidate_();
  this->handle_temperature_steps_();
}

void BalboaGS::check_water_candidate_() {
  if (!this->candidate_valid_)
    return;
  const uint32_t now = millis();
  if (now - this->candidate_first_ms_ < WATER_CONFIRM_MS)
    return;
  if (this->last_blank_ms_ != 0 && now - this->last_blank_ms_ < WATER_CONFIRM_MS)
    return;
  if (this->last_temp_button_ms_ != 0 && now - this->last_temp_button_ms_ < OWN_PRESS_WINDOW_MS)
    return;
  const float t = this->candidate_value_;
  this->candidate_valid_ = false;
  if (t != this->water_temperature_) {
    this->water_temperature_ = t;
    ESP_LOGD(TAG, "Vattentemperatur %.1f", t);
    if (this->water_temperature_sensor_ != nullptr)
      this->water_temperature_sensor_->publish_state(t);
  }
}

void BalboaGS::publish_target_(float t) {
  if (t == this->target_temperature_)
    return;
  this->target_temperature_ = t;
  ESP_LOGD(TAG, "Inställd temperatur %.1f", t);
  if (this->target_temperature_sensor_ != nullptr)
    this->target_temperature_sensor_->publish_state(t);
}

void BalboaGS::press_button(uint8_t code) {
  if (!this->connected_) {
    ESP_LOGW(TAG, "Ingen kontakt med panelen, knapptryck ignoreras");
    return;
  }
  ESP_LOGD(TAG, "Knapptryck 0x%X", code);
  if (code == BTN_TEMP_UP || code == BTN_TEMP_DOWN)
    this->last_temp_button_ms_ = millis();
  this->store_.repeat_left = this->button_repeat_;
  this->store_.pending_button = code;
}

void BalboaGS::set_target_temperature(float temperature) {
  if (std::isnan(this->target_temperature_)) {
    ESP_LOGW(TAG, "Måltemperaturen är inte känd ännu, läser av den. Försök igen om en stund.");
    this->press_button(BTN_TEMP_UP);
    return;
  }
  temperature = roundf(temperature * 2.0f) / 2.0f;  // panelen stegar 0,5 °C
  const float diff = temperature - this->target_temperature_;
  if (fabsf(diff) < 0.25f)
    return;
  this->temp_direction_ = diff > 0 ? 1 : -1;
  // Ett väcktryck om panelen inte redan är i inställningsläget, sedan ett tryck per 0,5 °C
  this->temp_presses_left_ = (this->in_temp_menu_() ? 0 : 1) + (uint8_t) lroundf(fabsf(diff) * 2.0f);
  this->last_temp_press_ms_ = 0;
  ESP_LOGI(TAG, "Ändrar måltemperatur %.1f -> %.1f (%u tryck)", this->target_temperature_, temperature,
           this->temp_presses_left_);
}

bool BalboaGS::in_temp_menu_() const {
  // Panelen lämnar inställningsläget ca 3 s efter senaste trycket
  const bool recent_press = this->last_temp_button_ms_ != 0 && millis() - this->last_temp_button_ms_ < 2000;
  return this->temp_menu_flag_ || recent_press;
}

void BalboaGS::step_temperature(int8_t direction) {
  if (!this->connected_) {
    ESP_LOGW(TAG, "Ingen kontakt med panelen, knapptryck ignoreras");
    return;
  }
  const bool in_menu = this->in_temp_menu_();
  if (this->temp_presses_left_ > 0 && this->temp_direction_ == direction) {
    this->temp_presses_left_++;  // flera snabba tryck i rad läggs på kön
  } else {
    this->temp_direction_ = direction > 0 ? 1 : -1;
    this->temp_presses_left_ = in_menu ? 1 : 2;
    this->last_temp_press_ms_ = 0;
  }
  ESP_LOGD(TAG, "Temp %s, %s", direction > 0 ? "upp" : "ner", in_menu ? "redan i inställningsläge" : "väcker panelen först");
}

void BalboaGS::handle_temperature_steps_() {
  if (this->temp_presses_left_ == 0 || this->store_.pending_button != BTN_NONE)
    return;
  const uint32_t now = millis();
  if (this->last_temp_press_ms_ != 0 && now - this->last_temp_press_ms_ < TEMP_PRESS_INTERVAL_MS)
    return;
  this->last_temp_press_ms_ = now;
  this->press_button(this->temp_direction_ > 0 ? BTN_TEMP_UP : BTN_TEMP_DOWN);
  this->temp_presses_left_--;
}

void BalboaGS::decode_frame_(const uint8_t *bits) {
  // Fyra 7-segmentstecken: bit 1-7, 8-14, 15-21, 22-28 (MSB först)
  uint8_t seg[4] = {0, 0, 0, 0};
  for (uint8_t i = 0; i < 4; i++) {
    for (uint8_t b = 0; b < 7; b++)
      seg[i] = (seg[i] << 1) | (bits[1 + i * 7 + b] & 0x01);
  }
  // Tecknen visas i omvänd ordning: bit 22-28 är det första tecknet
  const char c1 = lookup_char_(seg[3]);
  const char c2 = lookup_char_(seg[2]);
  const char c3 = lookup_char_(seg[1]);
  const char c4 = lookup_char_(seg[0]);

  const uint32_t now = millis();
  const bool blank = (seg[0] | seg[1] | seg[2] | seg[3]) == 0;
  std::string display;

  if (blank) {
    // Spara värdet som visades precis före blinket
    this->value_before_blank_ = this->prev_was_temp_ ? this->last_value_ : NAN;
    this->last_blank_ms_ = now;
    this->prev_was_blank_ = true;
    this->prev_was_temp_ = false;
    this->candidate_valid_ = false;
  }

  const bool time_menu = bits[BIT_TIME_MENU] != 0;
  const bool mode_prog_ok = bits[BIT_MODE_PROG] != 0;
  const bool temp_menu = this->setpoint_flag_bit_ > 0 && bits[this->setpoint_flag_bit_] != 0;
  this->temp_menu_flag_ = temp_menu && mode_prog_ok;

  if (time_menu && !blank) {
    display = {c1, c2, ':', c3, c4};
    this->prev_was_blank_ = false;
    this->prev_was_temp_ = false;
    this->candidate_valid_ = false;
  } else if (seg[0] == 0) {
    if (!blank) {
      display = {c1, c2, c3, c4};
      this->prev_was_blank_ = false;
      this->prev_was_temp_ = false;
    }
  } else {
    display = {c1, c2, '.', c3, c4};
    const bool ok = (c1 == ' ' || isdigit(c1)) && isdigit(c2) && isdigit(c3);
    if (ok) {
      const float t = 10.0f * (c1 == ' ' ? 0 : c1 - '0') + (c2 - '0') + 0.1f * (c3 - '0');
      // Samma värde före och efter ett blink = inställd temperatur (extra skydd)
      const bool blink_confirmed = this->prev_was_blank_ && t == this->value_before_blank_ &&
                                   now - this->last_blank_ms_ < BLINK_WINDOW_MS;
      if (!mode_prog_ok || t < 10.0f) {
        this->candidate_valid_ = false;  // menyvärde, inte en temperatur
      } else if (temp_menu || blink_confirmed) {
        this->publish_target_(t);
        this->candidate_valid_ = false;
      } else if (!this->candidate_valid_ || t != this->candidate_value_) {
        this->candidate_value_ = t;
        this->candidate_first_ms_ = now;
        this->candidate_valid_ = true;
      }
      this->last_value_ = t;
      this->prev_was_temp_ = true;
    } else {
      this->prev_was_temp_ = false;
    }
    this->prev_was_blank_ = false;
  }

  // Tom display (blinkar) publiceras inte
  if (display.find_first_not_of(" .") != std::string::npos && display != this->last_display_) {
    this->last_display_ = display;
    if (this->display_sensor_ != nullptr)
      this->display_sensor_->publish_state(display);
  }

  for (auto &bs : this->bit_sensors_)
    bs.sensor->publish_state(bits[bs.bit] != 0);  // binärsensorer publicerar bara vid ändring

  if (this->raw_bits_sensor_ != nullptr) {
    // Bit 29-71 som 0/1 i grupper om tio, för att kartlägga okända bitar
    std::string raw;
    raw.reserve(56);
    for (uint8_t i = 29; i <= DISPLAY_BITS; i++) {
      raw.push_back(bits[i] ? '1' : '0');
      if ((i - 29) % 10 == 9 && i != DISPLAY_BITS)
        raw.push_back(' ');
    }
    if (raw != this->last_raw_bits_) {
      this->last_raw_bits_ = raw;
      this->raw_bits_sensor_->publish_state(raw);
    }
  }
}

char BalboaGS::lookup_char_(uint8_t s) {
  switch (s) {
    case 0b0000000: return ' ';
    case 0b1111110: return '0';
    case 0b0110000: return '1';
    case 0b1101101: return '2';
    case 0b1111001: return '3';
    case 0b0110011: return '4';
    case 0b1011011: return '5';
    case 0b1011111: return '6';
    case 0b1110000: return '7';
    case 0b1111111: return '8';
    case 0b1110011: return '9';
    case 0b1110111: return 'A';
    case 0b1001110: return 'C';
    case 0b1001111: return 'E';
    case 0b1011110: return 'G';
    case 0b0110111: return 'H';
    case 0b0111100: return 'J';
    case 0b0001110: return 'L';
    case 0b1010100: return 'M';
    case 0b1110110: return 'N';
    case 0b1101011: return 'Q';
    case 0b1100110: return 'R';
    case 0b0111110: return 'U';
    case 0b0101010: return 'W';
    case 0b1111101: return 'a';
    case 0b0011111: return 'b';
    case 0b0001101: return 'c';
    case 0b0111101: return 'd';
    case 0b1101111: return 'e';
    case 0b1000111: return 'f';
    case 0b0010111: return 'h';
    case 0b0000100: return 'i';
    case 0b0000001: return 'j';
    case 0b1010111: return 'k';
    case 0b0000110: return 'l';
    case 0b0010100: return 'm';
    case 0b0010101: return 'n';
    case 0b0011101: return 'o';
    case 0b1100111: return 'p';
    case 0b0000101: return 'r';
    case 0b0001111: return 't';
    case 0b0011100: return 'u';
    case 0b0111011: return 'y';
    default: return '-';
  }
}

}  // namespace balboa_gs
}  // namespace esphome
