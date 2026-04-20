# switch2controller

Wake a Nintendo Switch 2 from sleep using a cheap ESP32 as a BLE beacon — reverse-engineered from scratch.

**Status: wake-beacon working.** The `esp32/` firmware successfully wakes a Switch 2 when configured with the target Switch's BT MAC and a bonded Joy-Con's BT MAC.

**Open goal (not yet working)**: triggering the wake from a Switch 1 Pro Controller HOME press. See [`docs/status-and-next-steps.md`](docs/status-and-next-steps.md) for what was tried, what's blocked, and realistic paths forward.

## How it works

The Switch 2 listens on BLE advertising channels 37/38/39 during sleep, watching for a 31-byte advertisement containing:
- Nintendo's Company ID (`0x0553`) + USB VID (`0x057E`)
- A bonded Joy-Con's PID (`0x2066` or `0x2067`)
- A specific state flag byte (`0x81` = "paired peer reconnecting in sleep")
- The Switch 2's own BT MAC address (reversed)

When a beacon with this exact structure arrives from an address that has previously bonded with the Switch, the BT chip wakes the host. No encryption, no pairing handshake, no LTK.

Full byte-level breakdown: [`docs/wake-packet.md`](docs/wake-packet.md)

## Quick start

### What you need

- An ESP32 dev board (classic ESP32-WROOM-32 recommended; also works on ESP32-C3/C6)
- A USB cable
- A Switch 2
- At least one Joy-Con paired to that Switch 2
- PlatformIO (VS Code extension or CLI)

### Step 1: Scan to find your MAC addresses

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

See [`docs/scanner-guide.md`](docs/scanner-guide.md) for the step-by-step walkthrough.

### Step 2: Configure the wake firmware

```bash
# Swap back to wake firmware
mv src/ble_scanner.c src/ble_scanner.c.disabled
mv src/switch2_wake.c.disabled src/switch2_wake.c

# Create your secrets file from the template
cp src/secrets.h.example src/secrets.h
$EDITOR src/secrets.h   # fill in the values you captured
```

Your `secrets.h` is gitignored, so it stays on your machine.

### Step 3: Flash and wake

```bash
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor
```

Press the **BOOT button** on the ESP32 — the LED goes solid ON for 2 seconds, and your Switch 2 should wake up.

### Button gestures

| Action | Result |
|---|---|
| Short press BOOT | Fire wake burst (2 s) |
| Long press BOOT (3 s) | Clear NVS config, force re-read from secrets.h next boot |
| 3 fast blinks at boot | Firmware alive, config loaded |
| 5 fast blinks | BLE error |

## Repository layout

```
docs/
  wake-packet.md              Byte-by-byte packet breakdown
  scanner-guide.md            How to find your MACs with the scanner
  research-notes.md           Chronological research log
  firmware-analysis.md        Why firmware-diffing GuliKit didn't work
  session-2026-04-20.md       Full end-to-end session that led to success
  blueretro-fork-plan.md      Plan for Switch 1 Pro Controller integration
  switch1-pro-controller-wake.md   Overview of the Pro Controller wake problem
esp32/                        PlatformIO project
  src/
    switch2_wake.c            Wake beacon firmware (main)
    ble_scanner.c.disabled    Scanner firmware (swap with .c/.c.disabled)
    secrets.h.example         Template; copy to secrets.h and fill in
    secrets.h                 (gitignored) Your captured MAC values
  platformio.ini
  sdkconfig.defaults
references/
  tv/                         Source from github.com/tv/switch2-wake-up
  minkelxy/                   Source from github.com/Minkelxy/xiaoai_switch2_wake_up
firmware/                     Archived GuliKit firmware samples (BT chip encrypted; abandoned path)
```

## Credits

- [tv/switch2-wake-up](https://github.com/tv/switch2-wake-up) — first public ESP32 + Flipper implementations of the wake beacon
- [Minkelxy/xiaoai_switch2_wake_up](https://github.com/Minkelxy/xiaoai_switch2_wake_up) — independent implementation with annotated payload
- [HelloRayH in ndeadly/MissionControl#199](https://github.com/ndeadly/MissionControl/issues/199) — SDR-level confirmation that wake uses BLE advertising channels 37/38/39
- [darthcloud/BlueRetro](https://github.com/darthcloud/BlueRetro) — Switch 1 Pro Controller classic-BT HID host reference

## What this project adds

- Self-contained, documented, reproducible setup
- **Scanner firmware** that recovers your Switch 2's BT MAC from Joy-Con advertisements (no nRF sniffer needed)
- Discovered and documented **byte 16 state flag** (`0x00` pairing mode vs `0x81` sleep-reconnect) — this byte was already present in prior implementations but its state-dependent meaning wasn't documented
- NVS-backed runtime config (change MACs without reflashing)
- Planned: Switch 1 Pro Controller classic-BT integration so pressing HOME on the Pro Controller wakes the Switch 2

## Disclaimer

This is reverse engineering of a protocol Nintendo hasn't documented publicly. Use at your own risk. Not affiliated with Nintendo.
