# Itron Cyble Enhanced protocol research

These notes describe the read commands found while adding Full FDR support for
EverBlu / AnyQuest Cyble Enhanced water meters. For setup and the history JSON
format, see the [Full FDR guide](full-fdr.md).

The protocol was independently derived for interoperability from Itron's Android
Driver Service and implemented using this project's existing RADIAN transport.
Frames 7 and 8 were validated on a real EverBlu Cyble Enhanced V2.1 meter
configured for monthly FDR. Other periods have synthetic test coverage.
The additional commands below were found in the driver; they have not been
implemented or tested on a meter by this project.

This work includes no APKs, decompiled source or extracted driver resources.
No real meter identity or household consumption data is included in these notes.

## Research sources

The research used this repository's RADIAN implementation, Itron product
documentation and Android software. These hashes identify the Android files
for anyone who independently has access to them:

| Software | Version | SHA-256 |
| --- | --- | --- |
| Itron Driver Service | 3.1.6 | `9500443b312f5421f120efe281363f288f970f14ee73af4aeec4d26133c2c33a` |
| Itron Driver Service | 3.1.9 | `3c6d0e6d178a0062a21c3f08c16556d7176aeef4dfdb47d16f28d2bb35b22ea2` |
| RFCT | 5.0.10 | `da819bb59b3fe21c6490a3a3e131cba9694102b214c44e444db3240940a7f953` |

The protocol tables were checked against Driver Service 3.1.6 and 3.1.9.
RFCT and third-party software that calls the driver provided context; they are
not evidence that every listed command works on every meter.

## Standard read: `0x40`

The existing reader sends `0A 40` after the RADIAN address header. It uses
CRC-16/KERMIT, the project's serial encoding and the normal wake-up burst.
An acknowledgement precedes the meter-data response.

The driver describes a cumulative index, diagnostic flags, meter clock,
battery life, meter identification, wake window and read/configuration counters.
It also defines optional backflow, leakage and historical-index fields.
Availability depends on the meter model and response length. The project
already reads the main index and up to 13 monthly cumulative history values.

These legacy monthly indexes are separate from the Full FDR interval archive.

## Full FDR request: `0x70`, frames 7 and 8

The Android API calls this operation `ReadFullFDR`. It requests two predefined
reading frames. The unencoded request has this structure:

```text
[length] 10 00
[meter address: 5 bytes] 00
[master address: 5 bytes] 00
0A 70
[ATS: 7 bytes]
[access code: 2 bytes]
[frame selector: 07 or 08]
[CRC: 2 bytes]
```

The implementation calculates the length and CRC and reuses the standard
read's addressing, encoding, wake-up and CC1101 exchange. Each frame request
has its own wake-up, acknowledgement and response.

**ATS means automatic time synchronisation.** Sending a timestamp can set the
meter's clock. This implementation sends the driver's ATS-disabled encoding,
seven zero bytes, and access code `00 00`. It exposes no clock-setting option.

For reference, enabled ATS uses day, month, year minus 2000, weekday, hour,
minute and second. Weekdays run from Monday = 1 to Sunday = 7. The inspected
driver uses local standard time, removing its daylight-saving adjustment.
That mode is not used here.

## Validated response layouts

Offsets below count bytes from zero after radio and serial decoding.
Both responses have a 16-byte envelope, with the communication
status at offset 16. Frame 7 also has two uninterpreted bytes after its fields.

| Frame 7 field | Offsets | Bytes |
| --- | --- | ---: |
| Communication status | 16 | 1 |
| Current index | 17–20 | 4 |
| Pulse and medium | 21–22 | 2 |
| FDR configuration | 23–25 | 3 |
| Enhanced alarms | 26–28 | 3 |
| Backflow data | 29–34 | 6 |
| Global/reference index | 35–38 | 4 |
| First consumption fragment | 39–126 | 88 |
| Water-intelligence alarms | 127–132 | 6 |
| Uninterpreted suffix | 133–134 | 2 |
| CRC | 135–136 | 2 |

| Frame 8 field | Offsets | Bytes |
| --- | --- | ---: |
| Communication status | 16 | 1 |
| Meter-interface group | 17 | 1 |
| Battery lifetime | 18 | 1 |
| Leakage threshold | 19–20 | 2 |
| RF counters | 21–22 | 2 |
| Leakage history | 23–36 | 14 |
| Second consumption fragment | 37–128 | 92 |
| Billing indexes | 129–136 | 8 |
| CRC | 137–138 | 2 |

The validated totals are **137 bytes (`0x89`)** and **139 bytes (`0x8B`)**.
The parser checks the declared length against the supported layout and checks
CRC over that declared length, along with the response control byte. The radio
reader also checks the addresses.
Extra captured bytes after the declared frame are ignored.

