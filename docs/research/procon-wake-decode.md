# Switch 1 Pro Controller wake packet — decoded from MissionControl#199 SDR captures

Source: [HelloRayH's screenshots in ndeadly/MissionControl#199](https://github.com/ndeadly/MissionControl/issues/199), 2021. Three independent captures: an SDR waveform with hex bytes, a logic-analyzer trace, and a repetition-pattern capture.

> **Important caveat (added after we found ndeadly's full reference spec):** This decode is for the **Switch 1** Pro Controller paired to a **Switch 1**, derived solely from HelloRayH's SDR screenshots without ground-truth verification. ndeadly's [`switch2_controller_research/bluetooth_interface.md`](https://github.com/ndeadly/switch2_controller_research/blob/master/bluetooth_interface.md) authoritatively documents the **Switch 2** controller wake adv as `ADV_IND` (not `ADV_DIRECT_IND`) with a structured 26-byte manufacturer data payload. The Switch 1 protocol may follow the same `ADV_IND`+manufacturer-data pattern, in which case our `ADV_DIRECT_IND` parse below is wrong (a misalignment of the hex grouping in HelloRayH's screenshots). Treat the PDU-type identification in this doc as **provisional** until empirically confirmed with our own scan. The byte-level *content* (access address, advertiser MAC) is reliable; the PDU type is not.

## What we knew before

HelloRayH established by SDR + 2.4 GHz RF chip capture:
- Pro Controller wakes Switch 1 via BLE advertising channels 37/38/39 (2402/2426/2480 MHz)
- The "wake code" is host-dependent (function of host BT MAC); same controller paired to two hosts emits two different codes
- The wake code can be replayed (no nonce / challenge-response)

What he didn't fully decode: the byte-level structure of the packet, or what the BLE "access code" he was trying to derive actually was.

## What the screenshots show

### Capture 1 — SDR with hex extraction

Two consecutive bursts captured from the same Pro Controller HOME-press:

```
Burst A: 7a6bfaaa 4a9a254e 612489e5 400fc0e3 8fff0068f8033a496913f73d09470407000000000000000001c205c5efae1a54b5e500000000...
Burst B: ...020000003faaa 4a9a254e 612489e5 400fc0e3 8fff00637213983ccddd73c93f438...
```

### Capture 2 — Logic analyzer (digital tap inside controller)

Channel 0 shows raw bytes flowing on what is almost certainly the BCM20734's SPI/UART debug interface:
```
... 25 4e 4a 9a 25 4e 4a 9a 25 4e 4a 9a ...
```

This is the BLE access address being clocked out, repeatedly, by the BCM chip during the wake transmission. Matches the SDR capture's `4a9a254e` segment exactly.

### Capture 3 — Repetition pattern

```
f3ca 254e4a9a 254e4a9a 254e4a9a 254e4a9a ... (61 selected, 30.50 µs)
```

Just the access address `0x4A9A254E` repeating — likely a pre-burst sync / preamble extension specific to the Pro Controller's wake transmission, or an artifact of the SDR capture window catching the same access-address-bearing PDU repeatedly across the three adv channels.

## Byte-level decode of Burst A

Mapping against the BLE Core Spec advertising PDU layout (LE 1M PHY, on-air bit order):

| Offset | Bytes | Field | Interpretation |
|---|---|---|---|
| - | `7a 6b` | Capture noise | Pre-trigger SDR samples, ignore |
| 0 | `fa aa` | Preamble | Standard BLE adv preamble (`0xAA` alternating bits, the `fa` is the trailing edge of the access-address sync) |
| 2 | `4a 9a 25 4e` | **Access address** | When bit-reversed per byte → `0x8E89BED6`, the **canonical BLE advertising access address** defined in the Bluetooth Core Spec |
| 6 | `61` | PDU header byte 0 | Type bits = `0001` = `ADV_DIRECT_IND`; ChSel=1; TxAdd=1 (random advertiser); RxAdd=0 (public target). *(See note below — payload length suggests this parse may not be exact, but PDU type is consistent.)* |
| 7 | `24` | PDU header byte 1 | Length field — exact value depends on PDU type interpretation |
| 8 | `89 e5 40 0f c0 e3` | AdvA | Pro Controller's BT MAC, little-endian → `E3:C0:0F:40:E5:89` |
| 14 | `8f ff 00 68 f8 03 3a 49 69 13 f7 3d 09 47 04 07 ...` | Payload | Either TargetA (if pure `ADV_DIRECT_IND`, 6 bytes) followed by trailing capture data, or a custom payload extension |
| end | `... c2 05 c5 ef ae 1a 54 b5 e5 ...` | CRC + capture trailer | BLE PDU CRC followed by SDR capture noise |

## Key findings

`0x4A9A254E` is `0x8E89BED6` bit-reversed. **`0x8E89BED6` is the standard BLE advertising-channel access address** from the Bluetooth Core Spec (Vol 6, Part B, §2.1.2). HelloRayH's open question — *"how do I generate the correct access code so that the Bluetooth chip can receive the response packet from the switch?"* — was the BLE access address all along. He just didn't recognize it because his tooling displayed bits in on-air LSB-first order.

**This means there is nothing exotic about the Pro Controller's wake transmission.** It is standard BLE adv. Any BLE scanner (including a $3 ESP32) can see it, no special hardware needed. The reason Pro Controller didn't show up in our prior session's BLE scan ([`../sessions/session-2026-04-20.md`](../sessions/session-2026-04-20.md)) is that the controller was bonded to and connected over classic BT to the Switch 2, not in a "trying to wake a sleeping/missing host" state where the BLE wake adv fires.

**The PDU type is `ADV_DIRECT_IND`** (or a close variant), not the Joy-Con's `ADV_IND`. This is structurally simpler — `ADV_DIRECT_IND` is a directed advertisement: "Hey host with this exact MAC, your bonded peer wants to reconnect." The Switch 1's BT chip wakes when it sees an `ADV_DIRECT_IND` addressed to its own MAC from a bonded source MAC. No structured payload, no PID, no Nintendo VID needed.

**Payload differs between bursts from the same press** (`8fff0068f8033a4969...` vs `8fff00637213983ccddd...`). Possible explanations: (a) a sequence counter or anti-replay nonce — but we already know from HelloRayH that replay works, so probably not; (b) the captures are from different adv channels (37/38/39) and channel-specific framing differs slightly; (c) trailing bytes after the actual PDU end are SDR capture noise.

## Implications

### For the wake-Switch-2-from-Pro-Controller goal

The Pro Controller's wake packet is *fundamentally different in shape* from the Switch 2 wake beacon (`ADV_DIRECT_IND` directed-wake vs `ADV_IND` Joy-Con-impersonation). Even with perfectly captured Pro Controller wake bytes, **you cannot just rewrite the host MAC field and replay it to the Switch 2** — the Switch 2's wake filter is looking for the Joy-Con-style undirected adv with specific VID/PID/state-flag bytes.

But this is fine for the **scan-and-react** path: the ESP32 doesn't need to *replay* the Pro Controller's packet, only to *detect* that the Pro Controller emitted one. On detection → fire the Switch-2-shaped beacon we already know works.

### For the Switch 2's wake mechanism

Open question worth testing: does the Switch 2 *also* support `ADV_DIRECT_IND` wake from any bonded MAC? If yes, then a much simpler wake packet would work — no Joy-Con impersonation needed. But the documented `tv/switch2-wake-up` and `Minkelxy` implementations both use `ADV_IND` with the Joy-Con payload, which suggests this is the only path the Switch 2 accepts. (Or nobody tested directed-wake.)

### For ESP32 detection

A standard NimBLE scanner with `filter_duplicates=0` and `passive=1` will see this packet. The match criterion is:

- `event_type == 0x01` (`ADV_DIRECT_IND`)
- `addr.val` matches the Pro Controller's BT MAC

That's it. No payload parsing required. See [`procon-trigger-test.md`](procon-trigger-test.md) for the test protocol.

## Caveats

- **PDU type is provisional, not confirmed.** The header parse `0x61 0x24` doesn't match `ADV_DIRECT_IND`'s fixed 12-byte payload. ndeadly's authoritative Switch 2 spec uses `ADV_IND`. The likely explanation: byte alignment in HelloRayH's hex display is off, and the actual PDU is `ADV_IND` (PDU type bits `0000`, header byte starting with `0x40`/`0x60` family) carrying a Joy-Con-style manufacturer data payload, not a directed advertisement. The `ADV_DIRECT_IND` interpretation should be discarded if the empirical scan in [`procon-trigger-test.md`](procon-trigger-test.md) shows otherwise.
- **Implication if the Switch 1 Pro Controller actually uses `ADV_IND` like Switch 2 controllers do**: the wake adv would have its own structured manufacturer data — likely with PID `0x2009` (Switch 1 Pro Controller) and the Switch 1's host MAC at the equivalent of bytes 17-22. The "scan and react" path simplifies further: detection can match on PID `0x2009` + the byte-16 = `0x81` flag, exactly mirroring how ndeadly identifies Switch 2 wake advs.
- HelloRayH's "wake code is related to the Bluetooth MAC address of the host" finding still holds either way — whether the host MAC appears in `ADV_DIRECT_IND`'s TargetA field or in `ADV_IND`'s manufacturer data payload, the per-host variation he observed is the host-MAC field changing.
- The advertiser MAC (`E3:C0:0F:40:E5:89` from his capture) and the access address (`0x8E89BED6`, standard BLE adv) are reliable regardless of PDU-type interpretation.
- We don't have HelloRayH's setup or his original captures, only the screenshots. There may be additional structure he discovered but didn't post.
