# procon-bridge — Pro Controller HOME → Switch 2 wake

A fork of [BlueRetro](https://github.com/darthcloud/BlueRetro) that turns an ESP32 into a classic-Bluetooth host for the Switch 1 Pro Controller. When the user presses HOME on the controller, the ESP32 fires a BLE wake beacon at the configured Switch 2.

Original BlueRetro README preserved at `README.blueretro.md`.

## Why this exists

Path A from `../docs/status-and-next-steps.md`. Previously blocked by Bluedroid SSP auth issues (commit `d49429d` in the parent repo). BlueRetro has a custom BTDM stack that already pairs successfully with the Switch Pro Controller (see `main/bluetooth/hidp/sw.c`), bypassing those issues.

The empirical scanner test on 2026-04-20 (`../docs/research/procon-trigger-test-results.md`) ruled out Path 0 (BLE scan-and-react). Path A via this fork is now the top viable no-mod path.

## What's added on top of BlueRetro

Minimal, conservative — the BT stack is untouched.

| File | Change |
|---|---|
| `main/wake_bridge.c` (new) | Builds the 31-byte Switch 2 wake adv payload, edge-detects HOME on Pro Controller input reports, fires the burst via BlueRetro's HCI helpers |
| `main/wake_bridge.h` (new) | Public interface |
| `main/bluetooth/host.c` | `bt_host_bridge` early-returns to `wake_bridge_on_input` when `CONFIG_PROCON_WAKE_BRIDGE` is set, bypassing the wired-adapter pipeline. `bt_host_init` calls `wake_bridge_init` |
| `main/bluetooth/hci.c` | Appended public helpers `bt_hci_cmd_le_set_adv_data_raw(payload, len)` and `bt_hci_cmd_le_adv_disable()` to fire arbitrary BLE adv burst with our 31-byte payload |
| `main/bluetooth/hci.h` | Declarations for the above |
| `main/Kconfig.projbuild` | Adds `config PROCON_WAKE_BRIDGE` (default y) |
| `main/CMakeLists.txt` | Adds `wake_bridge.c` to the build |

Everything else is stock BlueRetro. To resync from upstream BlueRetro: re-rsync excluding our changed files, then re-apply the diffs above.

## Status

**Scaffold only — not yet compiled, not yet flashed.** Outstanding work to bring it up:

1. **Configure the target Switch 2 MAC.** Edit `main/wake_bridge.c` and set `WAKE_TARGET_SWITCH_MAC` to your Switch 2's BT MAC bytes (or `#include` from `../esp32/src/secrets.h`). Default all-zeros refuses to fire (safety check in `fire_wake_burst`).

2. **Bypass BlueRetro's `wired_init_task` wait loop.** `main/main.c` calls `start_app_cpu(wired_init_task)`, which spins forever waiting for a system-ID config from BlueRetro's web UI. With `CONFIG_PROCON_WAKE_BRIDGE` set, we want to skip this entirely. Untouched here to keep diffs minimal — needs an `#ifdef` guard around the `start_app_cpu(wired_init_task)` call so `wl_init_task` runs immediately and goes straight to `bt_host_init`.

3. **Set up an ESP-IDF or PlatformIO project file for it.** BlueRetro uses raw `idf.py`. The closest matching no-special-hardware build config is `configs/dbg/`. Adapting to PlatformIO (matching the parent `esp32/platformio.ini`) would let us reuse the existing toolchain.

4. **Verify BTDM coexistence.** ESP32's BTDM controller does classic + LE simultaneously, but we need to confirm an LE adv burst works while a classic BT HID connection is active. May need to drop the classic conn momentarily, fire the burst, reconnect — TBD by experiment.

5. **Test.**
   - Flash to ESP32-WROOM-32. Confirm it shows up as a discoverable BT host.
   - Pair Switch Pro Controller to the ESP32 (sync button on top of controller).
   - Confirm input reports flow (`# wake_bridge: HOME pressed` log line on HOME-press).
   - Verify wake burst fires (`# wake_bridge: firing wake burst, target ...` log line).
   - Verify Switch 2 actually wakes from sleep on burst.

## Architecture

```
┌─────────────────────┐                              ┌─────────────────┐
│  Pro Controller     │  classic-BT HID (BR/EDR)     │   ESP32-WROOM   │
│  (BCM20734)         │  ────────────────────────►   │   procon-bridge │
│                     │  input reports (0x30/0x3F)   │                 │
└─────────────────────┘                              │ ┌─────────────┐ │
                                                     │ │ BlueRetro   │ │
                                                     │ │ BT stack    │ │
                                                     │ │ (sw.c)      │ │
                                                     │ └──────┬──────┘ │
                                                     │   bt_host_      │
                                                     │   bridge()      │
                                                     │        │        │
                                                     │   wake_bridge_  │
                                                     │   on_input()    │
                                                     │        │        │
                                                     │   HOME-press    │
                                                     │   edge detect   │
                                                     │        │        │
                                                     │   bt_hci_cmd_   │
                                                     │   le_set_adv_   │
                                                     │   data_raw()    │
                                                     │        │        │
                                                     └────────┼────────┘
                                                              │ BLE adv (37/38/39)
                                                              ▼
                                                     ┌─────────────────┐
                                                     │  Switch 2       │
                                                     │  wake filter    │
                                                     │  (sleeping)     │
                                                     └─────────────────┘
```

## Credits

- [darthcloud/BlueRetro](https://github.com/darthcloud/BlueRetro) — the entire BT stack and Switch Pro Controller HID host implementation. This fork is a thin application-layer modification on top. License: Apache-2.0 (see `LICENSE`).
- [ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research) — Switch 2 wake adv structure spec (corroborated by our independent decode in `../docs/research/wake-packet.md`)
- [dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) — Pro Controller HID input report format (button bit positions in report 0x30 / 0x3F)