## FDR configuration and consumption values

The three configuration bytes use these bit fields, with bit 0 being the
least-significant bit:

| Byte | Field |
| ---: | --- |
| 0 | Start hour |
| 1 | Bits 0–4: start day; bits 5–6: period; bit 7: unused |
| 2 | Bits 0–3: turn-factor code; bits 4–7: resolution code |

Period codes are **0 = monthly, 1 = weekly, 2 = daily, 3 = hourly**. The read
request does not select a period. No separate hourly archive was established
by this research: the response must be interpreted using its configuration.

Resolution controls how many interval values fit in the 180 consumption bytes:

| Code | Encoding | Capacity |
| ---: | --- | ---: |
| 0 | Signed 32-bit | 45 intervals |
| 1 | Unsigned 16-bit | 90 intervals |
| 2 | Signed 16-bit | 90 intervals |
| 3 | Unsigned 8-bit | 180 intervals |
| 4 | Signed 8-bit | 180 intervals |

The driver also defines irrigation encoding, which this decoder does not
support. Capacity is a count of slots, not a guarantee of valid readings.

Turn-factor codes 0–4 mean multipliers of 1, 10, 100, 1,000 and 10,000.
Values are little-endian. To obtain newest-first order, decode and reverse
the records within each fragment, then put frame 7's records before frame 8's.
Reversing the whole concatenated buffer would put the fragments in the wrong
order. The raw stored buffer keeps the original 88 + 92 wire bytes.

Each value is **interval consumption**, not a cumulative index. Consumption
is scaled by the turn multiplier and pulse weight. The global index is a
separate cumulative reference: the driver rounds it down to a turn-factor
multiple before pulse scaling, rather than multiplying it by the turn factor.

Some encodings reserve values for overflow or an unrepresentable negative
consumption. Zero can mean either zero consumption or an unrecorded interval;
the decoder uses the leakage-history FDR-done flags where possible. The JSON
exports validity alongside values, so consumers need not guess.

