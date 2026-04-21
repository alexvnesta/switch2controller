# Simplest path: wake a Switch 2 from a Switch 1 Pro Controller

## The honest answer

**Don't mod the controller.** Use a standalone ESP32 dongle you keep on a keychain or near the Switch 2.

The Switch 2 doesn't care which device transmits the wake beacon — only that the beacon contains its own BT address and comes from a source MAC that's previously bonded to it. Modding the Switch 1 Pro Controller is aesthetically nice but functionally identical to pressing a button on a $3 ESP32 board you carry separately.

## Why the Switch 1 Pro Controller can't wake a Switch 2 natively

Three compounding reasons: the controller doesn't emit BLE at all on stock firmware, the firmware that would need modifying is write-protected, and even if modified the wake-adv construction code lives in encrypted ROM.

The Pro Controller uses a Broadcom **BCM20734**, which is a **dual-mode (BR/EDR + BLE 4.1)** chip — the same family used in Joy-Cons. The radio hardware is fully capable of transmitting BLE advertising on channels 37/38/39. But on stock firmware (fw 4.33) it **never does** — it uses only classic BT (BR/EDR) paging.

What blocks Switch 2 wake:

1. **The controller emits zero BLE during wake/reconnect.** Triple-confirmed empirically ([`procon-trigger-test-results.md`](procon-trigger-test-results.md)): ESP32 BLE scanner captured 8586 BLE adv events from the surrounding environment across 3 minutes of full-spectrum passive scan with 19 unique devices visible — **zero from the Pro Controller's MAC** across multiple HOME-press cycles in multiple bonded/unbonded states. The controller wakes paired Switch 1 consoles via classic-BT paging (1 Mbps GFSK addressed to the bonded host's BD_ADDR via a Channel Access Code), which an ESP32 BLE radio cannot sniff. An earlier hypothesis based on [MissionControl#199](https://github.com/ndeadly/MissionControl/issues/199)'s SDR screenshots was that the wake used BLE `ADV_DIRECT_IND`; that interpretation was superseded — the `0x4A9A254E`-shaped pattern HelloRayH captured was almost certainly a classic-BT Channel Access Code, since both BLE access addresses and BR/EDR sync words share Bluetooth-spec ancestry. See [`procon-wake-decode.md`](procon-wake-decode.md) for the superseded decode and [`procon-trigger-test-results.md`](procon-trigger-test-results.md) for the falsifying test.

2. **Wrong target MAC and wrong PID regardless of transport.** A Pro Controller paired to a Switch 1 targets the Switch 1's BD_ADDR. Even if we somehow redirected it at the Switch 2's BD_ADDR, the Switch 2 accepts wake adv only from controllers advertising Nintendo PID `0x2066`/`0x2067`/`0x2069` (Joy-Con 2 L/R, Pro Controller 2), not `0x2009` (Switch 1 Pro Controller).

3. **Patch RAM is write-protected on fw 4.33.** The BCM ROM is encrypted; the Pro Controller's patch RAM (SPI `0x10000`+) is plain ARM Thumb code (see [`mfro/switch-controller-testing`](https://github.com/mfro/switch-controller-testing) `tmp0`/`tmp0.hop`). But on fw 4.33, writes to both DS1 (`0x10000`) and DS2 (`0x28000`) are rejected with status `0x01` "write protected" by subcommand `0x11`, while writes to the color region (`0x6050`) succeed — so the SPI write protocol is working, Nintendo just region-locked patch RAM. See [`spi-write-test-results.md`](spi-write-test-results.md). shinyquagsire23's 2017 work predates this lock.

4. **Even if patch RAM were writable, the wake adv construction code isn't there.** A byte-pattern hunt across the full 524 KB SPI dump ([`firmware-hunt-results.md`](firmware-hunt-results.md)) finds **none** of the wake adv's distinctive constants in flash — the adv mfr data is constructed at runtime by ROM code, not stored as a template. To change what the controller emits you'd need to hook a ROM function from patch RAM, which requires BCM20734 ROM symbol discovery that hasn't been done publicly (InternalBlue did this for BCM4339/43430; same SDK family but different chip).

So: physically possible on the radio, blocked at every software layer. Hence the ESP32 paths below.

**The once-promising "scan-and-react" option is dead.** Earlier design thinking imagined an ESP32 passively detecting the Pro Controller's wake adv on-air and reacting by firing the Switch-2-shaped beacon. Empirically ruled out — no BLE adv to detect. Path A (pair the controller TO an ESP32 over classic BT and relay HOME presses) is the top viable no-mod path. See [`../status-and-next-steps.md`](../status-and-next-steps.md).

## Ranked paths (simplest first)

### 1. Flash an ESP32 with `tv/switch2-wake-up` as a standalone dongle

Zero hardware work.

Requirements:
- Any ESP32-C3 or ESP32-C6 board (XIAO, SuperMini, QT Py — all work)
- macOS with `pip install esphome` OR use ESP-IDF directly
- Your Switch 2's BT MAC address (one-time capture)
- A bonded source MAC (e.g., from your Joy-Con — one-time capture)

Effort: one evening. Power: USB or small LiPo. Button press → 2-second wake burst → deep sleep.

### 2. Flipper Zero

If you already own one. tv's repo includes a Flipper app (`flipper_app/`). Same functionality, same packet.

Not worth buying a Flipper ($170) just for this.

### 3. Embed ESP32 inside the Switch 1 Pro Controller

**Cosmetic, not functional.** You solder a XIAO ESP32-C3 to the controller's 3.7V battery pads, wire a GPIO to an unused button pad (or piggyback HOME), and stuff it in the grip cavity. You now press the controller's button instead of a dongle's button.

Effort: days, with risk of damaging the controller. Requires soldering skill and patience.

Guides / references:
- [iFixit Pro Controller battery replacement](https://www.ifixit.com/Guide/Nintendo+Switch+Pro+Controller+Battery+Replacement/127714) — same teardown you'd do
- Battery is CTR-003, 3.7V Li-ion, 1300 mAh
- XIAO ESP32-C3 fits behind the battery or in the grip cavity (21 × 17.5 mm)

## What you need to capture from your hardware

To fill in the `wake-packet.md` template for YOUR Switch 2:

### Your Switch 2's BT address
Options:
- Pair it with a Mac / Linux / Android device; the OS will show the BT address in the paired-devices list.
- Use the ESP32 in scan mode to log nearby BLE devices and identify the Switch.
- Look in the Switch 2's System Settings → Device Info (if exposed — TBD).

Convert to reversed byte order for field H. E.g., `98:E2:55:99:1F:97` → `97 1F 99 55 E2 98`.

### A source MAC bonded to your Switch 2
This is any controller's BT MAC that you've already paired to that Switch 2. Options:
- **Your Joy-Con** — on a Mac, pair the Joy-Con via System Settings → Bluetooth while holding the sync button, then read the MAC from the paired devices list. (You'll need to un-pair from the Switch first, or pair it with both — this may confuse things.)
- **A sacrificial controller** — pair a cheap BLE controller to the Switch 2 once, note its MAC, then use that MAC as the ESP32's source address.
- **Skip this** — worth testing whether an unbonded random MAC works. Current implementations bake in a bonded MAC but it's not confirmed that bonding is *required* for wake, only that it's what both authors did.

## Recommended first-try hardware

- **Seeed XIAO ESP32-C6** (~$8) — 21 × 17.5 mm, USB-C, BLE 5.3, microamps in deep sleep
- Built-in boot button → trigger wake burst
- USB-C for power and flashing

If you want smaller: **ESP32-C3 SuperMini** (~$3) is slightly larger (22.5 × 18 mm) but cheaper.
