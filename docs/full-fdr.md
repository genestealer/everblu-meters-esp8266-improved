# Full FDR: request and retrieve an archive

Full FDR is a manual, read-only archive capture for supported Cyble Enhanced
water meters. It is separate from ordinary readings and monthly history.
The meter's configuration selects monthly, weekly, daily or hourly intervals;
this reader does not change that configuration. Monthly operation has physical
validation; the other periods have synthetic coverage and inferred date rules.

Every fetch performs a fresh standard `0x40` read, then predefined frames 7 and 8
(`0x70`), with three wake-ups. ATS is seven zero bytes: no clock synchronisation
or meter writes. Allow roughly ten seconds normally; radio faults can take much
longer. This blocks other radio work. There are no automatic FDR retries, scans,
backfill, scheduling or flash persistence.

The fresh standard reading publishes normal readings/history and updates normal
statistics and adaptive frequency tracking. A successful standard stage also
ends any cooldown and rearms failure-recovery bookkeeping, like a successful
manual read, even if a later archive stage fails. Standalone MQTT counts a failed
fresh standard stage as one failed read immediately, without spending the normal
retry budget. Frames 7/8 then use the selected meter's updated tuning. An
unsuccessful standard read aborts the capture. A successful standard read with an invalid clock permits an explicitly undated
archive. Capture, formatting and delivery failures are reported separately and
preserve the previous successful archive.

## Standalone MQTT

Publish a **non-retained** `fetch` command. Do not add `-r` to the request: a
retained command would cause another meter capture when the reader reconnects.
The discovered **Fetch Full FDR** button uses this command. Example synthetic
serial `123456` (replace broker settings and topic prefix for your installation):

```sh
mosquitto_sub -h BROKER -t 'everblu/cyble/123456/fdr_history' -C 1
# In another terminal, only when a fresh RF capture is wanted:
mosquitto_pub -h BROKER -t 'everblu/cyble/123456/request_full_fdr' -m fetch
```

The flat JSON result is retained on `fdr_history`. Subscribing later retrieves the
broker's last retained result without reading the meter. HA discovery adds the
archive to the existing MQTT device, with a short capture-time state and JSON
attributes. Without reader UTC, the state is `captured`. Ordinary
`liters_attributes`, including cumulative history, remains unchanged.

Before the first successful publish there is no archive topic. A failed refresh
leaves the broker's old result intact. QoS 0 publication success means accepted
by the local client, not acknowledged by the broker: disconnection can lose a
result. Reconnect and subscribe to check what the broker retained. Reader reboot
does not erase the broker's result; broker persistence depends on its settings.

Commands are rejected during scans, reads and pending retries. The existing
2048-byte MQTT packet buffer is enlarged before RF work to hold the measured
maximum plus topic/header overhead; allocation failure prevents capture. It is
shrunk after delivery when possible. MQTT keeps its upstream default keepalive
(15 seconds). Normal FDR takes roughly ten seconds, with standard-reading
publications during the operation. Pathological radio/FIFO waits can exceed the
keepalive, as they can for ordinary reads: the broker may disconnect and archive
publication may fail. The main loop resumes its existing connection/OTA servicing
and reconnection afterwards. There is no new reconnect or FDR retry mechanism,
and MQTT callbacks are not recursively serviced while the radio is busy.

## Standalone ESPHome (no Home Assistant required)

Use the complete [ESP8266 example](../ESPHOME/example-full-fdr.yaml). The optional
output owns the latest successful document in RAM:

```yaml
everblu_meter:
  # Existing SPI, meter_code, clock and radio settings go here.
  request_full_fdr_button:
    name: Fetch Full FDR
  fdr_history_json:
    id: fdr_archive
    name: Full FDR Archive
    internal: true
```

The button requires this output. Filters and `internal: false` are rejected:
the document is structured data and exceeds HA's 255-character entity state
limit. `history_json` remains the original directly exposed compact document
with exactly `monthly_usage`, `current_month_usage` and `months_available`.
Press the fetch button locally or through the native API. A button press itself
does **not** return JSON. Add this separate cached getter:

```yaml
api:
  actions:
    - action: get_full_fdr
      supports_response: only
      then:
        - if:
            condition:
              lambda: return id(fdr_archive).has_state();
            then:
              - api.respond:
                  data: |-
                    root["json"] = id(fdr_archive).state;
            else:
              - api.respond:
                  success: false
                  error_message: No successful Full FDR capture since boot
```

The getter performs no RF work and returns `{"json":"<complete archive JSON>"}`.
Parse the `json` string once at the receiver. It requires **ESPHome 2026.1.0** or
later and **aioesphomeapi 43.0.0** or later. A native client must request the
response explicitly; see the runnable client example below. The state survives a
failed refresh or disconnected receiver, and disappears at reader reboot.
The existing `on_value` automation is available for custom export. There is no
second component cache or `on_full_fdr` trigger. Native getter retrieval requires
neither a web server nor permission for the device to initiate HA actions.