The current implementation exports interval consumption and the reference
index; it does not generate a cumulative historical series or write into
Home Assistant's recorder. Interval dates use a fresh meter-clock sample from the same capture operation,
never a cached clock. See the [guide](full-fdr.md#json-and-meter-clock-dates) for
boundary-rollover rejection, missing-clock behaviour and timezone interpretation.

## Decoder reference

**CRC.** For a declared frame length `L`, CRC-16/KERMIT covers bytes `0` through
`L-3`, including the length byte. Bytes `L-2` and `L-1` hold the standard CRC
value least-significant byte first. As a check, ASCII `123456789` gives `0x2189`,
stored as `89 21`. This repository's `crc_kermit()` returns a byte-swapped
integer, so its callers write the high byte first to produce those same wire
bytes. Do not swap it a second time when reusing the helper.

**Pulse scaling.** `PulseMedium` contains the medium code first, then the pulse
code. The water decoder accepts medium codes 6, 7 and 22 (decimal). For pulse
code `p`, the pulse weight in litres is `10^(p-4)` for codes 0–7 and
`10^(p-15)` for codes 11–18. Other codes are unsupported. With decoded turn
multiplier `T` and pulse weight `W`, the conversions are:

```text
interval litres = signed or unsigned decoded value * T * W
current litres  = current index * W
global litres   = (global index - global index % T) * W
```

**Reserved values.** Check these before scaling. The numbers below are decoded
integer values, not byte sequences.

| Encoding | Overflow | Negative value cannot be represented |
| --- | ---: | ---: |
| Signed 32-bit | No reserved value | No reserved value |
| Unsigned 16-bit | 65535 | 65534 |
| Signed 16-bit | -32768 | No reserved value |
| Unsigned 8-bit | 255 | 254 |
| Signed 8-bit | -128 | No reserved value |

Reserved values become JSON `null`, with `overflow` or `negative` validity.
Other representable signed negative values are retained.

Zero needs a separate check. In the 14-byte leakage-history block, byte 0 is
the current month; bytes 1–13 run from 13 months ago to the most recent
completed month. Bit 7 (`0x80`) is the FDR-done flag. With a recent meter clock,
the decoder assigns an interval to a month using its exclusive end boundary
minus one second. For a covered past month, a set flag means a valid zero;
a clear flag means `not_done` and JSON `null`. For completed intervals in the current month, byte 0 semantics have not been
established, so zero retains `unknown` validity. Without a usable clock, or
outside that date window, zero likewise stays zero with `unknown` validity.

**Shorter FDR blocks.** `Fdr24` and `Fdr48` contain 24 and 48 consumption bytes,
respectively, plus a four-byte global index. Their total sizes are 28 and 52
bytes. The names do not mean hours or a fixed number of readings: divide the
consumption-byte count by the configured value width to get the slot count,
and use the configured period to interpret it. These block reads remain
unimplemented here.

**Communication status.** Its individual bit meanings have not been verified.
The frame parser preserves the raw status, and the JSON decoder requires zero
in both frames. A nonzero status leaves the previous JSON intact even when
the frame's CRC is valid.

## Date reconstruction and limits

The standard `0x40` clock is read afresh immediately before frames 7 and 8.
Strict parsing rejects malformed dates, including impossible calendar days.
Dates are meter-local exclusive interval ends, never inferred UTC. Reader UTC
capture time is reported separately. Comparing the boundary at the sampled
clock and at that clock advanced by the measured capture duration rejects known
rollovers, but does not establish an atomic snapshot or immediate meter closure.

Monthly start days are reapplied independently to each month. Days 28, 29 and 30
are clamped only when necessary; only day 31 consistently selects month end.
Daily intervals select the most recent configured start hour. Weekly intervals
select the configured weekday/start hour, rolling back a week when that boundary
has not yet occurred. These daily/weekly corrections resolve demonstrated
start-hour anomalies in the reconstruction; they are inferred rules with
synthetic tests, not additional physical validation. Hourly boundaries use the
start of the hour. Only monthly Full FDR has been validated on a physical meter.

## Other reads found in the driver

Apart from frames 7 and 8, the following are research leads, not supported
commands in this component. Their availability and response formats still need
hardware validation.

### Predefined frames

These frame selectors belong to command `0x70`. Selector numbers are decimal.

| Frame | Driver description |
| ---: | --- |
| 0 | Configuration, with Cyble and Pulse variants |
| 1 | NRF Basic |
| 2 | NRF France |
| 3 | NRF Enhanced |
| 4 | Minimal |
| 5 | Minimal with meter identification |
| 6 | Meter analysis |
| 7 / 8 | Full FDR, first / second fragment; implemented and validated |
| 9 | Irrigation volume FDR |
| 10 | Irrigation threshold FDR |

Frame 6 contains peak-period settings, a customer/event log, a field named
`CurrentFlow` and a peak table. The log includes removal, leakage, backflow
and configuration-event dates, recorded to the day. Alarm events record when
an alarm was raised or cleared, not when its detection was enabled or disabled.
The peak table holds five values with dates recorded to the hour.
The name `CurrentFlow` alone does not establish
its units, scaling or measurement period; those need checking before exposing
it as a flow-rate sensor.

Frame 0 contains settings such as wake profile, pulse/medium, leakage threshold,
FDR, peaks, billing and irrigation. It also includes an access code, which
must be excluded from logs and published data in any future reader.

### Block reads: `0x6A`

The driver calls this command `ReadByBlock`. Its request includes ATS, an access
code, a block count and a list of block IDs. Any future read-only implementation
must also disable ATS; a read command name alone does not prevent clock changes.

The readable block definitions include the following **decimal** IDs. For
example, current flow is block 36 (`0x24`), not `0x36`.

| Block ID | Content |
| ---: | --- |
| 20 | Enhanced alarms |
| 21 | Meter identification |
| 22 | RF counters |
| 23 | Backflow |
| 24 | Leakage history |
| 25 / 26 | FDR48 / FDR24 |
| 27 / 28 | FDR88 / FDR92 |
| 29 | Water-intelligence alarms |
| 30 | Highest peak |
| 31 / 32 | Volume above / below threshold |
| 33 | Customer billing index |
| 34 | Time-of-use index 2 |
| 35 | Customer/event log |
| 36 | Current flow |
| 37 | Peak table |
| 38 / 39 | Irrigation volume / threshold FDR |

These definitions suggest that a reader could request selected fields instead
of a full predefined frame. That is not yet a tested transaction in this project.

### Smaller legacy reads

The driver defines `0x41` for an index with diagnostic information and `0x43`
for the meter clock. Their benefit over the existing `0x40` read has not been
tested here.

## Further research and test data

The smallest next step would be to decode more of the diagnostic, backflow and
leakage fields already present in standard responses. Block reads and frame 6
could then be investigated if a specific feature needs another transaction.
These notes do not add any new commands to the firmware. Configuration writes,
clock setting, resets and probing unknown commands remain outside this work.

The [Full FDR fixture](../test/fixtures/full_fdr/README.md) is fully synthetic,
not an anonymised household capture. Tests cover frame sizes, CRCs, malformed
responses, fragment ordering, configured periods, encodings and preservation
of the standard request. Real captures can contain identities, consumption,
dates and access information; keep them out of repository fixtures.
