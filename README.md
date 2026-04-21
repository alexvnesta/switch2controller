# switch2controller

Wake a Nintendo Switch 2 from sleep using a cheap ESP32 as a BLE beacon — reverse-engineered from scratch.

**Status: wake-beacon working.** The `esp32/` firmware successfully wakes a Switch 2 when configured with the target Switch's BT MAC and a bonded Joy-Con's BT MAC.

**Open goal (in progress)**: triggering the wake from a Switch 1 Pro Controller HOME press. As of 2026-04-21:
- Three viable paths identified: **A** (BlueRetro fork — scaffold landed at `procon-bridge/`), **B** (physical mod), **D** (sidestep via Wi-Fi/BLE remote)
- One bonus path opened up: **Tier 1** (modify Pro Controller firmware via patch RAM disassembly) — see [`docs/plans/tier1-disassembly-plan.md`](docs/plans/tier1-disassembly-plan.md)
- One path empirically killed: **Path 0** (BLE scan-and-react — Pro Controller emits no BLE)

See [`docs/status-and-next-steps.md`](docs/status-and-next-steps.md) for the full path table and recommended next moves.

## How it works

The Switch 2 listens on BLE advertising channels 37/38/39 during sleep, watching for a 31-byte advertisement containing:
- Nintendo's Company ID (`0x0553`) + USB VID (`0x057E`)
- A bonded Joy-Con's PID (`0x2066` or `0x2067`)
- A specific state flag byte (`0x81` = "paired peer reconnecting in sleep")
- The Switch 2's own BT MAC address (reversed)

When a beacon with this exact structure arrives from an address that has previously bonded with the Switch, the BT chip wakes the host. No encryption, no pairing handshake, no LTK.

Full byte-level breakdown: [`docs/research/wake-packet.md`](docs/research/wake-packet.md)

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

See [`docs/research/scanner-guide.md`](docs/research/scanner-guide.md) for the step-by-step walkthrough.

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
  status-and-next-steps.md    Top-level navigation: what's done, what to do next
  research/                   Topical findings (what we learned)
    findings.md               Comprehensive evidence summary
    wake-packet.md            Byte-by-byte Switch 2 wake adv breakdown
    switch1-pro-controller-wake.md  Why the Switch 1 Pro Controller is hard
    procon-trigger-test.md    Empirical test design
    procon-trigger-test-results.md  Empirical results killing Path 0
    procon-wake-decode.md     SDR-screenshot decode (provisional, superseded)
    spi-dump-diff-results.md  DS1-universal + DS2-OTA findings
    firmware-hunt-results.md  Byte-pattern search of mfro's SPI dump
    firmware-analysis.md      Firmware encryption scope
    references-inventory.md   What's in references/ and what we learned
    scanner-guide.md          How to find your MACs with the scanner
  sessions/                   Chronological research logs
    session-2026-04-20.md     Original wake-beacon development session
    session-2026-04-20-procon.md  Pro Controller investigation + dump
    research-notes.md         Earlier research log
  plans/                      Forward-looking work plans
    tier1-disassembly-plan.md  Patch RAM disassembly + "Hello BLE" custom firmware
    blueretro-fork-plan.md    Notes for Path A (BlueRetro fork)
    pro-controller-integration-plan.md  Earlier integration notes
    pro-controller-piggyback-mod.md  Path B (physical mod) plan
esp32/                        PlatformIO project (NimBLE-based wake beacon)
  src/
    switch2_wake.c            Wake beacon firmware (main)
    ble_scanner.c.disabled    Scanner firmware (swap with .c/.c.disabled)
    procon_scanner.c          Pro Controller wake-trigger detector (test matrix)
    secrets.h.example         Template; copy to secrets.h and fill in
    secrets.h                 (gitignored) Your captured MAC values
procon-bridge/                Fork of BlueRetro (BTDM + classic-BT HID host)
                              for Path A: pair Pro Controller to ESP32, fire
                              wake burst on HOME-press. Scaffold only.
                              See procon-bridge/README.md.
tools/                        Standalone analysis/dump utilities
  procon_spi_dump.py          Python SPI dumper (blocked by macOS HID protection)
  procon_spi_dump.html        WebHID SPI dumper (works on macOS via Chrome)
references/                   Cloned external projects (gitignored)
  tv/                                       github.com/tv/switch2-wake-up
  minkelxy/                                 github.com/Minkelxy/xiaoai_switch2_wake_up
  ndeadly-switch2-research/                 Authoritative Switch 2 BLE/HID protocol spec
  mfro-switch-controller-testing/           Pro Controller emulator + real Switch 1 SPI dump + disassembly
  BlueRetro/                                darthcloud/BlueRetro source
captures/                     Live captures from this project (gitignored)
firmware/                     Archived GuliKit firmware samples (BT chip encrypted; abandoned path)
```

## Credits

- [tv/switch2-wake-up](https://github.com/tv/switch2-wake-up) — first public ESP32 + Flipper implementations of the wake beacon
- [Minkelxy/xiaoai_switch2_wake_up](https://github.com/Minkelxy/xiaoai_switch2_wake_up) — independent implementation with annotated payload
- [HelloRayH in ndeadly/MissionControl#199](https://github.com/ndeadly/MissionControl/issues/199) — SDR-level confirmation that wake uses BLE advertising channels 37/38/39
- [ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research) — authoritative Switch 2 BLE/HID/pairing protocol spec, controller flash layout, and Switch homebrew SPI dumper. Independently confirms our wake-packet decode and adds large amounts of detail we didn't have. See [`docs/research/references-inventory.md`](docs/research/references-inventory.md) for what we learned from it.
- [darthcloud/BlueRetro](https://github.com/darthcloud/BlueRetro) — Switch 1 Pro Controller classic-BT HID host reference
- [mfro/switch-controller-testing](https://github.com/mfro/switch-controller-testing) — Pro Controller HID/SDP emulator (Linux + CSR/Intel BT dongle) and a public SPI dump + Hopper disassembly of a real Switch 1 Pro Controller. Useful for understanding the controller's patch RAM layer, even though the project's actual goal was input automation, not wake.
- [CTCaer/jc_toolkit](https://github.com/CTCaer/jc_toolkit) — Switch 1 Joy-Con / Pro Controller SPI backup/restore tool. Documents the patch-RAM addresses (`0x10000` / `0x28000`) and OTA signature magic at `0x1FF4`.
- [dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) — original Switch 1 controller HID/SPI/USB documentation

## What this project adds

- Self-contained, documented, reproducible setup
- **Scanner firmware** that recovers your Switch 2's BT MAC from Joy-Con advertisements (no nRF sniffer needed)
- Discovered and documented **byte 16 state flag** (`0x00` pairing mode vs `0x81` sleep-reconnect) — this byte was already present in prior implementations but its state-dependent meaning wasn't documented
- NVS-backed runtime config (change MACs without reflashing)
- Planned: Switch 1 Pro Controller classic-BT integration so pressing HOME on the Pro Controller wakes the Switch 2

## Disclaimer

This is reverse engineering of a protocol Nintendo hasn't documented publicly. Use at your own risk. Not affiliated with Nintendo.
