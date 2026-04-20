# Status and next steps

Last updated: 2026-04-20

## What works today

**The ESP32 wakes a Switch 2 from sleep.** This is the hard, novel part of the project and it's working end-to-end.

- Firmware: `esp32/src/switch2_wake.c` (NimBLE-based BLE advertiser, ~250 lines)
- Scanner firmware: `esp32/src/ble_scanner.c.disabled` (swap in to capture MACs for a new Switch)
- Flash target: ESP32-WROOM-32 classic dev board
- Trigger: BOOT button short press → 2s wake burst → Switch wakes
- Config: `secrets.h` (gitignored) for per-user MACs, with an NVS fallback so you don't have to reflash
- Documented byte-for-byte payload spec and discovery flow in `docs/`

## Open goal: "press HOME on Switch 1 Pro Controller → Switch 2 wakes"

The original motivation. We spent significant effort on this and hit a fundamental constraint:

**Classic Bluetooth is designed to prevent the thing we want to do.**

The Switch 1 Pro Controller is classic BT. When it wakes from HOME-press, it sends a **targeted page packet** to its bonded host (the Switch 2). The page:
- Is addressed to the Switch 2's BT MAC specifically (not broadcast)
- Hops on 32 frequencies in a pattern seeded by the Switch 2's clock estimate
- Is filtered at baseband level — a third device CANNOT receive pages not addressed to it

Nobody has built a passive classic-BT page sniffer on ESP32. The research (see `docs/session-2026-04-20-procon.md`) confirms this is infeasible without specialized hardware (Ubertooth One + weeks of Broadcom firmware reverse-engineering that has never been published).

### Three viable paths to the original goal (pick one for a future session)

| Path | Effort | Trade-off |
|---|---|---|
| **A**. Pair Pro Controller *to* ESP32 (not Switch 2) | 1-2 days | Controller becomes wake-only; can't use for Switch 2 gameplay |
| **B**. Physical mod: ESP32-C3 inside Pro Controller, GPIO-tapped to HOME button | 1 evening + soldering | Risk of damaging the controller; needs manual soldering to a tiny pad |
| **C**. Claim Switch 2's classic BT MAC and page-scan | 1-2 days | Causes pairing confusion during gameplay (rejected by user) |

Path B is the most pragmatic. The Pro Controller retains full functionality with the Switch 2, and a tiny ESP32-C3 (Seeed XIAO, ~$8) piggybacks on the battery, sleeps at ~43 µA, wakes on HOME press via a soldered GPIO tap, and fires the existing wake burst. Estimated passive drain: <3% of controller battery over a year of normal use.

## WIP branch: Bluedroid Pro Controller pairing

Commit `d49429d` on master contains a **partial** Bluedroid dual-mode migration that:
- Builds successfully (827 KB firmware)
- Discovers a Switch 1 Pro Controller in pairing mode (OUI `98:B6:E9`)
- Attempts HID L2CAP connection
- Fails with `SDP error 0x9` — Pro Controller refuses because SSP auth sequence isn't fully wired up

Commit `cbc0505` (master HEAD after revert) is the known-good NimBLE wake-only state.

To pick up the Pro Controller work:
1. `git checkout d49429d -- esp32/`
2. Finish the Bluedroid SSP auth flow (`ESP_BT_GAP_CFM_REQ_EVT` handler is added but needs testing)
3. Once auth completes, HID reports start arriving
4. Edge-detect HOME (bit 12 of byte 3 in report 0x3F, or byte 3-5 of report 0x30)
5. On HOME press → call `send_wake_burst()`

See `docs/pro-controller-integration-plan.md` for the detailed plan.

## Future work ideas

- **Wi-Fi + HTTP wake trigger**: ESP32 joins local Wi-Fi, `POST /wake` from phone/Home Assistant → fires beacon. Clean "wake from anywhere in the house" UX. ~2 hours work.
- **Cheap BLE controller as wake remote**: pair an 8BitDo Micro or similar BLE controller to ESP32, use any button on it to trigger wake. ~4 hours work.
- **Battery-powered deep-sleep dongle**: ESP32-C3 on a LiPo battery with physical wake button, keychain form factor. ~1 evening.
- **Pro Controller piggyback mod (Path B)**: see `docs/pro-controller-piggyback-mod.md` (to be written).
- **Configurable MACs over BLE**: runtime MAC configuration via a temporary BLE GATT service, no reflash needed for a new Switch.

## How this all came together

Full research log is in `docs/session-2026-04-20.md`. Short version:

1. Attempted to reverse-engineer the wake protocol by diffing GuliKit's pre-wake vs post-wake firmware. Dead end — firmware is AES-encrypted by the BT chip's ROM bootloader.
2. Found two independent public implementations of the wake beacon (`tv/switch2-wake-up`, `Minkelxy/xiaoai_switch2_wake_up`) that converged on the same 31-byte payload structure.
3. Built a PlatformIO/NimBLE scaffold, tried to guess the Switch 2's BT MAC offset from its Wi-Fi MAC. Wrong — Nintendo assigns them independently.
4. Pivoted to a BLE scanner. Discovered Joy-Cons broadcast their paired Switch's BT MAC inside their own reconnect advertisements (bytes 17-22 reversed). Captured the Switch 2's BT MAC this way.
5. Discovered byte 16 of the payload is a state flag: `0x00` during pairing mode, `0x81` during paired-reconnect-in-sleep. Only `0x81` wakes the Switch. This byte was present in prior implementations but its state-dependent meaning wasn't documented.
6. Flashed the updated firmware with correct target MAC + `0x81` state flag. **Switch 2 woke.** 🎉
7. Hardened the firmware (NVS config, secrets extraction, button trigger, redacted MACs).
8. Attempted the Pro Controller integration. Migrated to Bluedroid successfully, got HID discovery working, but the SSP auth flow needed more work. Research confirmed the underlying MITM-sniffing-the-HOME-press approach is fundamentally infeasible with ESP32 hardware.

## Credits

- [tv/switch2-wake-up](https://github.com/tv/switch2-wake-up) — first public ESP32 + Flipper wake-beacon implementation
- [Minkelxy/xiaoai_switch2_wake_up](https://github.com/Minkelxy/xiaoai_switch2_wake_up) — independent implementation with annotated payload
- [HelloRayH in ndeadly/MissionControl#199](https://github.com/ndeadly/MissionControl/issues/199) — SDR confirmation of BLE advertising channels 37/38/39 being the wake mechanism
- [darthcloud/BlueRetro](https://github.com/darthcloud/BlueRetro) — Switch 1 Pro Controller HCI capture proving it pages-not-inquires on wake; reference for the dual-mode BT architecture
