"""
EverBlu Meter ESPHome Component

Reads water/gas meter data from Itron EverBlu Cyble Enhanced meters
using the RADIAN protocol over 433 MHz with a CC1101 transceiver.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, binary_sensor, button, time as time_
from esphome.const import (
    CONF_ID,
    CONF_FREQUENCY,
    CONF_TIME_ID,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TIMESTAMP,
    STATE_CLASS_TOTAL_INCREASING,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_DECIBEL_MILLIWATT,
)

DEPENDENCIES = ["time"]
CODEOWNERS = ["@your-github-username"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "button"]

# Tell ESPHome to include all source files in src/ subdirectories
MULTI_CONF = False

everblu_meter_ns = cg.esphome_ns.namespace("everblu_meter")
EverbluMeterComponent = everblu_meter_ns.class_("EverbluMeterComponent", cg.PollingComponent)
EverbluMeterTriggerButton = everblu_meter_ns.class_("EverbluMeterTriggerButton", button.Button)

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
CONF_INITIAL_READ_ON_BOOT = "initial_read_on_boot"
CONF_DEBUG_CC1101 = "debug_cc1101"
CONF_ADAPTIVE_THRESHOLD = "adaptive_threshold"

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
CONF_HISTORY_JSON = "history_json"
CONF_METER_SERIAL_SENSOR = "meter_serial_sensor"
CONF_METER_YEAR_SENSOR = "meter_year_sensor"
CONF_READING_SCHEDULE_SENSOR = "reading_schedule_sensor"
CONF_READING_TIME_UTC_SENSOR = "reading_time_utc_sensor"
CONF_ACTIVE_READING = "active_reading"
CONF_RADIO_CONNECTED = "radio_connected"
CONF_TOTAL_ATTEMPTS = "total_attempts"
CONF_SUCCESSFUL_READS = "successful_reads"
CONF_FAILED_READS = "failed_reads"
CONF_FREQUENCY_OFFSET = "frequency_offset"
CONF_TUNED_FREQUENCY = "tuned_frequency"
CONF_REQUEST_READING_BUTTON = "request_reading_button"
CONF_FREQUENCY_SCAN_BUTTON = "frequency_scan_button"
CONF_RESET_FREQUENCY_BUTTON = "reset_frequency_button"

# Meter types
METER_TYPE_WATER = "water"
METER_TYPE_GAS = "gas"

# Reading schedules - must match C++ ScheduleManager::isReadingDay(...) string comparisons
SCHEDULE_MONDAY_FRIDAY = "Monday-Friday"
SCHEDULE_MONDAY_SATURDAY = "Monday-Saturday"
SCHEDULE_MONDAY_SUNDAY = "Monday-Sunday"

CONF_GDO0_PIN = "gdo0_pin"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EverbluMeterComponent),
            cv.Required(CONF_METER_YEAR): cv.int_range(min=0, max=99),
            cv.Required(CONF_METER_SERIAL): cv.uint32_t,
            cv.Required(CONF_GDO0_PIN): cv.int_range(min=0, max=39),
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
            cv.Optional(CONF_INITIAL_READ_ON_BOOT, default=False): cv.boolean,
            cv.Optional(CONF_DEBUG_CC1101, default=False): cv.boolean,
            cv.Optional(CONF_ADAPTIVE_THRESHOLD, default=1): cv.int_range(min=1, max=100),
            # Sensors
            cv.Optional(CONF_VOLUME): sensor.sensor_schema(
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_BATTERY): sensor.sensor_schema(
                unit_of_measurement="months",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery-clock",
            ),
            cv.Optional(CONF_COUNTER): sensor.sensor_schema(
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:counter",
            ),
            cv.Optional(CONF_RSSI): sensor.sensor_schema(
                unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:signal",
            ),
            cv.Optional(CONF_RSSI_PERCENTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:signal-cellular-3",
            ),
            cv.Optional(CONF_LQI): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:signal",
            ),
            cv.Optional(CONF_LQI_PERCENTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:signal-cellular-outline",
            ),
            cv.Optional(CONF_TIME_START): text_sensor.text_sensor_schema(
                icon="mdi:clock-start",
            ),
            cv.Optional(CONF_TIME_END): text_sensor.text_sensor_schema(
                icon="mdi:clock-end",
            ),
            cv.Optional(CONF_TOTAL_ATTEMPTS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:counter",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_SUCCESSFUL_READS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:check-circle",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_FAILED_READS): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:alert-circle",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_FREQUENCY_OFFSET): sensor.sensor_schema(
                unit_of_measurement="kHz",
                accuracy_decimals=3,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:sine-wave",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_TUNED_FREQUENCY): sensor.sensor_schema(
                unit_of_measurement="MHz",
                accuracy_decimals=6,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:radio-tower",
                entity_category="diagnostic",
            ),
            # Text sensors
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:information",
            ),
            cv.Optional(CONF_ERROR): text_sensor.text_sensor_schema(
                icon="mdi:alert",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_RADIO_STATE): text_sensor.text_sensor_schema(
                icon="mdi:radio-tower",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_TIMESTAMP): text_sensor.text_sensor_schema(
                device_class=DEVICE_CLASS_TIMESTAMP,
                icon="mdi:clock",
            ),
            cv.Optional(CONF_HISTORY_JSON): text_sensor.text_sensor_schema(
                icon="mdi:history",
            ),
            cv.Optional(CONF_METER_SERIAL_SENSOR): text_sensor.text_sensor_schema(
                icon="mdi:barcode",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_METER_YEAR_SENSOR): text_sensor.text_sensor_schema(
                icon="mdi:calendar",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_READING_SCHEDULE_SENSOR): text_sensor.text_sensor_schema(
                icon="mdi:calendar-clock",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_READING_TIME_UTC_SENSOR): text_sensor.text_sensor_schema(
                icon="mdi:clock-outline",
                entity_category="diagnostic",
            ),
            # Binary sensors
            cv.Optional(CONF_ACTIVE_READING): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_RUNNING,
            ),
            cv.Optional(CONF_RADIO_CONNECTED): binary_sensor.binary_sensor_schema(
                icon="mdi:radio-tower",
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_REQUEST_READING_BUTTON): button.button_schema(EverbluMeterTriggerButton),
            cv.Optional(CONF_FREQUENCY_SCAN_BUTTON): button.button_schema(
                EverbluMeterTriggerButton,
                icon="mdi:magnify-scan",
                entity_category="config"
            ),
            cv.Optional(CONF_RESET_FREQUENCY_BUTTON): button.button_schema(
                EverbluMeterTriggerButton,
                icon="mdi:restore",
                entity_category="config"
            ),
        }
    )
    .extend(cv.polling_component_schema("24h"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Add SPI library for CC1101
    cg.add_library("SPI", None)

    # No custom include paths needed when using flat release layout

    # Define flags for conditional compilation in ESPHome environment
    # Use build flags instead of defines to ensure propagation to ALL .cpp files
    cg.add_build_flag("-DUSE_ESPHOME")
    cg.add_build_flag("-DWIFI_SERIAL_NO_REMAP")  # Don't remap Serial in ESPHome builds
    # Provide compile-time values expected by core code as numeric preprocessor defines
    # Use explicit -D flags to ensure visibility in all translation units
    cg.add_build_flag(f"-DMETER_YEAR={config[CONF_METER_YEAR]}")
    cg.add_build_flag(f"-DMETER_SERIAL={config[CONF_METER_SERIAL]}")
    cg.add_build_flag(f"-DGDO0={config[CONF_GDO0_PIN]}")
    
    # Note: ESPHome automatically compiles all .cpp files in component directory
    # No need to explicitly list source files - just ensure main.cpp is excluded from release
    
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
    cg.add(var.set_initial_read_on_boot(config[CONF_INITIAL_READ_ON_BOOT]))
    cg.add(var.set_adaptive_threshold(config[CONF_ADAPTIVE_THRESHOLD]))

    # Enable detailed CC1101 debug logs when requested
    if config.get(CONF_DEBUG_CC1101, False):
        cg.add_build_flag("-DDEBUG_CC1101=1")

    # Link time component
    time_component = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time_component(time_component))

    # Register sensors
    if CONF_VOLUME in config:
        volume_cfg = dict(config[CONF_VOLUME])
        if "unit_of_measurement" not in volume_cfg:
            volume_cfg["unit_of_measurement"] = "m³" if config[CONF_METER_TYPE] == METER_TYPE_GAS else "L"
        if "device_class" not in volume_cfg:
            volume_cfg["device_class"] = "gas" if config[CONF_METER_TYPE] == METER_TYPE_GAS else "water"
        if "icon" not in volume_cfg:
            volume_cfg["icon"] = "mdi:gas-cylinder" if config[CONF_METER_TYPE] == METER_TYPE_GAS else "mdi:water"
        if "accuracy_decimals" not in volume_cfg:
            volume_cfg["accuracy_decimals"] = 0
        if "state_class" not in volume_cfg:
            volume_cfg["state_class"] = STATE_CLASS_TOTAL_INCREASING
        sens = await sensor.new_sensor(volume_cfg)
        cg.add(var.set_volume_sensor(sens))
    
    if CONF_BATTERY in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY])
        cg.add(var.set_battery_sensor(sens))
    
    if CONF_COUNTER in config:
        counter_cfg = dict(config[CONF_COUNTER])
        if "icon" not in counter_cfg:
            counter_cfg["icon"] = "mdi:counter"
        if "accuracy_decimals" not in counter_cfg:
            counter_cfg["accuracy_decimals"] = 0
        if "state_class" not in counter_cfg:
            counter_cfg["state_class"] = STATE_CLASS_TOTAL_INCREASING
        sens = await sensor.new_sensor(counter_cfg)
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
        sens = await text_sensor.new_text_sensor(config[CONF_TIME_START])
        cg.add(var.set_time_start_sensor(sens))
    
    if CONF_TIME_END in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TIME_END])
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
    
    if CONF_FREQUENCY_OFFSET in config:
        sens = await sensor.new_sensor(config[CONF_FREQUENCY_OFFSET])
        cg.add(var.set_frequency_offset_sensor(sens))
    
    if CONF_TUNED_FREQUENCY in config:
        sens = await sensor.new_sensor(config[CONF_TUNED_FREQUENCY])
        cg.add(var.set_tuned_frequency_sensor(sens))

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

    if CONF_HISTORY_JSON in config:
        sens = await text_sensor.new_text_sensor(config[CONF_HISTORY_JSON])
        cg.add(var.set_history_sensor(sens))

    if CONF_METER_SERIAL_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_METER_SERIAL_SENSOR])
        cg.add(var.set_meter_serial_sensor(sens))

    if CONF_METER_YEAR_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_METER_YEAR_SENSOR])
        cg.add(var.set_meter_year_sensor(sens))

    if CONF_READING_SCHEDULE_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_READING_SCHEDULE_SENSOR])
        cg.add(var.set_reading_schedule_sensor(sens))

    if CONF_READING_TIME_UTC_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_READING_TIME_UTC_SENSOR])
        cg.add(var.set_reading_time_utc_sensor(sens))
    
    # Register binary sensors
    if CONF_ACTIVE_READING in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_ACTIVE_READING])
        cg.add(var.set_active_reading_sensor(sens))

    if CONF_RADIO_CONNECTED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_RADIO_CONNECTED])
        cg.add(var.set_radio_connected_sensor(sens))

    if CONF_REQUEST_READING_BUTTON in config:
        btn = await button.new_button(config[CONF_REQUEST_READING_BUTTON])
        cg.add(btn.set_parent(var))
        cg.add(btn.set_frequency_scan(False))
        cg.add(btn.set_reset_frequency(False))

    if CONF_FREQUENCY_SCAN_BUTTON in config:
        btn = await button.new_button(config[CONF_FREQUENCY_SCAN_BUTTON])
        cg.add(btn.set_parent(var))
        cg.add(btn.set_frequency_scan(True))
        cg.add(btn.set_reset_frequency(False))

    if CONF_RESET_FREQUENCY_BUTTON in config:
        btn = await button.new_button(config[CONF_RESET_FREQUENCY_BUTTON])
        cg.add(btn.set_parent(var))
        cg.add(btn.set_frequency_scan(False))
        cg.add(btn.set_reset_frequency(True))
