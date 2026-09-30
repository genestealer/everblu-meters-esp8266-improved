"""Documented FDR getter schema and real public aioesphomeapi client regressions.

Execute the guide against a narrow fake of public client methods and validate
those method signatures with the real installed package. No private client or
installed C++ sources are imported/extracted. This is not a TCP/radio test.
"""

from __future__ import annotations

import asyncio
import json
from pathlib import Path
import sys

import pytest

REPO = Path(__file__).resolve().parents[2]


def test_cached_getter_example_uses_required_response_and_clear_boot_error():
    pytest.importorskip("esphome")
    from esphome.components import api
    from esphome.core import CORE
    from esphome.yaml_util import load_yaml

    config = load_yaml(REPO / "ESPHOME/example-full-fdr.yaml")
    action = config["api"]["actions"][0]
    CORE.reset()
    try:
        validated = api.ACTIONS_SCHEMA(action)[0]
        assert validated["supports_response"] == "only"
        conditional = action["then"][0]["if"]
        assert ".has_state()" in str(conditional["condition"]["lambda"])
        success = conditional["then"][0]["api.respond"]
        assert 'root["json"] = id(fdr_archive).state' in str(success["data"])
        error = conditional["else"][0]["api.respond"]
        assert error["success"] is False
        assert "No successful Full FDR capture" in error["error_message"]
        assert "readFullFdr" not in str(action)
        assert config["everblu_meter"]["tuned_frequency"]["name"] == "Tuned Frequency"
        assert config["everblu_meter"]["auto_scan"] is False
    finally:
        CORE.reset()


def test_public_client_signature_contract():
    package = pytest.importorskip("aioesphomeapi")
    from inspect import signature

    signature(package.APIClient.subscribe_states).bind(None, lambda state: None)
    signature(package.APIClient.button_command).bind(None, 1, device_id=2)
    signature(package.APIClient.execute_service).bind(
        None, None, {}, return_response=True, timeout=600
    )

    async def construct():
        # Constructor validation also covers encryption and keepalive arguments.
        package.APIClient("synthetic.local", 6053, None, noise_psk=None, keepalive=600)

    asyncio.run(construct())


@pytest.mark.parametrize(
    "fetch,captured", [(True, True), (False, True), (False, False)]
)
def test_documented_client_waits_for_initialization_and_gets_archive(
    monkeypatch, capsys, fetch, captured
):
    package = pytest.importorskip("aioesphomeapi")
    from types import SimpleNamespace

    from aioesphomeapi import ButtonInfo, SensorInfo, SensorState, UserService

    source = (
        (REPO / "docs/full-fdr.md")
        .read_text()
        .split("```python\n", 1)[1]
        .split("```", 1)[0]
    )
    archive = {"period": "monthly", "captured_at": None}
    events = []

    class Client:
        def __init__(self, host, port, password, *, noise_psk, keepalive):
            assert host == "synthetic.local" and port == 6053 and keepalive == 600

        async def connect(self, *, login):
            assert login

        async def list_entities_services(self):
            return (
                [
                    ButtonInfo(key=1, name="Fetch Full FDR", device_id=2),
                    SensorInfo(key=2, name="Tuned Frequency", device_id=2),
                ],
                [UserService(name="get_full_fdr", key=3, args=[])],
            )

        def subscribe_states(self, callback):
            events.append("subscribed")
            # Missing/unrelated/non-finite state must not signal initialization.
            callback(SensorState(key=2, device_id=1, state=433.82))
            callback(SensorState(key=2, device_id=2, missing_state=True))
            callback(SensorState(key=2, device_id=2, state=float("nan")))

            def initialized():
                events.append("initialized")
                callback(SensorState(key=2, device_id=2, state=433.82))

            asyncio.get_running_loop().call_soon(initialized)

        def button_command(self, key, *, device_id):
            assert (key, device_id) == (1, 2)
            assert events == ["subscribed", "initialized"]
            events.append("fetch")

        async def execute_service(self, service, data, *, return_response, timeout):
            assert (
                service.key == 3 and data == {} and return_response and timeout == 600
            )
            assert events == ["subscribed", "initialized"] + (
                ["fetch"] if fetch else []
            )
            events.append("get")
            return SimpleNamespace(
                success=captured,
                response_data=json.dumps({"json": json.dumps(archive)}).encode(),
                error_message="No successful Full FDR capture since boot",
            )

        async def disconnect(self):
            events.append("disconnected")

    monkeypatch.setattr(package, "APIClient", Client)
    monkeypatch.setenv("ESPHOME_HOST", "synthetic.local")
    monkeypatch.delenv("ESPHOME_PORT", raising=False)
    monkeypatch.setattr(sys, "argv", ["get_fdr.py"] + (["--fetch"] if fetch else []))
    namespace = {}
    exec(
        compile(
            source.rsplit("asyncio.run(main())", 1)[0], "documented_get_fdr", "exec"
        ),
        namespace,
    )

    async def run():
        if captured:
            await asyncio.wait_for(namespace["main"](), timeout=1)
        else:
            with pytest.raises(RuntimeError, match="No successful Full FDR capture"):
                await asyncio.wait_for(namespace["main"](), timeout=1)

    asyncio.run(run())
    assert events[-1] == "disconnected"
    if captured:
        assert json.loads(capsys.readouterr().out) == archive
