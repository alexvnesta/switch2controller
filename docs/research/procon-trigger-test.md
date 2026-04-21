# Pro Controller wake-trigger test matrix

Goal: empirically determine the exact controller state(s) under which the Switch 1 Pro Controller emits a BLE wake advertisement, so we know what's required for an ESP32 to detect it.

## Why this matters

The "scan-and-react" path (ESP32 detects Pro Controller wake adv → fires Switch-2-shaped beacon) only works if pressing HOME on the Pro Controller reliably causes a BLE adv to fire. We need to know:

1. Is a bonded host required, or does any HOME-press fire the adv?
2. Does the bonded host need to be present/absent/asleep?
3. Does pressing HOME during active gameplay also fire it?
4. How long is the adv burst window, and how many packets per press?

Knowing this changes the UX:
- "Adv fires on every HOME press" → no Switch 1 dependency at all, the ESP32 is a pure RF translator
- "Adv fires only when bonded host is unreachable" → Pro Controller must be paired to a Switch 1 once, then the Switch 1 stays absent forever
- "Adv only fires from cold-boot reconnect attempt" → press HOME twice (once to wake controller, once to trigger wake) — usable but clunky

## Hardware required

- 1× ESP32 dev board with `procon_scanner` firmware (see [`../esp32/src/procon_scanner.c.disabled`](../esp32/src/procon_scanner.c.disabled))
- 1× Switch 1 Pro Controller
- 1× Switch 1 console (used briefly for bonding setup, then power off / move out of range)
- 1× Switch 2 console (for the gameplay-state tests)
- Serial monitor at 115200 baud

## Test setup

1. Flash ESP32 with `procon_scanner` firmware. Confirm it boots (3 LED blinks).
2. Note the Pro Controller's BT MAC. Easiest: pair it to a Mac/PC over classic BT once and read it from System Preferences → Bluetooth. Write it down.
3. Open serial monitor. The scanner logs every BLE adv. ★ marks any packet from the Pro Controller's MAC.

## Test matrix

For each row: set the controller into the State, perform the Action, observe whether the scanner logs ★ events from the Pro Controller's MAC and how many.

| # | State | Action | Expected outcome | Why we're testing |
|---|---|---|---|---|
| A | Factory-reset (no bonds) | Hold sync button until LEDs flash, then press HOME | Probably no wake adv; controller is in classic-BT pairing mode (`ADV_IND` style). Scanner may see classic-BT-mode adv though. | Baseline: does the controller emit anything BLE-side without a bonded host? |
| B | Bonded to Switch 1, Switch 1 powered OFF | Wake controller (HOME or any button) | **Hypothesis: wake adv fires.** Controller assumes host is asleep, tries to wake it. | The cleanest "scan-and-react" enabler — Switch 1 is set-and-forget. |
| C | Bonded to Switch 1, Switch 1 awake and in range | Wake controller (HOME) | Controller should connect via classic-BT directly without BLE wake adv. **Hypothesis: no wake adv.** | Confirms wake adv is conditional on host being unreachable. |
| D | Bonded to Switch 2, Switch 2 awake, controller currently disconnected | Press HOME | Same as C but for Switch 2. **Hypothesis: classic-BT page, no wake adv.** | Tests whether wake adv is Switch-1-specific or any-host. |
| E | Bonded to Switch 2, Switch 2 asleep | Press HOME | **This is the moment that wakes a real Switch 2 with a real Joy-Con.** Pro Controller is *also* bonded to Switch 2 here — does it emit the same Joy-Con-style wake beacon, or its own classic-BT page? | The "does Pro Controller already know how to wake Switch 2 and we just don't have the right pairing flow?" test. |
| F | Bonded to BOTH Switch 1 (off) and Switch 2 (active gameplay), connected to Switch 2 | Press HOME during gameplay | Should send classic-BT HID HOME-press to Switch 2. **Hypothesis: no BLE wake adv.** | Confirms wake adv doesn't fire mid-game (which would be useless / collide with HID). |
| G | Bonded to Switch 1 (off), no other bonds, controller already powered ON for >30s idle | Press HOME again | Tests whether the wake adv burst is one-shot (only on cold reconnect) or repeats per-press | Affects UX — do you press HOME once or twice to wake? |
| H | Bonded to Switch 1 (off) | Press HOME, time how long ★ events keep arriving | Measure burst duration in ms and packet count | Sets the scanner's match-window timing |

## What to record per test

For each row, capture:

- Number of ★ events seen during a 5-second window after the Action
- For any ★ events: the `evt=` type (`ADV_IND` / `ADV_DIRECT_IND` / etc.)
- The raw byte dump of the first ★ packet
- Notes on controller LED behavior (slow flash = pairing mode, fast flash = reconnect attempt, solid = connected)

## Interpretation guide

| Outcome | What it means | What to do next |
|---|---|---|
| Test B fires ★, Test C does not | Bonded-but-unreachable triggers wake adv. Path 1' is fully viable. | Build the reactive firmware: ESP32 matches on Pro Controller MAC + `ADV_DIRECT_IND` → fires Switch-2-shaped beacon. |
| Test E fires ★ with Joy-Con-style payload (PID `0x2066`/`0x2067`) | Pro Controller already emits a Switch-2-compatible wake — we may not need the ESP32 translator at all, just need to figure out why pairing/wake isn't working today. | Investigate why this isn't waking the Switch 2 directly; possibly a bonding-state / LTK issue. |
| Test E fires ★ with `ADV_DIRECT_IND` (Switch-1-style) addressed at the Switch 2 | Pro Controller emits a directed wake at the Switch 2, but Switch 2 ignores it (only accepts Joy-Con-style). Confirms Switch 2 ≠ Switch 1 wake protocol. | ESP32 translator is required. Build reactive firmware. |
| Test E fires nothing | Pro Controller doesn't wake Switch 2 at all today. | Same: ESP32 translator required, with the Switch-1-bonded trigger trick. |
| Test G shows ★ only once after cold-boot, not on subsequent HOME presses | Wake adv only fires from full power-cycle. Bad UX. | Look for a state-reset trigger; possibly need to add a small power-cycle circuit, or live with the limitation. |
| Test A fires ★ | Controller emits BLE adv even unbonded. | Even simpler trigger — could detect any Pro Controller in pairing mode. (Probably won't happen; classic-BT pairing is BR/EDR not BLE.) |

## Time budget

~30 minutes once setup is done. Tests B-G are each <1 minute.

The slow part is bonding/unbonding the controller between rows. To minimize this, do tests in matrix order — A, B, G, C, D, E, F — so each row only needs to add or change one thing from the previous.

## After the test

Record results in `docs/procon-trigger-test-results.md` with:

- Date of test
- Pro Controller serial / firmware version (from `System Settings → Controllers → Update Controllers` on Switch)
- Switch 1 firmware version
- Switch 2 firmware version
- Per-row results in a results table

The findings drive whether to build the scan-and-react firmware (Path 1' from `status-and-next-steps.md`) or fall back to one of the bonded-pair / physical-mod paths.