At boot, the component initializes after a native API client subscribes to
states. Connecting or listing entities alone is insufficient. The example adds
`Tuned Frequency`, whose first real value is published during reader
initialization. The client subscribes and waits for that value before pressing
the button; `Ready`/`Idle` text and radio connectivity are boot placeholders and
are not initialization signals. This also applies to local/web button presses:
keep a native state subscriber connected until initialization has completed.

```python
# Save as get_fdr.py; requires aioesphomeapi>=43.0.0.
# Cached only: ESPHOME_HOST=everblu-fdr.local python get_fdr.py
# Fetch then retrieve: ESPHOME_HOST=everblu-fdr.local python get_fdr.py --fetch
# Set ESPHOME_NOISE_PSK for api.encryption; ESPHOME_API_PASSWORD only if configured.
import asyncio
import json
import math
import os
import sys

from aioesphomeapi import APIClient, ButtonInfo, SensorInfo, SensorState


async def main():
    client = APIClient(
        os.environ["ESPHOME_HOST"],
        int(os.environ.get("ESPHOME_PORT", "6053")),
        os.environ.get("ESPHOME_API_PASSWORD"),
        noise_psk=os.environ.get("ESPHOME_NOISE_PSK"),
        keepalive=600,  # Permit the complete blocking operation, including RF faults.
    )
    await client.connect(login=True)
    try:
        entities, services = await client.list_entities_services()
        getter = next(service for service in services if service.name == "get_full_fdr")
        tuning = next(
            entity for entity in entities
            if isinstance(entity, SensorInfo) and entity.name == "Tuned Frequency"
        )
        initialized = asyncio.Event()

        def on_state(state):
            if (isinstance(state, SensorState)
                    and (state.key, state.device_id) == (tuning.key, tuning.device_id)
                    and not state.missing_state and math.isfinite(state.state)):
                initialized.set()

        client.subscribe_states(on_state)
        await asyncio.wait_for(initialized.wait(), timeout=60)
        if "--fetch" in sys.argv:
            button = next(
                entity for entity in entities
                if isinstance(entity, ButtonInfo) and entity.name == "Fetch Full FDR"
            )
            client.button_command(button.key, device_id=button.device_id)
        # Same connection: this request follows the optional blocking button action.
        response = await client.execute_service(
            getter, {}, return_response=True, timeout=600
        )
        if response is None or not response.success:
            raise RuntimeError(response.error_message if response else "No API response")
        envelope = json.loads(response.response_data)
        archive = json.loads(envelope["json"])
        print(json.dumps(archive, indent=2))
        # A refused/failed refresh can return the previous successful archive.
        # Check captured_at and the reader's status before treating it as fresh.
    finally:
        await client.disconnect()


asyncio.run(main())
```

### Optional web retrieval

With `web_server:` enabled, direct GET of the internal text sensor works without
`include_internal: true`. Do not enable that option just for retrieval: it also
puts these large state updates into the web event stream.

```sh
curl -u 'USER:PASSWORD' \
  'http://READER/text_sensor/Full%20FDR%20Archive' | jq -r .value | jq .
```

Use the URL-encoded **entity name**, not the deprecated object-ID URL. For a
sub-device, use `/text_sensor/<URL-encoded sub-device name>/<URL-encoded entity
name>`. The response carries the archive twice, in `state` and `value`; parse one.
Configure `web_server.auth` and use its credentials when enabled. Plain `-u`
sends Basic credentials, accepted by ESPHome 2026.1 and the default configuration
in 2026.9. On versions supporting explicit `auth.type: digest`, add `--digest`
when that option is configured. Without auth,
anyone able to reach the endpoint can retrieve the archive. An unset output has
no valid archive JSON; use the native getter for an explicit before-capture error.
Default debug text-sensor logs may truncate large strings; logs are not a complete
retrieval interface.

## ESPHome with Home Assistant

Use **Home Assistant 2026.1.0** or later. Press the fetch button on the ESPHome
device page, then call the getter with a response variable:

```yaml
actions:
  - action: esphome.everblu_fdr_get_full_fdr
    response_variable: fdr_response
  - variables:
      archive: "{{ fdr_response.json | from_json }}"
```

Use the actual action name registered for your ESPHome node. Because the getter
is response-only, calls without a response request are rejected. Calling it does
not require enabling permission for the device to perform HA actions.

HA does not automatically create an archive entity from this internal output.
For an optional separate archive entity, enable **Allow the device to perform
Home Assistant actions** in the ESPHome integration options and add forwarding:

```yaml
# Inside fdr_history_json:
on_value:
  then:
    - homeassistant.event:
        event: esphome.everblu_full_fdr
        data:
          meter: synthetic_first
          json: !lambda return x;
```

Then use a trigger-based template sensor in HA:

