"""Run the radio-free probe's actual lambda on the host; no ESPHome build needed."""

import json
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_probe_generates_measured_maximum_payload(tmp_path):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("C++ compiler unavailable")
    yaml = (ROOT / "ESPHOME/example-full-fdr-memory-probe.yaml").read_text()
    body = yaml.split("          auto sample =", 1)[1].split("\ninterval:", 1)[0]
    source = tmp_path / "probe.cpp"
    source.write_text(
        "#include <cstdio>\n#include <string>\n#include <iostream>\n"
        "#define ESP_LOGI(...)\n#define id(x) x\n"
        "struct Sensor { std::string state; void publish_state(const std::string &s) "
        "{ state = s; } };\n"
        "int main() { Sensor first_archive, second_archive; auto sample ="
        + body
        + "\nif (first_archive.state != second_archive.state) return 1;\n"
        "std::cout << first_archive.state; }\n"
    )
    executable = tmp_path / "probe"
    subprocess.run(
        [compiler, "-std=c++17", str(source), "-o", str(executable)], check=True
    )
    payload = subprocess.check_output([str(executable)], text=True)
    archive = json.loads(payload)
    assert len(payload) == 7898
    assert archive["interval_count"] == 180
    assert archive["consumptions"] == [-1270000000] * 180
    assert archive["validity"] == ["valid"] * 180
    assert archive["interval_end"][0] == "2099-12-31T23:00:00"
    assert archive["interval_end"][-1] == "2085-01-31T23:00:00"
    assert "2096-02-29T23:00:00" in archive["interval_end"]
    assert "2099-02-28T23:00:00" in archive["interval_end"]
    assert archive["captured_at"] == "2099-12-31T23:59:59Z"
