"""Unit tests for the everblu_meter ESPHome config validators.

These load the component's ``__init__.py`` directly and call each validator
function, so failures are precise and fast (no ESPHome compile step). They
complement the negative ``esphome config`` fixtures under
``.ci/esphome/everblu_meter/test.invalid-*.yaml``.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path

import esphome.config_validation as cv
from esphome.const import (
    KEY_CORE,
    KEY_TARGET_FRAMEWORK,
    KEY_TARGET_PLATFORM,
    PLATFORM_ESP32,
    PLATFORM_ESP8266,
)
from esphome.core import CORE
import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
COMPONENT_INIT = (
    REPO_ROOT / "ESPHOME" / "components" / "everblu_meter" / "__init__.py"
)


def _load_component():
    spec = importlib.util.spec_from_file_location(
        "everblu_meter_component_under_test", COMPONENT_INIT
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


comp = _load_component()


@pytest.fixture(autouse=True)
def _reset_core():
    """Give each test a clean CORE with no platform/framework set."""
    CORE.reset()
    yield
    CORE.reset()


def _set_target(platform, framework):
    CORE.data[KEY_CORE] = {
        KEY_TARGET_PLATFORM: platform,
        KEY_TARGET_FRAMEWORK: framework,
    }


# --- validate_meter_code -------------------------------------------------


def test_meter_code_valid_without_suffix():
    result = comp.validate_meter_code("20-0257750")
    assert result["year"] == 20
    assert result["serial"] == 257750
    assert result["raw"] == "20-0257750"


def test_meter_code_valid_with_suffix():
    result = comp.validate_meter_code("16-0039185-107")
    assert result["year"] == 16
    assert result["serial"] == 39185


@pytest.mark.parametrize(
    "code",
    [
        "1-1234567",  # 1-digit year
        "20-12345",  # serial not 7 digits
        "20-1234567-12",  # suffix not 3 digits
        "201234567",  # no dashes
        "20 0257750",  # contains space
        "20-0000000",  # serial all zeros
    ],
)
def test_meter_code_invalid(code):
    with pytest.raises(cv.Invalid):
        comp.validate_meter_code(code)


# --- validate_reading_schedule ------------------------------------------


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("monday-friday", "Monday-Friday"),
        ("MONDAY-SUNDAY", "Monday-Sunday"),
        ("friday", "Friday"),
        ("Saturday", "Saturday"),
        ("  Monday-Saturday  ", "Monday-Saturday"),
    ],
)
def test_reading_schedule_normalizes_case(value, expected):
    assert comp.validate_reading_schedule(value) == expected


@pytest.mark.parametrize("value", ["Funday", "Mon-Fri", "Everyday", ""])
def test_reading_schedule_invalid(value):
    with pytest.raises(cv.Invalid):
        comp.validate_reading_schedule(value)


# --- validate_pins -------------------------------------------------------


def test_pins_conflict_rejected():
    config = {
        comp.CONF_GDO0_PIN: {"number": 4},
        comp.CONF_GDO2_PIN: {"number": 4},
    }
    with pytest.raises(cv.Invalid):
        comp.validate_pins(config)


def test_pins_distinct_ok():
    config = {
        comp.CONF_GDO0_PIN: {"number": 4},
        comp.CONF_GDO2_PIN: {"number": 5},
    }
    assert comp.validate_pins(config) is config


def test_pins_no_gdo2_ok():
    config = {comp.CONF_GDO0_PIN: {"number": 4}}
    assert comp.validate_pins(config) is config


# --- validate_gdo2_required ---------------------------------------------


def test_gdo2_both_set_rejected():
    config = {comp.CONF_GDO2_PIN: {"number": 5}, comp.CONF_DISABLE_GDO2: True}
    with pytest.raises(cv.Invalid):
        comp.validate_gdo2_required(config)


def test_gdo2_neither_set_rejected():
    config = {comp.CONF_DISABLE_GDO2: False}
    with pytest.raises(cv.Invalid):
        comp.validate_gdo2_required(config)


def test_gdo2_pin_only_ok():
    config = {comp.CONF_GDO2_PIN: {"number": 5}, comp.CONF_DISABLE_GDO2: False}
    assert comp.validate_gdo2_required(config) is config


def test_gdo2_disabled_only_ok():
    config = {comp.CONF_DISABLE_GDO2: True}
    assert comp.validate_gdo2_required(config) is config


# --- validate_esp32_framework -------------------------------------------


def test_esp32_idf_rejected():
    _set_target(PLATFORM_ESP32, "esp-idf")
    with pytest.raises(cv.Invalid):
        comp.validate_esp32_framework({})


def test_esp32_arduino_ok():
    _set_target(PLATFORM_ESP32, "arduino")
    config = {}
    assert comp.validate_esp32_framework(config) is config


def test_esp8266_arduino_ok():
    _set_target(PLATFORM_ESP8266, "arduino")
    config = {}
    assert comp.validate_esp32_framework(config) is config