```yaml
template:
  - triggers:
      - trigger: event
        event_type: esphome.everblu_full_fdr
        event_data:
          meter: synthetic_first
    sensor:
      - name: Water Full FDR Archive
        unique_id: synthetic_first_full_fdr
        state: >-
          {% set a = trigger.event.data.json | from_json %}
          {{ a.captured_at or 'captured' }}
        attributes:
          archive: "{{ trigger.event.data.json | from_json }}"
```

This retains the document under one `archive` attribute. It is a separate HA
template entity: automatic association with the native ESPHome device is not
provided. Give each meter its own text sensor ID, getter action, event routing
value and template entity. The [multi-meter fixture](../.ci/esphome/everblu_meter/test.esp8266-multi.yaml)
shows separate outputs/getters/events on one radio.

Events can be missed while HA is disconnected. Recover the cached document with
the getter while the reader remains running; optionally fire the same event in
HA using the returned `json`. After a reader reboot, a new successful capture is
needed. No recorder backfill or automatic recovery capture is performed.

## JSON and meter-clock dates

The dedicated document is flat, with these fields:

| Fields | Meaning |
| --- | --- |
| `period`, `resolution`, `start_day`, `start_hour` | Meter interval configuration |
| `turn_factor`, `pulse_value_code`, `unit` | Scaling; supported water values are litres |
| `current_index`, `global_index` | Scaled current and reference indexes |
| `order`, `interval_count` | `newest_first`; 45, 90 or 180 slots by encoding |
| `consumptions`, `validity` | Interval usage and corresponding quality labels |
| `interval_end`, `timestamp_basis` | Meter-local exclusive ends, or null / `unavailable` |
| `captured_at` | Reader UTC ISO timestamp, or null without valid reader time |

Consumption is not a cumulative history. Supported signed negatives are retained;
reserved overflow/negative values become null. Zero can be `valid`, `not_done`
(null), or `unknown`; current-month zeros remain uncertain because leakage byte
zero semantics are not established.

Dates use only the fresh clock sampled during this fetch. `timestamp_basis` is
`meter_clock` for dated archives. Meter-local boundaries have no `Z` suffix: the
meter clock does not establish UTC or a timezone. `captured_at` is a separate
reader UTC timestamp. Monthly boundaries use the configured day independently
in each month, clamped to month length; only day 31 consistently means month end.
Daily/weekly start-hour rules are reconstructed from driver evidence, corrected
for boundary anomalies, and remain unverified on hardware.

A fetch crossing a calculated interval boundary over its measured duration is
rejected. This guard adds no invented meter-processing margin and does not prove
atomicity of the frame pair or immediate interval closure. Clock sampling and
meter processing uncertainty remain. See the separate
[protocol reference](itron-cyble-enhanced-protocol.md) for layouts and provenance.

## Payload, memory and hardware verification

Host tests enumerate supported scaling/encoding extremes, timestamps, all validity
classes, malformed frames, capture failures and undersized buffers. The measured
maximum raw document is **7898 bytes**, requiring **7899 bytes** including NUL.
This bound is asserted by a runnable regression; truncated JSON is never sent.
The escaped getter envelope is **8671 bytes**; an archive-string envelope is
**8674 bytes**. The minimal web `state`/`value` envelope is **17343 bytes** before
entity metadata and transport framing. Real event routing metadata adds more.
See the execution report for static build sizes.

Static firmware RAM numbers and host tests do not establish ESP8266 runtime heap
headroom. Formatting needs a temporary buffer plus retained sensor state. ESPHome
2026.1 also creates a temporary `std::string` for publication and retains both raw
and filtered state; current ESPHome avoids these extra unfiltered copies.
API retrieval additionally holds the
ArduinoJson string copy, serialized envelope and transport buffers together.
MQTT holds formatter memory, a String copy and the enlarged packet buffer while
constructing the String. It then frees the 7899-byte formatter before publication,
reducing live memory during sending by that amount, not the allocation peak.
Multiple meters multiply retained state costs.

Build and flash [the synthetic ESP8266 probe](../ESPHOME/example-full-fdr-memory-probe.yaml)
without a meter or CC1101. It refreshes two internal outputs every 30 seconds and
logs free heap, largest allocatable block and fragmentation before formatting,
while temporary/retained states coexist, inside the getter copy and afterwards.
Connect native and web clients, request both outputs repeatedly, disconnect and
reconnect clients, and exercise refresh during requests. Record minima and any
allocation/disconnect/reset failures over a sustained run, including one-output
and two-output configurations. Compare logs with actual returned byte lengths.

**ESP8266 runtime heap validation is outstanding.** ESP32-S3 field delivery checks
cannot establish ESP8266 headroom. If the probe fails, reduce avoidable transport
copies first; changing timestamps, dropping fields or limiting platforms requires
an explicit contract decision, not silent truncation.
