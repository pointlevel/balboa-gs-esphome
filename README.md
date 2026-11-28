# balboa-gs-esphome

ESPHome external component for **Balboa GS "DZ" spa controllers with the VL801D (Serial Deluxe) topside panel**, for example GS510DZ. It reads the panel's display bus and sends button presses, so the spa shows up in Home Assistant with temperatures, status, a target-temperature control, a heat-mode selector and buttons.

The component sits between the panel and the controller on the panel's RJ45 cable. The spa's own electronics are not modified.

> [!WARNING]
> The spa controller enclosure contains **mains voltage (230/400 V)**. Always switch off the spa at the breaker before opening it or plugging anything in. Work on mains-powered equipment at your own risk, and follow your local electrical regulations. Using this project may void your spa's warranty.

> [!NOTE]
> This is an independent hobby project. It is not affiliated with or endorsed by Balboa Water Group. It has been tested on **one GS510DZ with a VL801D panel**. Other GS DZ systems with VL801D/VL802D panels use the same bus, but have not been verified.

## Features

| Entity | Type | Notes |
|---|---|---|
| Water temperature | sensor | Only accepted after it has been shown steadily for 3 s, so the set point is never mistaken for it |
| Target temperature | sensor | Read when the panel shows the set point |
| Display | text sensor | What the panel shows, e.g. `37.5C` or `12:34` |
| Heater, pump 1, pump 2, blower, light, filtration, filter cycle 1/2, standard mode, economy mode | binary sensors | Decoded from the display bus |
| Panel connection | binary sensor | Off if no frames have been received for 2 s |
| Raw bits 29–71 | text sensor | Diagnostics for mapping unknown bits |
| Target temperature control | `number` (template) | Steps the set point in 0.5 °C increments |
| Temp up / down | buttons (template) | One press = one 0.5 °C step. The extra "wake" press the panel needs is sent automatically |
| Heat mode | `select` (template) | Standard / Economy / Sleep, verified against the status bits |
| Light, pump 1, pump 2, blower, Mode, Time | buttons (template) | Same as pressing the panel button |
| Estimated power and energy | template sensors | Optional, calculated from the status bits (see the example) |

## Hardware

- **Wemos D1 mini** (ESP8266) or D1 mini Pro.
- **Interface board** from [MagnusPer/Balboa-GS510SZ](https://github.com/MagnusPer/Balboa-GS510SZ) (`extras/PCB layout`). The board works unchanged with the VL801D. It has two voltage dividers (5 V → 3.3 V) for the clock and data lines, and two optocouplers that let the ESP and the panel both drive the button line.
- Two RJ45 connections: **RJ1 "Display"** to the panel, **RJ2 "Control system"** to the controller's panel port. You can solder RJ45 jacks, or solder patch-cable pigtails straight into the board (a male plug towards the controller and a female coupler for the panel's plug).

The ESP is powered from the panel cable (5 V on pin 7). If it reboots when Wi-Fi connects, power it separately and only share ground.

### Panel cable pinout

| RJ45 pin | Signal | D1 mini |
|---|---|---|
| 3 | Button data (via optocouplers) | D8 (GPIO15) |
| 4 | GND | G |
| 5 | Display data | D2 (GPIO4), via divider |
| 6 | Clock | D1 (GPIO5), via divider |
| 7 | +5 V | 5V |
| 1, 2, 8 | Not used, passed straight through | – |

Pin 1 is on the **right** when you hold a plug with the latch facing down and the tip pointing at you. It is on the **left** when you look into a jack or coupler with the latch slot facing down.

With a network cable tester across the two cables, pins 1, 2, 4, 5, 6, 7 and 8 should pass straight through and pin 3 should be open, because it goes through the optocouplers. A simple tester may still fail to show pins 4–6, since those are joined through the voltage dividers. Check them with a multimeter instead: about 0 Ω end to end, and about 3 kΩ from pin 5 and pin 6 to pin 4.

## Installation

1. Add the component to your ESPHome configuration:

   ```yaml
   external_components:
     - source: github://pointlevel/balboa-gs-esphome@v0.1.0
       components: [balboa_gs]
   ```

   Pinning to a release tag is recommended. To follow `main` instead, use `@main` together with `refresh: 0s`. Otherwise ESPHome keeps a cached copy and you may build an old version.

2. Start from [`example/spabad.yaml`](example/spabad.yaml), which is a complete configuration with all entities, and [`example/secrets.yaml.example`](example/secrets.yaml.example). Entity names in the example are in Swedish; rename them as you like.

