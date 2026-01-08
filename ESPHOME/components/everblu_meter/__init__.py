"""
EverBlu Meter ESPHome Component

Reads water/gas meter data from Itron EverBlu Cyble Enhanced meters
using the RADIAN protocol over 433 MHz with a CC1101 transceiver.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, binary_sensor, time as time_
from esphome.const import (
    CONF_ID,
    CONF_FREQUENCY,
    CONF_TIME_ID,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TIMESTAMP,
    STATE_CLASS_TOTAL_INCREASING,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_DECIBEL_MILLIWATT,
)

DEPENDENCIES = ["time"]
CODEOWNERS = ["@your-github-username"]

everblu_meter_ns = cg.esphome_ns.namespace("everblu_meter")
EverbluMeterComponent = everblu_meter_ns.class_("EverbluMeterComponent", cg.PollingComponent)

# Configuration keys
CONF_METER_YEAR = "meter_year"
CONF_METER_SERIAL = "meter_serial"
CONF_METER_TYPE = "meter_type"
CONF_GAS_VOLUME_DIVISOR = "gas_volume_divisor"
CONF_AUTO_SCAN = "auto_scan"
CONF_READING_SCHEDULE = "reading_schedule"
CONF_READ_HOUR = "read_hour"
CONF_READ_MINUTE = "read_minute"
CONF_TIMEZONE_OFFSET = "timezone_offset"
CONF_AUTO_ALIGN_TIME = "auto_align_time"
CONF_AUTO_ALIGN_MIDPOINT = "auto_align_midpoint"
CONF_MAX_RETRIES = "max_retries"
CONF_RETRY_COOLDOWN = "retry_cooldown"

# Sensor configuration keys
CONF_VOLUME = "volume"
CONF_BATTERY = "battery"
CONF_COUNTER = "counter"
CONF_RSSI = "rssi"
CONF_RSSI_PERCENTAGE = "rssi_percentage"
CONF_LQI = "lqi"
CONF_LQI_PERCENTAGE = "lqi_percentage"
CONF_TIME_START = "time_start"
CONF_TIME_END = "time_end"
CONF_STATUS = "status"
CONF_ERROR = "error"
CONF_RADIO_STATE = "radio_state"
CONF_TIMESTAMP = "timestamp"
CONF_ACTIVE_READING = "active_reading"
CONF_TOTAL_ATTEMPTS = "total_attempts"
CONF_SUCCESSFUL_READS = "successful_reads"
CONF_FAILED_READS = "failed_reads"

# Meter types
METER_TYPE_WATER = "water"
METER_TYPE_GAS = "gas"

# Reading schedules
SCHEDULE_MONDAY_FRIDAY = "Monday-Friday"
SCHEDULE_MONDAY_SATURDAY = "Saturday"
SCHEDULE_MONDAY_SUNDAY = "Sunday"
SCHEDULE_EVERYDAY = "Everyday"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EverbluMeterComponent),
            cv.Required(CONF_METER_YEAR): cv.int_range(min=0, max=99),
            cv.Required(CONF_METER_SERIAL): cv.uint32_t,
            cv.Required(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            cv.Optional(CONF_METER_TYPE, default=METER_TYPE_WATER): cv.enum(
                {METER_TYPE_WATER: False, METER_TYPE_GAS: True}
            ),
            cv.Optional(CONF_GAS_VOLUME_DIVISOR, default=100): cv.int_range(min=1, max=1000),
            cv.Optional(CONF_FREQUENCY, default=433.82): cv.float_range(min=300.0, max=928.0),
            cv.Optional(CONF_AUTO_SCAN, default=True): cv.boolean,
            cv.Optional(CONF_READING_SCHEDULE, default=SCHEDULE_MONDAY_FRIDAY): cv.string,
            cv.Optional(CONF_READ_HOUR, default=10): cv.int_range(min=0, max=23),
            cv.Optional(CONF_READ_MINUTE, default=0): cv.int_range(min=0, max=59),
            cv.Optional(CONF_TIMEZONE_OFFSET, default=0): cv.int_range(min=-720, max=720),
            cv.Optional(CONF_AUTO_ALIGN_TIME, default=True): cv.boolean,
            cv.Optional(CONF_AUTO_ALIGN_MIDPOINT, default=True): cv.boolean,
            cv.Optional(CONF_MAX_RETRIES, default=10): cv.int_range(min=1, max=50),
            cv.Optional(CONF_RETRY_COOLDOWN, default="1h"): cv.positive_time_period_milliseconds,
            # Sensors
            cv.Optional(CONF_VOLUME): sensor.sensor_schema(
                unit_of_measurement="L",
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:water",
            ),
            cv.Optional(CONF_BATTERY): sensor.sensor_schema(
                unit_of_measurement="years",
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery",
            ),
            cv.Optional(CONF_COUNTER): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:counter",
            ),
            cv.Optional(CONF_RSSI): sensor.sensor_schema(
                unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_RSSI_PERCENTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:signal",
            ),
            cv.Optional(CONF_LQI): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:wifi-strength-4",
            ),
            cv.Optional(CONF_LQI_PERCENTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:wifi-strength-4",
            ),
            cv.Optional(CONF_TIME_START): sensor.sensor_schema(
                unit_of_measurement="h",
                accuracy_decimals=0,
                icon="mdi:clock-start",
            ),
            cv.Optional(CONF_TIME_END): sensor.sensor_schema(
                unit_of_measurement="h",
                accuracy_decimals=0,
                icon="mdi:clock-end",
            ),
            cv.Optional(CONF_TOTAL_ATTEMPTS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:counter",
            ),
            cv.Optional(CONF_SUCCESSFUL_READS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:check-circle",
            ),
            cv.Optional(CONF_FAILED_READS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:alert-circle",
            ),
            # Text sensors
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:information",
            ),
            cv.Optional(CONF_ERROR): text_sensor.text_sensor_schema(
                icon="mdi:alert",
            ),
            cv.Optional(CONF_RADIO_STATE): text_sensor.text_sensor_schema(
                icon="mdi:radio-tower",
            ),
            cv.Optional(CONF_TIMESTAMP): text_sensor.text_sensor_schema(
                device_class=DEVICE_CLASS_TIMESTAMP,
                icon="mdi:clock",
            ),
            # Binary sensors
            cv.Optional(CONF_ACTIVE_READING): binary_sensor.binary_sensor_schema(
                icon="mdi:radio",
            ),
        }
    )
    .extend(cv.polling_component_schema("24h"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set basic configuration
    cg.add(var.set_meter_year(config[CONF_METER_YEAR]))
    cg.add(var.set_meter_serial(config[CONF_METER_SERIAL]))
    cg.add(var.set_meter_type(config[CONF_METER_TYPE]))
    cg.add(var.set_gas_volume_divisor(config[CONF_GAS_VOLUME_DIVISOR]))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    cg.add(var.set_auto_scan(config[CONF_AUTO_SCAN]))
    cg.add(var.set_reading_schedule(config[CONF_READING_SCHEDULE]))
    cg.add(var.set_read_hour(config[CONF_READ_HOUR]))
    cg.add(var.set_read_minute(config[CONF_READ_MINUTE]))
    cg.add(var.set_timezone_offset(config[CONF_TIMEZONE_OFFSET]))
    cg.add(var.set_auto_align_time(config[CONF_AUTO_ALIGN_TIME]))
    cg.add(var.set_auto_align_midpoint(config[CONF_AUTO_ALIGN_MIDPOINT]))
    cg.add(var.set_max_retries(config[CONF_MAX_RETRIES]))
    cg.add(var.set_retry_cooldown(config[CONF_RETRY_COOLDOWN]))  # Already in ms

    # Link time component
    time_component = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time_component(time_component))

    # Register sensors
    if CONF_VOLUME in config:
        sens = await sensor.new_sensor(config[CONF_VOLUME])
        cg.add(var.set_volume_sensor(sens))
    
    if CONF_BATTERY in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY])
        cg.add(var.set_battery_sensor(sens))
    
    if CONF_COUNTER in config:
        sens = await sensor.new_sensor(config[CONF_COUNTER])
        cg.add(var.set_counter_sensor(sens))
    
    if CONF_RSSI in config:
        sens = await sensor.new_sensor(config[CONF_RSSI])
        cg.add(var.set_rssi_sensor(sens))
    
    if CONF_RSSI_PERCENTAGE in config:
        sens = await sensor.new_sensor(config[CONF_RSSI_PERCENTAGE])
        cg.add(var.set_rssi_percentage_sensor(sens))
    
    if CONF_LQI in config:
        sens = await sensor.new_sensor(config[CONF_LQI])
        cg.add(var.set_lqi_sensor(sens))
    
    if CONF_LQI_PERCENTAGE in config:
        sens = await sensor.new_sensor(config[CONF_LQI_PERCENTAGE])
        cg.add(var.set_lqi_percentage_sensor(sens))
    
    if CONF_TIME_START in config:
        sens = await sensor.new_sensor(config[CONF_TIME_START])
        cg.add(var.set_time_start_sensor(sens))
    
    if CONF_TIME_END in config:
        sens = await sensor.new_sensor(config[CONF_TIME_END])
        cg.add(var.set_time_end_sensor(sens))
    
    if CONF_TOTAL_ATTEMPTS in config:
        sens = await sensor.new_sensor(config[CONF_TOTAL_ATTEMPTS])
        cg.add(var.set_total_attempts_sensor(sens))
    
    if CONF_SUCCESSFUL_READS in config:
        sens = await sensor.new_sensor(config[CONF_SUCCESSFUL_READS])
        cg.add(var.set_successful_reads_sensor(sens))
    
    if CONF_FAILED_READS in config:
        sens = await sensor.new_sensor(config[CONF_FAILED_READS])
        cg.add(var.set_failed_reads_sensor(sens))
    
    # Register text sensors
    if CONF_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_STATUS])
        cg.add(var.set_status_sensor(sens))
    
    if CONF_ERROR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ERROR])
        cg.add(var.set_error_sensor(sens))
    
    if CONF_RADIO_STATE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_RADIO_STATE])
        cg.add(var.set_radio_state_sensor(sens))
    
    if CONF_TIMESTAMP in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TIMESTAMP])
        cg.add(var.set_timestamp_sensor(sens))
    
    # Register binary sensors
    if CONF_ACTIVE_READING in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_ACTIVE_READING])
        cg.add(var.set_active_reading_sensor(sens))
