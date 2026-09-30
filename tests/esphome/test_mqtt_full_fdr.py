"""Run the standalone command body with a recording transport (no board/broker)."""

from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_mqtt_full_fdr(tmp_path):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("C++ compiler unavailable")
    source = (ROOT / "src/main.cpp").read_text()
    signature = "void onUpdateData()"
    assert signature in source, "standalone MQTT must implement Full FDR"
    start = source.index(signature)
    end = source.index("\n// Function: onScheduled", start)
    harness = Path(__file__).with_name("mqtt_full_fdr_harness.cpp.in").read_text()
    harness = harness.replace("// COMMAND_UNDER_TEST", source[start:end])
    cpp = tmp_path / "mqtt.cpp"
    cpp.write_text(harness)
    binary = tmp_path / "mqtt"
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
