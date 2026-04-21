# Pro Controller integration — ground-up plan

> **Status: superseded by BlueRetro fork plan.** This was an early design sketch for implementing Pro Controller classic-BT HID host from scratch on ESP32. The project since pivoted to forking BlueRetro (see [`blueretro-fork-plan.md`](blueretro-fork-plan.md)) because BlueRetro already solves the classic-BT HID host problem with a proven bare-HCI stack, and Bluedroid's classic-BT HID host was abandoned in commit `d49429d` (unreliable scanning/pairing). Kept here for reference — the phase breakdown and coexistence-risk analysis below is still technically sound if anyone attempts a non-BlueRetro path.

Goal: press HOME on a Switch 1 Pro Controller → ESP32 sends wake beacon → Switch 2 wakes.

## Why this is non-trivial

- The Switch 1 Pro Controller uses **classic Bluetooth** (BR/EDR), not BLE
- The wake beacon itself is BLE — we already have that working
- The two radios need to coexist on the same ESP32-WROOM-32 (dual-mode BT)
- There's no off-the-shelf "Pro Controller on ESP32" library — BlueRetro does this but is a large codebase

## Scope

This is the scope for a **ground-up port** of just the Pro Controller pairing sequence, not a full BlueRetro fork. We extract the minimum necessary code from BlueRetro's `sw.c` / `sw.h` and the classic-BT HID host scaffold.

Estimated size: ~800-1000 lines of new code in our project. Estimated time: 1-2 full days of careful work + several hours of testing with the actual Pro Controller.

## Reference code

All from `references/BlueRetro/` in this repo (v25.10-beta):

- `main/bluetooth/hidp/sw.c` — protocol handler (470 lines), the core pairing state machine
- `main/bluetooth/hidp/sw.h` — subcommand / protocol constants
- `main/bluetooth/hidp/hidp.c` — device-name → type mapping (tells us "Pro Controller" string → `BT_SW`)
- `main/adapter/wireless/sw.c` — input report parser (546 lines), decodes HID 0x30 / 0x3F reports
- `main/bluetooth/hci.c` — HCI command layer, much of which we already have equivalents for in NimBLE

## Implementation plan

### Phase 1: classic BT HCI bring-up (~4 hrs)

The ESP32 classic BT stack in ESP-IDF is exposed via the `esp_bt_gap_*` and `esp_bt_hci_*` APIs. NimBLE is BLE-only, so for classic BT we need **Bluedroid alongside NimBLE** OR switch back to Bluedroid entirely.

**Decision needed**: can Bluedroid + NimBLE coexist, or do we switch to Bluedroid for both?

- **Option A**: Bluedroid-only (for both classic BT HID and BLE advertising). Simpler stack, bigger binary (~700 KB), proven to work for dual use.
- **Option B**: Bluedroid for classic BT + NimBLE for BLE. Unusual combination, may not be supported. Need to investigate.

Recommended: **Option A**. Drop NimBLE, rewrite the wake beacon for Bluedroid's `esp_ble_gap_config_adv_data_raw()` + `esp_ble_gap_start_advertising()`. This is actually what the tv/switch2-wake-up reference code uses, so there's a known-good pattern.

### Phase 2: classic BT inquiry + connection (~4 hrs)

In Bluedroid:

1. `esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE)`
2. `esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, ...)` — scan for nearby classic BT devices
3. When we find one with name "Pro Controller" (or check the EIR for the right VID/PID):
   - `esp_bt_gap_cancel_discovery()`
   - Open L2CAP connection to HID control PSM (0x11) and interrupt PSM (0x13)

This requires classic BT pairing. Nintendo Pro Controller uses **JUST_WORKS** pairing (no PIN, no confirmation) which simplifies things.

### Phase 3: Nintendo subcommand handshake (~6 hrs)

Once L2CAP connected, send a sequence of HID output reports to transition the Pro Controller to "sending real input" state. From BlueRetro's `sw.c` `bt_hid_sw_init` → `bt_hid_sw_exec_next_state` sequence:

