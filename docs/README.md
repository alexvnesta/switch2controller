# Docs index

Navigation guide for everything under `docs/`. If you're new to the project and want to continue the research, read these in roughly this order:

1. **[`status-and-next-steps.md`](status-and-next-steps.md)** — the top-level path table and recommended next moves. Start here.
2. **[`research/findings.md`](research/findings.md)** — single canonical "what we know," with evidence citations for every claim.
3. **[`research/wake-packet.md`](research/wake-packet.md)** — byte-by-byte breakdown of the 31-byte Switch 2 wake beacon.

Everything else is topic-specific. Use the index below to jump.

> **Before trusting any single doc**: check for a `⚠️ SUPERSEDED` or `⚠️ BLOCKED` header at the top. Those docs preserve a hypothesis that later evidence falsified — the headers explain what replaced them. See also the AI-assistance disclaimer in the root [`../README.md`](../README.md).

---

## `research/` — what we learned

### Canonical references

| Doc | One-line summary |
|---|---|
| [`findings.md`](research/findings.md) | Comprehensive evidence summary. The one-stop synthesis. |
| [`wake-packet.md`](research/wake-packet.md) | Switch 2 wake adv: all 31 bytes explained, cross-validated against ndeadly's spec. |
| [`scanner-guide.md`](research/scanner-guide.md) | How to capture your Switch 2's BT MAC + a Joy-Con's MAC with the ESP32 scanner firmware. |
| [`references-inventory.md`](research/references-inventory.md) | What's in `references/` (cloned external projects), what's useful, what was misidentified. |

### Switch 1 Pro Controller investigation

| Doc | One-line summary |
|---|---|
| [`switch1-pro-controller-wake.md`](research/switch1-pro-controller-wake.md) | Why the Switch 1 Pro Controller can't natively wake a Switch 2 — four compounding blockers. |
| [`procon-trigger-test-results.md`](research/procon-trigger-test-results.md) | Empirical test: 0 BLE events from the controller across 8586 captured BLE adv events. Kills Path 0. |
| [`procon-trigger-test.md`](research/procon-trigger-test.md) | ⚠️ HISTORICAL — test protocol design; run, hypothesis falsified by the results doc above. |
| [`procon-wake-decode.md`](research/procon-wake-decode.md) | ⚠️ SUPERSEDED — provisional BLE decode of HelloRayH's SDR screenshots. Interpretation replaced by "it's classic-BT paging, not BLE." |

### Firmware / SPI analysis

| Doc | One-line summary |
|---|---|
| [`spi-dump-diff-results.md`](research/spi-dump-diff-results.md) | Our fw 4.33 dump vs mfro's 2019 dump: **DS1 is byte-identical** across 6+ years; DS2 is the OTA-updated slot. |
| [`spi-write-test-results.md`](research/spi-write-test-results.md) | **Tier 1 hard blocker**: patch RAM SPI writes return `0x01` "write protected" on fw 4.33. Color region still writable. |
| [`firmware-hunt-results.md`](research/firmware-hunt-results.md) | Byte-pattern hunt across the full 524 KB SPI dump. Wake adv constants not present in flash — adv built at runtime by ROM code. |
| [`firmware-analysis.md`](research/firmware-analysis.md) | GuliKit firmware diff dead-end (BT chip firmware is AES-CCM-encrypted at rest) + scope correction for Pro Controller patch RAM (unencrypted). |

---

## `plans/` — what we thought we'd do

| Doc | Status | One-line summary |
|---|---|---|
| [`blueretro-fork-plan.md`](plans/blueretro-fork-plan.md) | Active, scaffolded | Path A: fork BlueRetro, pair Pro Controller to ESP32 over classic BT, fire wake on HOME press. Scaffold at [`../procon-bridge/`](../procon-bridge/). |
| [`pro-controller-piggyback-mod.md`](plans/pro-controller-piggyback-mod.md) | Viable, not built | Path B: solder a XIAO ESP32-C3 inside the Pro Controller, GPIO-tap the HOME pad. Preserves gameplay pairing. |
| [`tier1-disassembly-plan.md`](plans/tier1-disassembly-plan.md) | ⚠️ BLOCKED | Patch RAM disassembly + "Hello BLE" custom firmware. Blocked by SPI write protection (see `spi-write-test-results.md`). |
| [`pro-controller-integration-plan.md`](plans/pro-controller-integration-plan.md) | Superseded | Earlier ground-up-on-Bluedroid design for Path A; pivoted to forking BlueRetro instead (Bluedroid's classic-BT HID host was unreliable). |
| [`ghidra-setup.md`](plans/ghidra-setup.md) | Reference | Setting up Ghidra + GhidraMCP for collaborative disassembly of the Pro Controller patch RAM. |

---

## `sessions/` — chronological research logs

Narrative accounts of what was done, when, and why. These are rough and redundant with the research docs above — useful if you're reconstructing the order of discoveries.

| Doc | One-line summary |
|---|---|
| [`session-2026-04-20.md`](sessions/session-2026-04-20.md) | Original wake-beacon development session — getting the Switch 2 wake firmware working end-to-end. |
| [`session-2026-04-20-procon.md`](sessions/session-2026-04-20-procon.md) | Pro Controller investigation: triple-confirm classic-BT, scaffold BlueRetro fork, dump SPI flash. |
| [`research-notes.md`](sessions/research-notes.md) | Earlier research log, predating the session-format docs. |

---

## Quick map: "where do I look for…"

- **Switch 2 wake byte structure** → [`research/wake-packet.md`](research/wake-packet.md)
- **How to find my Switch 2's BT MAC** → [`research/scanner-guide.md`](research/scanner-guide.md)
- **Why HOME-on-Pro-Controller doesn't Just Work** → [`research/switch1-pro-controller-wake.md`](research/switch1-pro-controller-wake.md)
- **What's the recommended next step for this project** → [`status-and-next-steps.md`](status-and-next-steps.md)
- **Can I modify the Pro Controller firmware to emit BLE?** → [`research/spi-write-test-results.md`](research/spi-write-test-results.md) (short answer: no, on fw 4.33)
- **What prior art exists?** → [`research/references-inventory.md`](research/references-inventory.md)
