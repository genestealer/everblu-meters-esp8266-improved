"""Run the standalone command body with a recording transport (no board/broker)."""

from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def fdr_settings(source, enabled):
    start = source.index("// Define MQTT debugging")
    end = source.index("// Define gas volume divisor", start)
    override = "" if enabled is None else f"#define ENABLE_FULL_FDR {enabled}\n"
    return override + source[start:end]


@pytest.mark.parametrize("enabled", [None, 0, 1])
def test_mqtt_full_fdr(tmp_path, enabled):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("C++ compiler unavailable")
    source = (ROOT / "src/main.cpp").read_text()
    signature = "void onUpdateData()"
    assert signature in source, "standalone MQTT must implement Full FDR"
    start = source.index(signature)
    end = source.index("\n// Function: onScheduled", start)
    harness = Path(__file__).with_name("mqtt_full_fdr_harness.cpp.in").read_text()
    header = (ROOT / "src/services/meter_history.h").read_text()
    interval = next(
        line
        for line in header.splitlines()
        if "constexpr uint32_t FULL_FDR_MIN_INTERVAL_MS" in line
    )
    harness = harness.replace(
        "// COMMAND_UNDER_TEST", interval + "\n" + source[start:end]
    )
    subscription_start = source.index(
        "  if (!meterIsGas", source.index("void onConnectionEstablished()\n{")
    )
    subscription_end = source.index("  char restartTopic[", subscription_start)
    harness = harness.replace(
        "// SUBSCRIPTION_UNDER_TEST", source[subscription_start:subscription_end]
    )
    cpp = tmp_path / "mqtt.cpp"
    cpp.write_text(fdr_settings(source, enabled) + harness)
    binary = tmp_path / "mqtt"
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


@pytest.mark.parametrize("enabled", [None, 0, 1])
def test_mqtt_fdr_discovery_requires_opt_in_and_water(tmp_path, enabled):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("C++ compiler unavailable")
    source = (ROOT / "src/main.cpp").read_text()
    start = source.index("  // Request Reading Button")
    end = source.index("  // Diagnostic sensors", start)
    harness = (
        r"""
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
using String = std::string;
const char *mqttBaseTopic = "everblu/cyble/123456";
std::string getMeterPrefix() { return "test_"; }
std::string buildDeviceJson() { return ""; }
std::vector<std::string> entities;
void publishDiscoveryMessage(const char *, const char *id, const std::string &) {
  entities.emplace_back(id);
}
void discover(bool meterIsGas) {
  std::string json;
"""
        + source[start:end]
        + r"""
}
int main() {
  discover(true);
  assert(entities == std::vector<std::string>{"everblu_meter_request"});
  entities.clear();
  discover(false);
  if (!ENABLE_FULL_FDR) {
    assert(entities == std::vector<std::string>{"everblu_meter_request"});
    return 0;
  }
  assert((entities == std::vector<std::string>{"everblu_meter_request",
    "everblu_meter_full_fdr_request", "everblu_meter_fdr_history"}));
}
"""
    )
    cpp = tmp_path / "discovery.cpp"
    cpp.write_text(fdr_settings(source, enabled) + harness)
    binary = tmp_path / "discovery"
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


@pytest.mark.parametrize("enabled", [None, 0, 1])
def test_mqtt_fdr_subscription_requires_opt_in_and_water(tmp_path, enabled):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("C++ compiler unavailable")
    source = (ROOT / "src/main.cpp").read_text()
    start = source.index("  char fdrTopic[")
    # Include the guard around the subscription when present.
    guard = source.rfind("  if (!meterIsGas", 0, start)
    if guard > source.rfind("onUpdateData(); });", 0, start):
        start = guard
    end = source.index("  char restartTopic[", start)
    cpp = tmp_path / "subscriptions.cpp"
    cpp.write_text(
        fdr_settings(source, enabled)
        + r"""
#include <cassert>
#include <cstdio>
#include <string>
#include <functional>
#include <vector>
using String = std::string;
constexpr int MQTT_TOPIC_BUFFER_SIZE = 128;
const char *mqttBaseTopic = "synthetic";
bool dispatching = false;
int requests = 0, errors = 0;
struct {
  int count = 0;
  std::function<void(const String &)> callback;
  std::vector<std::function<void()>> delayed;
  template<class T> void subscribe(const char *, T fn) { ++count; callback = fn; }
  template<class T> void executeDelayed(unsigned, T fn) { delayed.push_back(fn); }
  void drain() {
    while (!delayed.empty()) { auto fn = delayed.front(); delayed.erase(delayed.begin()); fn(); }
  }
} mqtt;
void publishSub(const char *, const char *, bool) { assert(!dispatching); ++errors; }
void onRequestFullFdr() { assert(!dispatching); ++requests; }
void subscribe(bool meterIsGas) {
"""
        + source[start:end]
        + r"""
}
int main() {
  subscribe(true); assert(mqtt.count == 0);
  subscribe(false);
  if (!ENABLE_FULL_FDR) { assert(mqtt.count == 0); return 0; }
  assert(mqtt.count == 1);
  // The subscription dispatcher still owns its input buffer and matches later
  // subscribers after invoking ours. No publication or resize may happen here.
  dispatching = true;
  for (int i = 0; i < 100; ++i) mqtt.callback("fetch");
  assert(requests == 0 && mqtt.delayed.size() == 1);
  dispatching = false;
  mqtt.drain();
  assert(requests == 1 && errors == 0);
  dispatching = true;
  for (int i = 0; i < 100; ++i) mqtt.callback("invalid");
  assert(errors == 0 && mqtt.delayed.size() == 1);
  dispatching = false;
  mqtt.drain();
  assert(requests == 1 && errors == 1);
  dispatching = true;
  mqtt.callback("invalid");
  mqtt.callback("fetch"); // A valid fetch wins over a pending malformed command.
  assert(mqtt.delayed.size() == 1);
  dispatching = false;
  mqtt.drain();
  assert(requests == 2 && errors == 1);
}
"""
    )
    binary = tmp_path / "subscriptions"
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
