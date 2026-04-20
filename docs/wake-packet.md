# The Switch 2 Wake Packet

Complete byte-by-byte breakdown of the BLE advertising beacon that wakes a Switch 2 from sleep.

## The 31 bytes

```
02 01 06  1B FF  53 05  01 00 03  7E 05  66 20  00 01 81
--------  -----  -----  --------  -----  -----  --------
   A        B      C        D       E      F       G

97 1F 99 55 E2 98  0F 00 00 00 00 00 00 00
-----------------  -----------------------
        H                    I
```

| Field | Offset | Bytes | Meaning |
|-------|--------|-------|---------|
| **A** | 0 | `02 01 06` | BLE AD structure: Flags = LE General Discoverable, BR/EDR Not Supported |
| **B** | 3 | `1B FF` | BLE AD structure: length 0x1B = 27 bytes follow, type 0xFF = Manufacturer Specific Data |
| **C** | 5 | `53 05` | Company ID 0x0553 (little-endian) |
| **D** | 7 | `01 00 03` | Header bytes — unknown purpose, constant across captures |
| **E** | 10 | `7E 05` | Nintendo VID 0x057E (little-endian) |
| **F** | 12 | `66 20` | Joy-Con L PID 0x2066 (little-endian) |
| **G** | 14 | `00 01 81` | **Byte 16 = `0x81` is the wake-trigger flag**. Observed `0x00` during Change Grip/Order (pairing mode) and `0x81` during Sleep-mode reconnect attempts. Switch 2 only wakes on `0x81`. Bytes 14-15 constant across all captures. |
| **H** | 17 | 6 bytes | **★ Target Switch 2's Bluetooth MAC address, reversed byte order** |
| **I** | 23 | `0F 00 00 00 00 00 00 00` | Trailing padding |

## Per-device customization

**Only field H varies.** That is the target Switch 2's BT MAC. Everything else is constant.

If a Switch's BT address is `98:E2:55:99:1F:97`, field H contains `97 1F 99 55 E2 98` (reversed).

## Source-advertiser MAC

The BLE frame's own source address (the MAC the ESP32/Flipper advertises as) must match a MAC that has been **bonded** to the target Switch 2. In practice this means the source MAC of a real controller (Joy-Con, Pro Controller 2, 8BitDo, etc.) that you've already paired.

- `tv/switch2-wake-up` ESPHome version uses base MAC `98:E2:55:AE:F7:D6`
- `tv/switch2-wake-up` Flipper version uses `D8:F7:AE:55:E2:98`
- `Minkelxy/xiaoai_switch2_wake_up` uses a custom `78:81:8C:06:9A:C4` (an 8BitDo OUI)

These are each author's own bonded-device addresses. **You need yours.**

## Radio parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Advertising channels | 37, 38, 39 | All three (2402, 2426, 2480 MHz). BLE primary advertising. |
| Advertising type | `ADV_TYPE_IND` (connectable) or `ADV_TYPE_NONCONN_IND` | Both confirmed working |
| Interval | ~20–50 ms | tv uses 50 ms; Minkelxy uses 20–40 ms (0x20–0x40 in 0.625 ms units) |
| Burst duration | 1–2 seconds | Long enough for the Switch's intermittent wake-scan to catch the beacon |

## What's NOT required

- Pairing handshake / SMP exchange
- GATT services
- LTK (long-term key) possession
- Specific HID descriptor
- The BlueRetro-documented Nintendo challenge blobs (those are for the *console*-impersonation direction, not wake)

This is the biggest insight: the "reattach Joy-Con" ritual vendors mention is irrelevant for wake. Wake is a simple unencrypted beacon, authenticated only by the fact that the source MAC must already be bonded.

## Sources

- [tv/switch2-wake-up](https://github.com/tv/switch2-wake-up) — captured with nRF BLE sniffer; `switch2_wake.cpp` and `flipper_app/switch2_wake_test.c` show the same 31-byte payload with different per-Switch addresses
- [Minkelxy/xiaoai_switch2_wake_up](https://github.com/Minkelxy/xiaoai_switch2_wake_up) — independent implementation; `src/main.cpp` line 67 has an annotated breakdown
- [HelloRayH's SDR analysis](https://github.com/ndeadly/MissionControl/issues/199) — first public confirmation of CH37/38/39 wake mechanism and that "the wake-up code is only related to the host" (the Switch 2's BT address)
