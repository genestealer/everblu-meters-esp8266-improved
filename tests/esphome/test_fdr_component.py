"""Execute the component's diagnostic entry point against recording radio seams."""

from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_diagnostic_report_cannot_reenter_full_fdr(tmp_path):
    compiler = shutil.which("c++")
    if not compiler:
        pytest.skip("C++ compiler unavailable")
    source = (ROOT / "ESPHOME/components/everblu_meter/everblu_meter.cpp").read_text()
    # These adjacent methods belong to this repository, not upstream internals.
    method = source.split(
        "void EverbluMeterComponent::request_diagnostic_report() {", 1
    )[1].split("void EverbluMeterComponent::apply_radio_context()", 1)[0]
    harness = r"""
#include <cassert>
#include <string>
#define ESP_LOGW(...)
constexpr int GPIO_SUMMARY_MAX_LEN = 32;
struct FrequencyManager { static bool scanning; static bool isScanInProgress() { return scanning; } };
bool FrequencyManager::scanning = false;
struct MeterReader { static bool fdr; static bool isFullFdrInProgress() { return fdr; } };
bool MeterReader::fdr = false;
struct cc1101_report_context_t {
  const char *meter_code, *cs_pin_text, *gdo0_pin_text, *gdo2_pin_text;
  int meter_year, meter_serial, rx_attenuation_db;
  bool is_gas, meter_initialised;
  float configured_frequency_mhz;
};
int reports = 0, contexts = 0;
void cc1101_print_diagnostic_report(const cc1101_report_context_t *) { ++reports; }
const char *pin_summary(int, char *, int, const char *fallback) { return fallback; }
struct EverbluMeterComponent {
  std::string meter_code_ = "21-0123456";
  int meter_year_ = 21, meter_serial_ = 123456, rx_attenuation_db_ = 0;
  int cs_ = 0, gdo0_pin_ = 0, gdo2_pin_ = 0;
  bool is_gas_ = false, meter_initialized_ = false;
  float frequency_ = 433.82f;
  void apply_radio_context() { ++contexts; }
  void request_diagnostic_report();
};
void EverbluMeterComponent::request_diagnostic_report() {
"""
    harness += (
        method
        + r"""
int main() {
  EverbluMeterComponent component;
  // A callback on another meter must not inspect registers on the active radio.
  MeterReader::fdr = true;
  component.request_diagnostic_report();
  assert(reports == 0 && contexts == 0);
  component.meter_initialized_ = true;
  component.request_diagnostic_report();
  assert(reports == 0 && contexts == 0);
  MeterReader::fdr = false;
  FrequencyManager::scanning = true;
  component.request_diagnostic_report();
  assert(reports == 0 && contexts == 0);
  // A report must still work before initialisation when the radio cannot start.
  FrequencyManager::scanning = false;
  component.meter_initialized_ = false;
  component.request_diagnostic_report();
  assert(reports == 1 && contexts == 1);
}
"""
    )
    cpp = tmp_path / "diagnostic.cpp"
    cpp.write_text(harness)
    binary = tmp_path / "diagnostic"
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_copyable_example_and_local_ci_source():
    from esphome.yaml_util import load_yaml

    example = load_yaml(ROOT / "ESPHOME/example-full-fdr.yaml")
    established = load_yaml(ROOT / "ESPHOME/example-water-meter.yaml")
    fixture = load_yaml(ROOT / ".ci/esphome/everblu_meter/test.esp8266-fdr.yaml")
    assert example["external_components"] == established["external_components"]
    assert fixture["external_components"][0]["source"]["type"] == "local"
    assert fixture["everblu_meter"] == example["everblu_meter"]
    assert fixture["api"]["actions"][0]["supports_response"] == "only"


def test_fdr_button_reports_rejections_without_touching_radio(tmp_path):
    compiler = shutil.which("c++")
    if not compiler:
        pytest.skip("C++ compiler unavailable")
    source = (ROOT / "ESPHOME/components/everblu_meter/everblu_meter.cpp").read_text()
    method = source.split("void EverbluMeterComponent::request_full_fdr() {", 1)[
        1
    ].split("void EverbluMeterComponent::request_deep_scan()", 1)[0]
    harness = r"""
#include <cassert>
#include <functional>
#include <string>
#define ESP_LOGW(...)
struct FrequencyManager { static bool scanning; static bool isScanInProgress() { return scanning; } };
bool FrequencyManager::scanning = false;
struct MeterReader {
  bool busy = false;
  int requests = 0;
  bool isReadingInProgress() { return busy; }
  void readFullFdr() { ++requests; }
};
struct Publisher {
  bool ready = true;
  int reports = 0;
  std::string error, status;
  std::function<void()> on_status;
  bool isReady() { return ready; }
  void publishError(const char *value) { error = value; }
  void publishStatusMessage(const char *value) {
    status = value;
    ++reports;
    if (reports < 3 && on_status) on_status();
  }
};
struct EverbluMeterComponent {
  MeterReader *meter_reader_ = nullptr;
  Publisher *data_publisher_ = nullptr;
  bool meter_initialized_ = false;
  int contexts = 0;
  void apply_radio_context() { ++contexts; }
  void request_full_fdr();
};
void EverbluMeterComponent::request_full_fdr() {
"""
    harness += (
        method
        + r"""
int main() {
  EverbluMeterComponent component;
  component.request_full_fdr(); // No publisher/reader yet: log only.
  MeterReader reader;
  Publisher publisher;
  component.meter_reader_ = &reader;
  component.data_publisher_ = &publisher;
  publisher.on_status = [&]() { component.request_full_fdr(); };
  component.request_full_fdr();
  assert(publisher.reports == 1);
  assert(publisher.status.find("not ready") != std::string::npos);
  assert(publisher.error == publisher.status);
  assert(reader.requests == 0 && component.contexts == 0);
  component.meter_initialized_ = true;
  reader.busy = true;
  component.request_full_fdr();
  assert(publisher.reports == 2);
  assert(publisher.status.find("radio operation") != std::string::npos);
  assert(reader.busy && reader.requests == 0 && component.contexts == 0);
  reader.busy = false;
  FrequencyManager::scanning = true;
  component.request_full_fdr();
  assert(publisher.reports == 3 && FrequencyManager::scanning);
  assert(reader.requests == 0 && component.contexts == 0);
  publisher.ready = false;
  component.request_full_fdr();
  assert(publisher.reports == 3 && reader.requests == 0 && component.contexts == 0);
  FrequencyManager::scanning = false;
  publisher.ready = true;
  component.request_full_fdr();
  assert(reader.requests == 1 && component.contexts == 1);
}
"""
    )
    cpp = tmp_path / "fdr_button.cpp"
    cpp.write_text(harness)
    binary = tmp_path / "fdr_button"
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
