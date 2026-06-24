Import("env")  # type: ignore[name-defined]
import socket
import time

_RETRY_TIMEOUT = 0.0
try:
    _custom_monitor_connect_delay = env.GetProjectOption(
        "custom_monitor_connect_delay", 0
    )  # type: ignore[name-defined]
    _RETRY_TIMEOUT = float(_custom_monitor_connect_delay or 0)
except Exception as exc:  # noqa: BLE001 - broad for robustness in PlatformIO env
    print(
        "[monitor-delay] Invalid or unavailable 'custom_monitor_connect_delay' value ({!r}); using default 0.0. Error: {}".format(
            locals().get("_custom_monitor_connect_delay", None), exc
        )
    )
_RETRY_INTERVAL = 1.0  # seconds between connection attempts
_INITIAL_DELAY = (
    6.0  # seconds to wait for ESP to reboot before first connection attempt
)


def _wait_for_monitor_ready(*args, **kwargs):
    if _RETRY_TIMEOUT <= 0:
        return

    # Extract IP and port from monitor_port (format: socket://ip:port)
    monitor_port = env.GetProjectOption("monitor_port", "")
    if not monitor_port.startswith("socket://"):
        print("[monitor-delay] No socket monitor configured, skipping wait")
        return

    # Parse IP:port from socket://192.168.2.21:23
    try:
        addr_str = monitor_port.replace("socket://", "")
        host, port = addr_str.rsplit(":", 1)
        port = int(port)
    except (ValueError, AttributeError):
        print("[monitor-delay] Could not parse monitor_port, skipping wait")
        return

    # Wait for ESP to reboot first
    print(f"[monitor-delay] Waiting {_INITIAL_DELAY:.1f}s for ESP to reboot...")
    time.sleep(_INITIAL_DELAY)

    print(
        f"[monitor-delay] Attempting to connect to {host}:{port} (timeout: {_RETRY_TIMEOUT:.0f}s)..."
    )

    start_time = time.time()
    attempt = 0

    while (time.time() - start_time) < _RETRY_TIMEOUT:
        attempt += 1
        try:
            # Try to connect to the monitor port
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            result = sock.connect_ex((host, port))
            sock.close()

            if result == 0:
                elapsed = time.time() - start_time
                print(
                    f"[monitor-delay] Monitor port ready after {elapsed:.1f}s (attempt {attempt})"
                )
                return
        except (TimeoutError, OSError):
            # Ignore transient connection errors; retry until overall timeout
            pass

        # Wait before next attempt
        time.sleep(_RETRY_INTERVAL)

    # Timeout reached
    print(
        f"[monitor-delay] Timeout reached after {_RETRY_TIMEOUT:.0f}s, proceeding anyway..."
    )


# When running `-t upload -t monitor`, this runs after upload and before monitor.
env.AddPostAction("upload", _wait_for_monitor_ready)  # type: ignore[name-defined]
