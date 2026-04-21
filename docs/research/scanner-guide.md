# How to capture your Switch 2's MAC addresses

You need two things before the wake firmware will work for YOUR Switch 2:

1. **Your Switch 2's Bluetooth MAC** (the target — the thing that wakes up)
2. **A Joy-Con's Bluetooth MAC** (the source — the ESP32 impersonates this)

Both values come from a single BLE scan.

## Why you can't just look this up

- The Switch 2 doesn't display its BT MAC in settings (only the Wi-Fi MAC)
- The BT MAC is NOT derivable from the Wi-Fi MAC (Nintendo assigns them independently)
- The Switch 2 itself doesn't advertise on BLE (it's a central, not peripheral)

**But**: Joy-Cons broadcast the Switch 2's BT MAC inside their own advertising packets every time they disconnect. So we scan Joy-Con advertisements and extract the Switch 2's MAC from them.

## Procedure

### 1. Swap in the scanner firmware

```bash
cd esp32
mv src/switch2_wake.c src/switch2_wake.c.disabled
mv src/ble_scanner.c.disabled src/ble_scanner.c
```

### 2. Build, flash, and open the serial monitor

```bash
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor
```

You should see the scanner start up and its LED go solid on. It's now scanning for any BLE device with a Nintendo OUI (`E0:EF:BF:*`) or Nintendo manufacturer data patterns.

### 3. Capture Joy-Con advertisements

Two options that each work:

**Option A — Sleep-mode capture (recommended)**

1. Put the Switch 2 to **Sleep** (short-press power button → Sleep Mode)
2. Detach one Joy-Con from the console
3. Watch the serial log — the Joy-Con will start advertising as it tries to reconnect
4. Wait ~5 seconds, note the output
5. Reattach the Joy-Con
6. Repeat with the other Joy-Con if you want to compare

This captures packets with **byte 16 = `0x81`** — the state the wake firmware needs.

**Option B — Change Grip/Order capture**

1. On Switch 2: System Settings → Controllers & Accessories → **Change Grip/Order**
2. Detach and reattach a Joy-Con

This captures packets with **byte 16 = `0x00`** — pairing state, NOT the state the wake firmware needs. You'd need to change byte 16 to `0x81` manually when configuring.

Use Option A whenever possible.

### 4. Decode the captured packet

You'll see serial output like this:

```
★ E0:EF:BF:12:34:56  RSSI=-41  type=0  evt=ADV_IND  [mfg data]
        raw: 02 01 06 1B FF 53 05 01 00 03 7E 05 66 20 00 01 81 BC 9A 78 BF EF E0 0F 00 00 ...
```

The ★ line gives you the **Joy-Con's MAC**: `E0:EF:BF:12:34:56`

The raw bytes give you the rest. Here's the byte-by-byte meaning:

| Offset | Bytes | What it is |
|---|---|---|
| 0-2 | `02 01 06` | Flags (ignore) |
| 3-4 | `1B FF` | Manufacturer data header (ignore) |
| 5-6 | `53 05` | Nintendo company ID (ignore) |
| 7-11 | `01 00 03 7E 05` | Header + Nintendo VID (ignore) |
| **12-13** | `66 20` | **PID, little-endian: `0x2066`** |
| **14-16** | `00 01 81` | **Byte 16 should be `0x81` for wake** |
| **17-22** | `BC 9A 78 BF EF E0` | **Switch 2 BT MAC, reversed** |
| 23-30 | `0F 00 00 00 00 00 00 00` | Pad |

To get the Switch 2's BT MAC: reverse bytes 17-22.
- Raw: `BC 9A 78 BF EF E0`
- Reversed: `E0 EF BF 78 9A BC`
- As MAC: **`E0:EF:BF:78:9A:BC`**

### 5. Put your values in secrets.h

```bash
cp src/secrets.h.example src/secrets.h
$EDITOR src/secrets.h
```

Fill in:
- `TARGET_SWITCH_MAC_BYTES` — your Switch 2 BT MAC in reading order, e.g. `{0xE0, 0xEF, 0xBF, 0x78, 0x9A, 0xBC}`
- `SOURCE_JOYCON_MAC_BYTES` — the Joy-Con MAC from the ★ line, in reading order
- `SOURCE_JOYCON_PID` — the PID you decoded from bytes 12-13 (reverse-endian), e.g. `0x2066`

### 6. Swap back and flash the wake firmware

```bash
mv src/ble_scanner.c src/ble_scanner.c.disabled
mv src/switch2_wake.c.disabled src/switch2_wake.c
~/.platformio/penv/bin/pio run -t upload
```

Press BOOT. Switch should wake.

## Troubleshooting

**I don't see any ★ lines during the scan.**
- Make sure the Joy-Con is actually detached from the console. Attached Joy-Cons stay connected and don't advertise.
- Try Change Grip/Order mode (Option B above) — that forces advertising.
- Move the ESP32 physically closer to the Joy-Con.

**I see ★ lines but the raw bytes don't look right.**
- If the raw is short (<30 bytes) you're seeing a scan response, not the primary advertisement. Wait for more packets — primary ones come at a ~20 ms interval.
- If byte 16 is `0x00`, you're in pairing mode. Use Option A (Sleep mode) instead.

**Multiple ★ devices show up.**
- That's expected — you probably have both Joy-Cons advertising. Pick one as your source. Either works; the PID in your `secrets.h` must match the one from that same Joy-Con.

**The wake firmware boots but pressing BOOT doesn't wake the Switch.**
- Verify `secrets.h` values byte-by-byte against the raw scan bytes
- Confirm byte 16 = `0x81` (check via serial: the hardened firmware logs `state_flag: 0x81`)
- Make sure the target Switch 2 is in **Sleep** (screen dark, LED off) — fully-off consoles don't wake-on-BLE
- Keep the ESP32 within a few meters