1. **Subcommand `0x02`** — request device info (reply has controller MAC, firmware version, etc.)
2. **Subcommand `0x10`** — SPI read at `0x603D` to get stick calibration data (10 bytes)
3. **Subcommand `0x04`** — set trigger timing
4. **Subcommand `0x06`** — disable "ship mode" (low power)
5. **Subcommand `0x30`** — set input report mode to 0x30 (standard mode with 6-axis IMU)
6. **Subcommand `0x40`** — enable IMU
7. **Subcommand `0x48`** — enable rumble
8. **Subcommand `0x30`** LED set — turn on player LED

Each subcommand is an output report with specific byte layout (see `sw.h` constants). The Pro Controller replies with an acknowledgment report; we must await each ack before sending the next. This is the bulk of the port effort — not conceptually hard but detail-heavy.

### Phase 4: input report parsing (~2 hrs)

Once subcommand init is complete, the Pro Controller sends 0x30 input reports at ~60 Hz containing:
- 12 bytes button state / sticks at offset 3
- 36 bytes 6-axis IMU at offset 13

From `adapter/wireless/sw.c`:
```c
enum {
    SW_PRO_B = 2, SW_PRO_A, SW_PRO_Y, SW_PRO_X, SW_PRO_L, SW_PRO_R,
    SW_PRO_ZL, SW_PRO_ZR, SW_PRO_MINUS, SW_PRO_PLUS, SW_PRO_LJ,
    SW_PRO_RJ, SW_PRO_HOME, SW_PRO_CAPTURE,
};
```

So HOME is bit 12 of the 16-bit button word at offset 3. Edge detect: on `HOME released` (trigger on release, not press, to avoid firing on long-presses for screenshot etc.), call `send_wake_burst()`.

### Phase 5: dual-mode coordination (~2 hrs)

With Bluedroid running both classic BT (connected to Pro Controller) and BLE (advertising to wake Switch), the ESP32 radio time-slices. Coexistence is handled by the controller but can cause jitter. Need to:

- Temporarily disconnect / pause classic BT scan during the 2-second wake burst
- Resume after burst complete
- Persist classic BT link key in NVS so re-pairing isn't required on reboot

### Phase 6: testing (~4 hrs)

Real-hardware iteration with your Pro Controller. Debugging classic BT pairing with obscure HCI errors is tedious — expect several hours of log analysis.

## Risks

1. **Bluedroid + NimBLE coexistence** is probably not supported. Going Option A (Bluedroid only) means rewriting the wake-beacon code. Not a huge loss but worth knowing.

2. **Pro Controller sometimes requires "paired before" state** — if its paired-host list is full or doesn't include us, it may refuse pairing. May need to put the controller in explicit pair mode (hold the small sync button on top) each time.

3. **Nintendo's 6-axis / rumble init has been modified across firmware versions** — if your Pro Controller has been updated, subcommand semantics may differ slightly from what's in BlueRetro. Mitigation: skip IMU/rumble init entirely (we only need button input for HOME detection).

## Simplification opportunities

- **Skip IMU init**: we only need buttons, not motion. Cuts phase 3 by ~50%.
- **Skip rumble init**: same.
- **Pair once, then persist**: after first successful pair, store link key in NVS. Subsequent boots skip inquiry and go straight to reconnect.
- **Button-only report parsing**: we only care about HOME. Skip sticks, skip IMU, skip everything except byte 3 bit 12.

With these simplifications, estimated total is more like 10-12 hours of focused work.

## Alternative: USB trigger

Much simpler: plug the ESP32 into a USB-C hub with the Switch 1 Pro Controller, and have the ESP32 act as a **classic BT controller** detecting "any BT button press" rather than full Pro Controller protocol parsing. The Pro Controller connects to the ESP32, sends HID reports, and any report with HOME-bit-set triggers the wake.

This is what Phase 1-3 accomplish above, just with reduced scope.

## Decision point

Before we start implementing, confirm:

1. **Bluedroid migration** — OK to drop NimBLE and rewrite wake beacon for Bluedroid?
2. **Scope** — start with the simplified version (button-only, skip IMU/rumble)?
3. **Timeline** — next session (1-2 days focused) or spread across multiple sessions?
