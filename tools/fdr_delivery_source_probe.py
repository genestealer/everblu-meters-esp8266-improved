"""Opt-in upstream source research: pytest tools/fdr_delivery_source_probe.py.

Run with current ESPHome and the 2026.1.0 minimum; run the client test also
with aioesphomeapi 43.0.0. All imports/tools are installed development tooling.
The web check compiles the installed upstream methods against narrow HTTP/JSON
seams, so it does not claim a hardware heap or actual network-server test.
"""

from __future__ import annotations

import ast
import asyncio
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
from types import SimpleNamespace
from urllib.parse import quote, unquote

import pytest

REPO = Path(__file__).resolve().parents[1]


def test_native_client_requests_response_and_decodes_cached_archive():
    package = pytest.importorskip("aioesphomeapi")
    from aioesphomeapi.api_pb2 import ExecuteServiceResponse
    from aioesphomeapi.model import APIVersion, UserService

    # Load the distributed Python implementation: installed wheels may otherwise
    # expose a Cython class whose typed socket seam cannot accept a recording fake.
    source = Path(package.__file__).parent / "client.py"
    spec = importlib.util.spec_from_file_location(
        "aioesphomeapi._fdr_test_client", source
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    archive = {"period": "monthly", "consumptions": [10], "captured_at": None}
    envelope = json.dumps({"json": json.dumps(archive)}).encode()

    class Connection:
        callback = None
        sent = None
        removed = False
        captured = True

        def add_message_callback(self, callback, _types):
            self.callback = callback

            def unsubscribe():
                self.removed = True

            return unsubscribe

        def send_message(self, request):
            self.sent = request
            if request.return_response:
                self.callback(
                    ExecuteServiceResponse(
                        call_id=request.call_id,
                        success=self.captured,
                        response_data=envelope if self.captured else b"",
                        error_message=""
                        if self.captured
                        else "No successful Full FDR capture since boot",
                    )
                )

    connection = Connection()

    class Client(module.APIClient):
        def _get_connection(self):
            return connection

        @property
        def api_version(self):
            return APIVersion(1, 12)

    async def run():
        client = Client("synthetic.local", 6053, None)
        service = UserService(name="get_full_fdr", key=1, args=[])
        response = await client.execute_service(service, {}, return_response=True)
        assert connection.sent.return_response is True
        assert connection.sent.call_id != 0
        assert connection.removed
        assert response.success
        assert json.loads(json.loads(response.response_data)["json"]) == archive
        connection.captured = False
        response = await client.execute_service(service, {}, return_response=True)
        assert not response.success
        assert "No successful Full FDR capture" in response.error_message
        assert await client.execute_service(service, {}) is None
        assert not connection.sent.return_response
        assert connection.sent.call_id == 0

    asyncio.run(run())


def _cpp_function(source, signature):
    """Extract one installed upstream function, including its original body."""
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def test_internal_web_get_by_encoded_name_and_subdevice(tmp_path):
    package = pytest.importorskip("esphome")
    compiler = shutil.which("c++")
    if not compiler:
        pytest.skip("C++ compiler unavailable")
    source = (
        Path(package.__file__).parent / "components/web_server/web_server.cpp"
    ).read_text()
    methods = "\n".join(
        _cpp_function(source, signature)
        for signature in (
            "static UrlMatch match_url(",
            "EntityMatchResult UrlMatch::match_entity(",
            "void WebServer::on_text_sensor_update(",
            "void WebServer::handle_text_sensor_request(",
        )
    )
    harness = Path(__file__).with_name("fdr_web_source_harness.cpp").read_text()
    cpp = tmp_path / "web.cpp"
    cpp.write_text(harness.replace("// INSERT_ESPHOME_METHODS", methods))
    binary = tmp_path / "web"
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
    name = quote("Full FDR Archive", safe="")
    device = quote("Synthetic Meter", safe="")
    routes = [f"/text_sensor/{name}", f"/text_sensor/{device}/{name}"]
    assert "%20" in routes[0]
    # AsyncWebServer supplies a URL-decoded path to ESPHome's request handler.
    subprocess.run([str(binary), *(unquote(route) for route in routes)], check=True)


def test_home_assistant_only_response_contract():
    """Use a pinned HA checkout without installing the full Home Assistant app."""
    from aioesphomeapi.core import APIConnectionError
    from aioesphomeapi.model import SupportsResponseType, UserService

    configured = os.environ.get("FDR_HA_MANAGER_SOURCE")
    if not configured:
        pytest.skip("Set FDR_HA_MANAGER_SOURCE to pinned HA esphome/manager.py")
    source = Path(configured).read_text()
    function = next(
        node
        for node in ast.parse(source).body
        if isinstance(node, ast.AsyncFunctionDef) and node.name == "execute_service"
    )
    # Avoid importing the entire HA app; preserve the original executable body.
    code = "from __future__ import annotations\n" + ast.unparse(function)

    class ActionError(Exception):
        def __init__(self, **kwargs):
            super().__init__(kwargs["translation_placeholders"].get("error", ""))

    namespace = {
        "SupportsResponseType": SupportsResponseType,
        "APIConnectionError": APIConnectionError,
        "HomeAssistantError": ActionError,
        "DOMAIN": "esphome",
        "json_loads_object": json.loads,
    }
    exec(compile(code, "pinned_ha_esphome_manager", "exec"), namespace)
    captured = True

    class Client:
        async def execute_service(self, _service, _data, *, return_response):
            assert return_response is True
            return SimpleNamespace(
                success=captured,
                error_message="No successful Full FDR capture since boot",
                response_data=b'{"json":"{\\"period\\":\\"monthly\\"}"}',
            )

    async def run():
        nonlocal captured
        entry = SimpleNamespace(client=Client(), name="Synthetic Meter")
        service = UserService(name="get_full_fdr", key=1, args=[])
        call = SimpleNamespace(data={}, return_response=True)
        result = await namespace["execute_service"](
            entry, service, call, supports_response=SupportsResponseType.ONLY
        )
        assert json.loads(result["json"]) == {"period": "monthly"}
        captured = False
        with pytest.raises(ActionError, match="No successful Full FDR capture"):
            await namespace["execute_service"](
                entry, service, call, supports_response=SupportsResponseType.ONLY
            )

    asyncio.run(run())
