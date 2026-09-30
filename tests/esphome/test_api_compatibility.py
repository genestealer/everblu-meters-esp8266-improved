"""Compile the component's API query and reject clients without state subscriptions.

The small API doubles check selection/semantics; firmware builds still verify
compatibility with the actual ESPHome headers.
"""

from pathlib import Path
import shutil
import subprocess

import pytest

SOURCE = (
    Path(__file__).resolve().parents[2]
    / "ESPHOME/components/everblu_meter/everblu_meter.cpp"
)


@pytest.mark.parametrize(
    "version", [(2026, 1, 0), (2026, 2, 0), (2026, 3, 0), (2026, 9, 1)]
)
def test_api_requires_state_subscription(tmp_path, version):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("C++ compiler required for API compatibility check")

    source = SOURCE.read_text()
    query = source.split("if (esphome::api::global_api_server != nullptr) {", 1)[1]
    query = query.split("// Initialize meter reader", 1)[0]
    includes = "\n".join(
        line
        for line in source.splitlines()
        if line == '#include "esphome/core/version.h"'
    )
    header = tmp_path / "esphome/core/version.h"
    header.parent.mkdir(parents=True)
    header.write_text(
        "#define VERSION_CODE(major, minor, patch) "
        "((major << 16) | (minor << 8) | patch)\n"
        f"#define ESPHOME_VERSION_CODE VERSION_CODE({version[0]}, {version[1]}, {version[2]})\n"
    )
    methods = (
        "bool is_connected(bool subscription_only = false) const { "
        "return subscription_only ? subscribed : connected; }"
        if version in [(2026, 1, 0), (2026, 2, 0)]
        else "bool is_connected() const { return connected; }\n"
        "bool is_connected_with_state_subscription() const { return subscribed; }"
    )
    program = tmp_path / "api.cpp"
    program.write_text(
        f"""
#include <cassert>
{includes}
namespace esphome {{ namespace api {{
struct APIServer {{
  bool connected = false;
  bool subscribed = false;
  {methods}
}} server;
APIServer *global_api_server = &server;
}} }}
bool ha_connected() {{
{query}
  return is_ha_connected;
}}
int main() {{
  assert(!ha_connected());
  esphome::api::server.connected = true;
  assert(!ha_connected());
  esphome::api::server.subscribed = true;
  assert(ha_connected());
  esphome::api::server.subscribed = false;
  assert(!ha_connected());
}}
"""
    )
    binary = tmp_path / "api-check"
    subprocess.run(
        [compiler, "-std=c++17", "-I", str(tmp_path), str(program), "-o", str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    subprocess.run([str(binary)], check=True)
