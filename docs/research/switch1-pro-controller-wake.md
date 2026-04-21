# Simplest path: wake a Switch 2 from a Switch 1 Pro Controller

## The honest answer

**Don't mod the controller.** Use a standalone ESP32 dongle you keep on a keychain or near the Switch 2.

The Switch 2 doesn't care which device transmits the wake beacon — only that the beacon contains its own BT address and comes from a source MAC that's previously bonded to it. Modding the Switch 1 Pro Controller is aesthetically nice but functionally identical to pressing a button on a $3 ESP32 board you carry separately.

## Why the Switch 1 Pro Controller can't wake a Switch 2 natively

Not a radio limitation — a payload mismatch + locked firmware.

The Pro Controller uses a Broadcom **BCM20734**, which is a **dual-mode (BR/EDR + BLE 4.1)** chip — the same family used in Joy-Cons. It is fully capable of transmitting on BLE advertising channels 37/38/39 (2402/2426/2480 MHz), and in fact already does so to wake a paired Switch 1 from sleep (confirmed by SDR capture in [MissionControl#199](https://github.com/ndeadly/MissionControl/issues/199), 2021).

What blocks Switch 2 wake:

1. **Different PDU type and packet structure.** SDR captures from MissionControl#199 show the Pro Controller's wake uses `ADV_DIRECT_IND` (PDU type `0x1`) with the Pro Controller's BT MAC as advertiser address — *not* the Joy-Con-style `ADV_IND` with VID/PID/manufacturer-data payload that the Switch 2 wake filter expects. See [`procon-wake-decode.md`](procon-wake-decode.md) for the byte-level decode.
2. **Wrong target MAC.** A Pro Controller paired to a Switch 1 will direct the wake at the *Switch 1's* BT MAC, not the Switch 2's.
3. **Stock firmware is partially modifiable, but the wake adv code isn't in the modifiable region.** The BCM ROM is encrypted; the Pro Controller's patch RAM (SPI `0x10000`+) is plain ARM Thumb code (see [`mfro/switch-controller-testing`](https://github.com/mfro/switch-controller-testing) `tmp0`/`tmp0.hop`). However, a byte-pattern hunt across the full 524 KB SPI dump ([`firmware-hunt-results.md`](firmware-hunt-results.md)) finds **none** of the wake adv's distinctive constants in flash — the adv mfr data is constructed at runtime by ROM code, not stored as a template. To modify the wake adv shape from the controller side you'd need to hook a ROM function from patch RAM, which requires BCM20734 ROM analysis that hasn't been done publicly. See [`firmware-analysis.md`](firmware-analysis.md) for the corrected scope.

So: physically possible, practically infeasible at the controller-firmware level. Hence the ESP32 paths below.

**But there's a new option opened up by the SDR analysis**: an ESP32 can passively *detect* the Pro Controller's wake adv on-air and react by firing the Switch-2-shaped beacon itself. The Pro Controller becomes a "remote button" without any modification. See [`procon-wake-decode.md`](procon-wake-decode.md) and the test-matrix protocol in [`procon-trigger-test.md`](procon-trigger-test.md).

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