3. Flash the first time over USB, for example **Install → Manual download → Factory format** in the ESPHome Device Builder, then [web.esphome.io](https://web.esphome.io). After that, update over the air. Do not connect USB and the spa at the same time.

### First start against the spa

1. Switch off the spa at the breaker, then connect the board.
2. Set `probe_setpoint: false` so the ESP sends no button presses on its own.
3. Switch on and check that *Panel connection* is on and that the water temperature and display text match the panel.
4. Test a button, for example the light.
5. Press Warm on the panel. *Target temperature* should update while *Water temperature* stays put.
6. Set `probe_setpoint: true` again.

## Configuration

```yaml
balboa_gs:
  id: spa
  clock_pin: D1
  data_pin: D2
  button_pin: D8
  # optional:
  button_repeat: 1
  probe_setpoint: true
  setpoint_flag_bit: 40
  water_temperature:
    name: "Water temperature"
  # ...see the example for all sensors
```

| Option | Default | Description |
|---|---|---|
| `clock_pin`, `data_pin` | required | Inputs for the clock (RJ45 pin 6) and display data (pin 5) lines |
| `button_pin` | required | Output that drives the button line through the optocoupler |
| `button_repeat` | `1` | How many frames each button press is sent. Raise to 2–3 if presses are missed |
| `probe_setpoint` | `true` | About 10 s after start, press Temp up once to read the set point. This does not change the temperature |
| `setpoint_flag_bit` | `40` | Display bit that marks "set point shown". `0` disables it, leaving only blink detection |

Sensor keys: `water_temperature`, `target_temperature`, `display`, `raw_bits`, `connected`, `heater`, `pump1`, `pump2`, `blower`, `light`, `filtration`, `filter1`, `filter2`, `standard_mode`, `eco_mode`, `temp_menu`, `time_menu`. Each accepts the normal ESPHome sensor options.

### Methods for lambdas

| Method | Description |
|---|---|
| `step_temperature(int8_t dir)` | Changes the set point by one 0.5 °C step (`1` or `-1`). Sends the extra wake press only when needed |
| `set_target_temperature(float t)` | Steps to the given set point (26–40 °C) |
| `get_target_temperature()`, `get_water_temperature()` | Last known values, `NAN` if unknown |
| `set_heat_mode(std::string)`, `get_heat_mode()` | `"Standard"`, `"Economy"` or `"Sleep"` |
| `press_button(uint8_t code)` | Raw press. Constants: `BTN_MODE`, `BTN_TEMP_UP`, `BTN_TEMP_DOWN`, `BTN_LIGHT`, `BTN_PUMP1`, `BTN_PUMP2`, `BTN_BLOWER`, `BTN_TIME` in the `esphome::balboa_gs` namespace |

## How it works

The panel and controller communicate over a synchronous serial bus. The controller sends a burst of **75 clock pulses about every 10 ms**. Bits 0–71 carry display data from the controller. During bits 72–75 the panel, or this component, pulls the button line to send a 4-bit button code.

- Bits 1–28 hold four 7-segment characters (MSB first, last character first).
- Status bits used (VL801D): 30 time menu · 36/37 filter cycle 1/2 · 39 heater · 40 set point shown · 42 blower · 43 filtration · 47 light · 48/49 pump 1/2 · 59 standard mode · 60 economy mode · 65 Mode/Prog (1 = not in the menu).
- Button codes: Mode `1000` · Pump 1 `1001` · Pump 2 `1010` · Light `1011` · Time `1100` · Blower `1101` · Temp up `1110` · Temp down `1111`.

The set point is recognised by bit 40. As a fallback, a value that appears both before and after a display blink also counts as the set point. A frame is used only after it has been received identically three times in a row.

The **heat mode** selector presses Temp up (which only wakes the panel), then Mode/Prog. It then waits about 4.5 s for the panel to leave the menu and checks bits 59/60. This repeats until the mode matches, at most 4 times. Sleep is assumed when neither the standard nor the economy bit is set.

## Limitations

- Pump 1 speed (low/high) cannot be read from the bus.
- Time of day and filter-cycle times cannot be set from Home Assistant; use the panel. GS systems lose the clock after a power cut.
- The estimated power in the example assumes fixed wattages. Adjust the substitutions to your spa's rating plates, or use a real energy meter.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `has no member named 'step_temperature'` (or similar) when building | ESPHome is using a cached old version. Use a newer tag, or `refresh: 0s` with `@main` |
| Water temperature jumps to the set point | `setpoint_flag_bit` does not match your panel. Watch *Raw bits* while the panel shows the set point |
| Button presses are sometimes missed | Set `button_repeat: 2` or `3` |
| Panel connection keeps dropping | Weak 5 V supply or a poor RJ45 connection |
| Panel blank or showing errors after connecting | Switch off and restore the original cable. Check the wiring: pin numbering, and RJ1 to the panel, RJ2 to the controller |

## Credits

This component is a new implementation, but the protocol knowledge comes from:

- [MagnusPer/Balboa-GS510SZ](https://github.com/MagnusPer/Balboa-GS510SZ): the original library, protocol analysis and interface board.
- [micke85/Balboa-GS510DZ-with-panel-VL801D](https://github.com/micke85/Balboa-GS510DZ-with-panel-VL801D): the VL801D bit map and button codes.
- [Shuraxxx](https://github.com/Shuraxxx/-Balboa-GS523DZ-with-panel-VL801D-DeluxeSerie--MQTT): the first VL801D adaptation.

## License

[MIT](LICENSE). Balboa, GS510DZ and VL801D are trademarks of their respective owners.
