# Pro Controller wake-trigger test results

Date: 2026-04-20
Hardware:
- Switch 1 Pro Controller, BT MAC `98:B6:E9:3F:D4:FB`, USB PID `0x2009`, OUI `98:B6:E9` (Nintendo)
- ESP32-WROOM-32 (MAC `7c:9e:bd:61:a0:e8`)
- Firmware: `esp32/src/procon_scanner.c` (NimBLE passive scan, 100% duty cycle, all PDU types)
- Captures: `esp32/captures/procon-discovery-20260420-192924.log` (835 KB, 8586 lines)

## TL;DR

**The Switch 1 Pro Controller does NOT emit any BLE advertisement when waking/reconnecting.** Two independent test runs (filtered-mode and full-discovery-mode) captured zero events from the Pro Controller's MAC across multiple HOME-press cycles. Path 0 (scan-and-react) is therefore not viable — the controller almost certainly uses classic-BT paging on the BR/EDR PHY, which an ESP32 cannot sniff.

## Test conditions

- Pro Controller forgotten on Mac (bond record cleared on Mac side; bond record on controller side preserved)
- Mac Bluetooth on, but no other paired hosts in range
- ESP32 scanner running passively at 100% duty cycle (40 ms scan window, 40 ms scan interval, `passive=1`, `filter_duplicates=0`)
- Pro Controller power-cycled, HOME pressed multiple times across both test runs, LED-cycle "searching for host" state observed

## Results

| Run | Mode | Duration | Total BLE adv events captured | Pro Controller hits | Any Nintendo OUI hits |
|---|---|---|---|---|---|
| 1 | Filtered (PROCON_MAC + Nintendo OUIs only) | ~5 min | 0 | 0 | 0 |
| 2 | Full discovery (every BLE adv logged) | ~3 min | 8586 lines, 19 unique MACs | **0** | **0** |

Run 2 confirms the radio is functioning correctly: 19 distinct BLE devices in the surrounding environment were captured (Apple devices, smart-home accessories, etc.). The Pro Controller is simply not present on BLE adv channels under any state we could trigger.

## What this rules out

- **Pro Controller using `ADV_IND` to wake** (the Switch 2 controller pattern documented by ndeadly) — would have appeared in discovery mode
- **Pro Controller using `ADV_DIRECT_IND` to wake** (our earlier provisional parse of HelloRayH's SDR screenshots) — same
- **Pro Controller emitting any BLE adv on any of channels 37/38/39 in this state** — full-spectrum passive scan saw nothing

## What this implies

The Pro Controller is using **classic Bluetooth paging** (BR/EDR baseband, frequency-hopping across 79 channels at 1600 hops/s, addressed to the bonded host's BT_ADDR via the host's BCH-encoded sync word). This is exactly what `docs/status-and-next-steps.md` originally assumed before HelloRayH's MissionControl#199 SDR analysis caused us to reconsider.

This means the SDR captures in MissionControl#199 likely showed **classic-BT page packets**, not BLE adv. The repeating `4a9a254e` access-address-shaped pattern HelloRayH observed (which we provisionally identified as the standard BLE access address `0x8E89BED6` bit-reversed) may instead have been a **classic-BT Channel Access Code derived from the host BT_ADDR** — these share Bluetooth-spec ancestry but appear on different channel sets and PHY modes. SDR can see both; an ESP32 BLE radio can only see BLE adv.

## Implications for paths forward

| Path | Status before this test | Status after this test |
|---|---|---|
| **0**. ESP32 scan-and-react (detect Pro Controller's wake adv) | Top candidate, untested | **Dead** — no BLE adv to detect |
| **A**. Pair Pro Controller TO ESP32 over classic BT | Backup option | Now the **top viable path** for a no-mod solution |
| **B**. Physical mod: ESP32-C3 piggybacked inside controller | Pragmatic backup | Unchanged — still viable, requires soldering |
| **C**. Claim Switch 2's classic BT MAC and page-scan | Rejected (causes pairing chaos) | Same |
| **D**. Wi-Fi / cheap BLE remote / dongle (sidestep Pro Controller) | Future work | Worth reconsidering as the simplest "wake from couch" UX |

## Recommended next move

Switch focus to Path A. Commit `d49429d` already had partial Bluedroid SSP work for pairing the Pro Controller to the ESP32. Finishing that work gives us:

- Real-time HOME-press detection via classic-BT HID input reports
- No physical modification of the controller
- No dependency on a Switch 1
- Trade-off: while paired to the ESP32, the controller can't simultaneously gameplay-pair to the Switch 2 (unless dual-bonding works — needs verification)

Alternatively, **Path D** (a $3 BLE remote button or Wi-Fi HTTP trigger) sidesteps the Pro Controller entirely and ships a working solution in an afternoon. If "wake the Switch 2 from across the room" is the actual goal rather than "use this specific controller," Path D wins on effort vs. value.
