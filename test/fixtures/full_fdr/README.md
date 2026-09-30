# Synthetic Full FDR pair

`synthetic_pair.json` is **fully synthetic**, not a recording and not an
anonymised capture. Every meter identity, reading, configuration value, counter,
and alarm value was invented. The protocol layout was checked against Android
Driver Service 3.1.6 and 3.1.9 and physical monthly-meter responses. No driver
binaries or source are included. The fixture contains no real household data
or observation timestamps.

The JSON has three sections:

- `provenance`: explicitly identifies the pair as synthetic.
- `expected`: decoded values, including all 45 consumptions in newest-first order.
- `frames`: frame selectors 7 and 8, complete decoded response bytes in
  `decoded_hex`, and optional unencoded requests in `request_hex`.

Hex strings use space-separated bytes. They are protocol bytes before serial
encoding or RF oversampling; they are not raw radio samples. Response lengths
are 137 and 139 bytes, including their CRCs. Both responses use a 16-byte
envelope, with byte 15 set to `01` and communication status at byte 16 set to
zero. Frame 7's two opaque suffix bytes are invented `00 00`; they are not
assigned any meaning. Frame 8 has no such suffix.

The invented meter address is `45 15 01 E2 40` (year 21, serial 123456). The
master address `45 20 0A 50 14` is the reader's protocol address. Requests use
command `70`, seven zero ATS bytes, zero access code, and selector `07` or `08`.
They are examples for offline tests, not instructions to transmit to a meter.

The invented current index is 123456 and FDR global index is 120000. The
configuration is monthly, day 1, hour 0, resolution code 0 (signed 32-bit,
45-slot capacity), and turn-factor code 0 (multiplier 1). PulseMedium `16 04`
denotes water with a one-litre pulse for this example. No calendar dates are assigned to intervals.

Consumptions are `100 + 7*i` for `i = 0..44` in newest-first order. Each is a
little-endian signed 32-bit integer. On the wire, the first 22 values are
reversed into frame 7 offsets 39–126, and the remaining 23 values are reversed
into frame 8 offsets 37–128. Reverse these two blocks separately to recover
`expected.consumptions_newest_first`; reversing all 45 together is incorrect.
`expected.consumptions_raw_hex` preserves the concatenated 180 wire-order bytes.

Remaining fields are invented constants: MIU group 3, battery lifetime 84,
leakage threshold 250, RF counter bytes `03 02`, and billing indexes 110000 and
115000. Alarm, backflow, and leakage-history bytes are zero.

CRC-16/KERMIT covers every byte before the final two bytes, including the length
byte, and is stored least-significant byte first. The standard check value for
`123456789` is `2189`. The repository's `crc_kermit()` returns a byte-swapped
integer, which its callers write most-significant byte first to produce the
same wire bytes.

This fixture covers a monthly archive. Other periods and encodings are exercised
by synthetic host tests; their slot capacities are described in the
[Full FDR guide](../../../docs/full-fdr.md). Physical validation is limited to
monthly history on an EverBlu Cyble Enhanced V2.1.
