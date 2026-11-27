"""ESPHome-komponent för Balboa GS-system (DZ) med panel VL801D.

Läser panelens klock- och displaylinje och kan "trycka" på knappar via
knapplinjen, på samma sätt som MagnusPer/Balboa-GS510SZ och
Shuraxxx-forken för VL801D.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import binary_sensor, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_HEAT,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

CODEOWNERS = []
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]

CONF_CLOCK_PIN = "clock_pin"
CONF_DATA_PIN = "data_pin"
CONF_BUTTON_PIN = "button_pin"
CONF_BUTTON_REPEAT = "button_repeat"
CONF_PROBE_SETPOINT = "probe_setpoint"
CONF_SETPOINT_FLAG_BIT = "setpoint_flag_bit"

CONF_WATER_TEMPERATURE = "water_temperature"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_DISPLAY = "display"
CONF_RAW_BITS = "raw_bits"
CONF_CONNECTED = "connected"

# Binärsensorer: nyckel -> bitposition i displayramen (VL801D, Shuraxxx-mappning)
# Bitkarta enligt micke85/Balboa-GS510DZ-with-panel-VL801D
BINARY_BITS = {
    "time_menu": 30,
    "filter1": 36,
    "filter2": 37,
    "heater": 39,
    "temp_menu": 40,
    "blower": 42,
    "filtration": 43,
    "light": 47,
    "pump1": 48,
    "pump2": 49,
    "standard_mode": 59,
    "eco_mode": 60,
}
RUNNING_KEYS = {"blower", "filtration", "pump1", "pump2"}

balboa_gs_ns = cg.esphome_ns.namespace("balboa_gs")
BalboaGS = balboa_gs_ns.class_("BalboaGS", cg.Component)

_binary_schemas = {}
for key in BINARY_BITS:
    kwargs = {}
    if key in RUNNING_KEYS:
        kwargs["device_class"] = DEVICE_CLASS_RUNNING
    elif key == "heater":
        kwargs["device_class"] = DEVICE_CLASS_HEAT
    _binary_schemas[cv.Optional(key)] = binary_sensor.binary_sensor_schema(**kwargs)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BalboaGS),
            cv.Required(CONF_CLOCK_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_DATA_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_BUTTON_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_BUTTON_REPEAT, default=1): cv.int_range(min=1, max=20),
            cv.Optional(CONF_PROBE_SETPOINT, default=True): cv.boolean,
            cv.Optional(CONF_SETPOINT_FLAG_BIT, default=40): cv.int_range(min=0, max=71),
            cv.Optional(CONF_WATER_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TARGET_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
            ),
            cv.Optional(CONF_DISPLAY): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_RAW_BITS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            **_binary_schemas,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_clock_pin(await cg.gpio_pin_expression(config[CONF_CLOCK_PIN])))
    cg.add(var.set_data_pin(await cg.gpio_pin_expression(config[CONF_DATA_PIN])))
    cg.add(var.set_button_pin(await cg.gpio_pin_expression(config[CONF_BUTTON_PIN])))
    cg.add(var.set_button_repeat(config[CONF_BUTTON_REPEAT]))
    cg.add(var.set_probe_setpoint(config[CONF_PROBE_SETPOINT]))
    cg.add(var.set_setpoint_flag_bit(config[CONF_SETPOINT_FLAG_BIT]))

    if CONF_WATER_TEMPERATURE in config:
        s = await sensor.new_sensor(config[CONF_WATER_TEMPERATURE])
        cg.add(var.set_water_temperature_sensor(s))
    if CONF_TARGET_TEMPERATURE in config:
        s = await sensor.new_sensor(config[CONF_TARGET_TEMPERATURE])
        cg.add(var.set_target_temperature_sensor(s))
    if CONF_DISPLAY in config:
        s = await text_sensor.new_text_sensor(config[CONF_DISPLAY])
        cg.add(var.set_display_sensor(s))
    if CONF_RAW_BITS in config:
        s = await text_sensor.new_text_sensor(config[CONF_RAW_BITS])
        cg.add(var.set_raw_bits_sensor(s))
    if CONF_CONNECTED in config:
        s = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(var.set_connected_sensor(s))

    for key, bit in BINARY_BITS.items():
        if key in config:
            s = await binary_sensor.new_binary_sensor(config[key])
            cg.add(var.add_bit_sensor(bit, s))
