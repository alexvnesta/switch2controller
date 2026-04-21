# Setup: flashing the wake beacon firmware

End-to-end instructions for getting the `esp32/` wake-beacon firmware running against your Switch 2. Assumes you're starting from nothing.

## What you need

- An ESP32 dev board (classic ESP32-WROOM-32 recommended; also works on ESP32-C3/C6)
- A USB cable
- A Switch 2
- At least one Joy-Con paired to that Switch 2
- PlatformIO (VS Code extension or CLI)

## Step 1: Capture your MAC addresses

Two values need capturing: your **Switch 2's BT MAC** and a **paired Joy-Con's BT MAC**. Both come from the same scan.

```bash
cd esp32
# Enable scanner build (quick swap)
mv src/switch2_wake.c src/switch2_wake.c.disabled
mv src/ble_scanner.c.disabled src/ble_scanner.c
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor
```

With the scanner running and the Switch 2 in Sleep mode:

1. **Detach a Joy-Con** — the Joy-Con will briefly broadcast advertising packets trying to reconnect. The scanner will log them, highlighted with `★`.
2. You'll see output like `★ AA:BB:CC:DD:EE:FF  RSSI=-42  evt=ADV_IND` with a raw-byte dump on the next line.
3. The **Joy-Con's own MAC** is on the `★` line.
4. The **target Switch 2 BT MAC** is encoded in the raw bytes at positions 17-22, **reversed**. E.g., raw bytes `BC 9A 78 BF EF E0` → Switch BT MAC `E0:EF:BF:78:9A:BC` (example only — yours will differ).
5. The **PID** is at positions 12-13 (little-endian). E.g., `66 20` → `0x2066`.

For the long-form walkthrough with troubleshooting, see [`research/scanner-guide.md`](research/scanner-guide.md).

## Step 2: Configure the wake firmware

```bash
# Swap back to wake firmware
mv src/ble_scanner.c src/ble_scanner.c.disabled
mv src/switch2_wake.c.disabled src/switch2_wake.c

# Create your secrets file from the template
cp src/secrets.h.example src/secrets.h
$EDITOR src/secrets.h   # fill in the values you captured
```

Your `secrets.h` is gitignored, so it stays on your machine.

## Step 3: Flash and wake

```bash
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor
```

Press the **BOOT button** on the ESP32 — the LED goes solid ON for 2 seconds, and your Switch 2 should wake up.

## Button gestures

| Action | Result |
|---|---|
| Short press BOOT | Fire wake burst (2 s) |
| Long press BOOT (3 s) | Clear NVS config, force re-read from secrets.h next boot |
| 3 fast blinks at boot | Firmware alive, config loaded |
| 5 fast blinks | BLE error |

## Troubleshooting

- **"No `★` events appear in the scanner"** — the Joy-Con has to actually try to reconnect. Detaching it from the Switch (not just putting the Switch to sleep) is what triggers the advertising burst. You can also power-cycle the Joy-Con by sliding it off and back on.
- **"Wake burst fires but Switch doesn't wake"** — most common cause is a wrong source MAC or PID. The source MAC must be a Joy-Con that's **currently bonded** to the target Switch. The payload must contain that Joy-Con's PID (`0x2066`/`0x2067`/`0x2069`).
- **"Byte order confusion"** — the Switch 2's BT MAC appears **reversed** in the wake payload. The scanner shows the Joy-Con's own MAC in reading order; the Switch 2's MAC is encoded reversed inside the manufacturer data at offset 17-22. Your `secrets.h` template documents the expected order.

## Deeper reading

- [`research/wake-packet.md`](research/wake-packet.md) — full byte-level breakdown of the wake beacon
- [`research/findings.md`](research/findings.md) — all established protocol facts with evidence
