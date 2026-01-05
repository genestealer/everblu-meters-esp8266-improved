Import("env")  # type: ignore
import time
import socket

# Optional: retry logic for WiFi serial monitor connection
# Set with `custom_monitor_connect_delay` in platformio.ini (float or int).
# If > 0, will attempt to connect to monitor port with retries instead of fixed delay.
_RETRY_TIMEOUT = float(env.GetProjectOption("custom_monitor_connect_delay", 0) or 0)  # type: ignore
_RETRY_INTERVAL = 1.0  # seconds between connection attempts
_INITIAL_DELAY = 3.0   # seconds to wait for ESP to reboot before first connection attempt


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
    print("[monitor-delay] Waiting %.1fs for ESP to reboot..." % _INITIAL_DELAY)
    time.sleep(_INITIAL_DELAY)
    
    print("[monitor-delay] Attempting to connect to %s:%d (timeout: %.0fs)..." % (host, port, _RETRY_TIMEOUT))
    
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
                print("[monitor-delay] Monitor port ready after %.1fs (attempt %d)" % (elapsed, attempt))
                return
        except Exception as e:
            pass
        
        # Wait before next attempt
        time.sleep(_RETRY_INTERVAL)
    
    # Timeout reached
    print("[monitor-delay] Timeout reached after %.0fs, proceeding anyway..." % _RETRY_TIMEOUT)


# When running `-t upload -t monitor`, this runs after upload and before monitor.
env.AddPostAction("upload", _wait_for_monitor_ready)  # type: ignore
