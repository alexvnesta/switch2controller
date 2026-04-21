# Status and next steps

Last updated: 2026-04-21

## What works today

**The ESP32 wakes a Switch 2 from sleep.** Working end-to-end since 2026-04-20:

- Firmware: `esp32/src/switch2_wake.c` (NimBLE-based BLE advertiser, ~250 lines)
- Trigger: BOOT button short press → 2s wake burst → Switch wakes
- Config via `secrets.h` (gitignored) or NVS — change MACs without reflashing
- Documented byte-for-byte payload spec in [`research/wake-packet.md`](research/wake-packet.md)

## Open goal: "press HOME on Switch 1 Pro Controller → Switch 2 wakes"

The original motivation. Investigation status as of 2026-04-21:

### What we now definitively know

- **Switch 1 Pro Controller is 100% classic-BT** (BR/EDR), no BLE adv ever. Triple-confirmed via ESP32 BLE scanner (8586 events captured, 0 from controller), macOS bluetoothd (322 events from controller, all classic-BT subsystems), and direct `< ClassicScan >` flag classification in macOS logs.
- **Wake adv constants are not pre-templated in the controller's flash.** Exhaustive byte-pattern hunt across 524 KB of SPI on two firmware revisions: zero hits.
- **Patch RAM DS1 (factory) is byte-identical across firmware revisions** (mfro 2019 vs ours fw 4.33). Means modifications to DS1 are portable across all controllers we've seen.
- **Patch RAM DS2 (OTA-update slot) holds active firmware on controllers that received OTA updates.** OTA Signature Magic at SPI `0x1FF4` is what selects the active bank.

See [`research/findings.md`](research/findings.md) for the comprehensive evidence summary.

### Path table (definitive)

| Path | Effort | Success | Trade-off | Status |
|---|---|---|---|---|
| **0**: ESP32 scan-and-react (detect Pro Controller's BLE wake adv) | — | — | — | **Empirically dead.** Controller emits no BLE. See [`research/procon-trigger-test-results.md`](research/procon-trigger-test-results.md). |
| **A**: Pair Pro Controller TO ESP32 over classic BT, fire wake on HOME-press | 2-3 days | ~95% | Controller may not simultaneously gameplay-pair to Switch 2 (untested) | **Top viable no-mod path.** Scaffold landed at [`procon-bridge/`](../procon-bridge/) (BlueRetro fork). |
| **B**: Physical mod — XIAO ESP32-C3 inside controller, GPIO-tap HOME | 1 evening + soldering | ~90% | Soldering risk, ~$8 hardware | Pragmatic. See [`plans/pro-controller-piggyback-mod.md`](plans/pro-controller-piggyback-mod.md). |
| **C**: Claim Switch 2's classic BT MAC and page-scan | 1-2 days | high | Causes pairing chaos during gameplay | **Rejected by user.** |
| **D**: Sidestep — Wi-Fi HTTP trigger / BLE remote button / deep-sleep dongle | ~1 evening | ~99% | Doesn't use Pro Controller | Simplest "wake from across room" path. |
| **Tier 1**: Modify Pro Controller firmware via patch RAM | — | ~2-3% | All-or-nothing | **Empirically blocked** (2026-04-21). Patch RAM (DS1 + DS2) returns `0x01` "write protected" on every probe; only color region accepts writes. See [`research/spi-write-test-results.md`](research/spi-write-test-results.md). |

### Recommended next move

Pick one of two based on goal:

1. **"I want a working HOME→wake feature soon"** → finish Path A (BlueRetro fork). 2-3 days. Outstanding work in [`procon-bridge/README.md`](../procon-bridge/README.md): set Switch 2 MAC, bypass `wired_init_task` waitloop, build, flash, test.

2. **"I want the simplest possible 'wake the Switch 2 from across the room' UX"** → Path D (Wi-Fi HTTP trigger or BLE remote button). 1 evening of work, ships immediately. Doesn't use the Pro Controller specifically.

Tier 1 (firmware mod) was investigated and ruled out — the chip's patch RAM regions are write-protected on current firmware. See [`research/spi-write-test-results.md`](research/spi-write-test-results.md). The Tier 1 disassembly plan in [`plans/tier1-disassembly-plan.md`](plans/tier1-disassembly-plan.md) is preserved for documentation but no longer recommended.

## Future work ideas (not on critical path)

- **Wi-Fi + HTTP wake trigger**: ESP32 joins local Wi-Fi, `POST /wake` → fires beacon. Clean Home Assistant integration. ~2 hours.
- **Cheap BLE controller as wake remote**: pair an 8BitDo Micro or similar, any button → wake. ~4 hours.
- **Battery-powered deep-sleep dongle**: ESP32-C3 + LiPo + button, keychain form factor. ~1 evening.
- **Configurable MACs over BLE**: temporary BLE GATT service for runtime MAC config, no reflash for new Switch.
- **Capture ground-truth wake-adv bytes from a real Joy-Con 2 / Pro Controller 2** when the user gets one — cross-validate our spec against ndeadly's.

## How this all came together

Two long sessions of work documented in chronological detail:

- [`sessions/session-2026-04-20.md`](sessions/session-2026-04-20.md) — original wake firmware development
- [`sessions/session-2026-04-20-procon.md`](sessions/session-2026-04-20-procon.md) — Pro Controller investigation, scanner test killing Path 0, BlueRetro fork scaffold, SPI dump

Comprehensive findings summary: [`research/findings.md`](research/findings.md).

Tier 1 deep dive (disassembly + custom firmware path): [`plans/tier1-disassembly-plan.md`](plans/tier1-disassembly-plan.md).
