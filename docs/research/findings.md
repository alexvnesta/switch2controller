# Findings — comprehensive summary

This is the **single canonical "what we know"** document. Other research docs deep-dive specific topics; this one summarizes everything in one place.

Last updated: 2026-04-21 after full SPI dump + diff vs mfro's reference.

## Executive summary

The project's original goal — "press HOME on a Switch 1 Pro Controller, Switch 2 wakes from sleep" — turns out to require either (a) an ESP32 acting as a classic-BT bridge between the Pro Controller and the Switch 2, or (b) a non-trivial reverse-engineering effort to modify the Pro Controller's BCM20734 patch RAM to emit BLE wake adv directly.

We've cleanly characterized the protocol layer ("the Switch 2 wake adv is a 31-byte BLE undirected adv with specific Nintendo VID/PID/state-flag bytes") and demonstrated it working from an ESP32. We've definitively ruled out the simplest cross-controller paths (the Switch 1 Pro Controller emits zero BLE — purely classic-BT page) via three independent observations. The remaining options are scoped and ranked.

## Established facts (with evidence)

### Switch 2 wake protocol

| Fact | Evidence |
|---|---|
| Switch 2 listens on BLE adv channels 37/38/39 in sleep | Working wake firmware `esp32/src/switch2_wake.c` — confirmed |
| Wake adv is 31-byte `ADV_IND` with specific manufacturer data structure | [`wake-packet.md`](wake-packet.md), independently confirmed by [ndeadly's spec](https://github.com/ndeadly/switch2_controller_research/blob/master/bluetooth_interface.md) |
| Byte 16 of mfr data = `0x81` is the wake-trigger flag (vs `0x00` for normal adv) | Empirically discovered, corroborated by ndeadly's docs |
| Accepted PIDs include `0x2066` (Joy-Con 2 L), `0x2067` (Joy-Con 2 R), `0x2069` (Pro Controller 2) | ndeadly's spec |
| Source MAC must be **bonded** to the Switch 2; payload alone isn't enough | Tested empirically; unbonded MAC + correct payload doesn't wake |
| No cryptographic protection on wake — bonding is the only auth | Plain BLE adv replay works |

### Switch 1 Pro Controller's behavior

| Fact | Evidence |
|---|---|
| Controller is BCM20734 (dual-mode capable: BR/EDR + BLE 4.1) | dekuNukem datasheets, chip markings |
| Stock firmware uses **only classic BT** (BR/EDR), no BLE adv ever | **Triple-confirmed**: ESP32 BLE scanner saw 0 events / 8586 captured; macOS bluetoothd saw 0 BLE Scanner events for it / 322 captured; macOS classified all events as `< ClassicScan >` |
| Wake/reconnect uses classic-BT page (1 Mbps GFSK, addressed to host BD_ADDR) | macOS log shows `Stack.HCI: Allocating ACL connection` for the controller — ACL = BR/EDR data channel |
| HelloRayH's MissionControl#199 SDR captures (2021) were classic-BT page packets, not BLE adv | Implied by triple-confirmed BLE absence; the `4a9a254e` access-address-shaped pattern was likely a classic-BT Channel Access Code derived from host MAC |

### BCM20734 firmware structure

| Fact | Evidence |
|---|---|
| Patch RAM lives in SPI flash at `0x10000` (DS1) and `0x28000` (DS2) | dekuNukem, CTCaer's jc_toolkit, our own SPI dump |
| Patch RAM is plain unencrypted ARM Thumb code | mfro's `tmp0`/`tmp0.hop`, our own SPI dump confirms |
| BCM ROM (in-chip mask ROM) is encrypted; decrypted by hardware bootloader at boot | Industry-standard for BCM dual-mode chips; not directly verified but consistent with all observations |
| **DS1 is byte-identical between mfro's 2019 dump and our 2026 fw 4.33 dump** | [`spi-dump-diff-results.md`](spi-dump-diff-results.md) — 0 bytes differ across 96 KB |
| **OTA Signature Magic at SPI `0x1FF4`** points to active patch RAM bank | Empirical: our dump has `AA 55 F0 0F | 8 bytes | 00 80 02 00` (= LE addr `0x00028000` = DS2 start); mfro's is `0xFF` (no OTA) |
| Wake adv constants are **NOT pre-templated anywhere in flash** | [`firmware-hunt-results.md`](firmware-hunt-results.md) — exhaustive byte-pattern search returned 0 hits in both DS1 and DS2 |
| BLE infrastructure exists in firmware build (single string `BLE ACL buffer allocation fail` in patch RAM) | But adv emission code paths likely live in encrypted ROM, called from patch RAM |

### Hardware capability constraints

| Capability | Available? |
|---|---|
| ESP32 (NimBLE or Bluedroid) emit BLE adv on 37/38/39 with arbitrary 31-byte payload | Yes — used today in working firmware |
| ESP32 act as classic-BT HID host (pair Pro Controller, receive input reports) | **Yes via BlueRetro's bare-HCI BTDM stack** (proven). Bluedroid struggles — abandoned in commit `d49429d` |
| ESP32 scan-and-react to Pro Controller's wake-time BLE adv | **No** — controller emits no BLE adv |
| ESP32 sniff classic-BT page packets addressed to other devices | **No** — classic-BT baseband filters at silicon level |

## Ruled-out paths

### Path 0: ESP32 scan-and-react

Premise: detect Pro Controller's wake-time BLE adv, react with our own Switch-2-shaped adv.
**Killed** by the empirical scanner test ([`procon-trigger-test-results.md`](procon-trigger-test-results.md)). Pro Controller emits no BLE during wake/reconnect.

### Path C: Claim Switch 2's classic BT MAC and page-scan

Premise: ESP32 impersonates the Switch 2's BD_ADDR and listens for the controller's page packets.
**Rejected** by the user — would cause pairing chaos during normal Switch 2 gameplay (controller would page our ESP32 instead of the Switch 2).

### MissionControl#199-style "BLE wake packet from Pro Controller"

Premise: HelloRayH's SDR captures showed the Pro Controller waking via BLE adv channels.
**Re-interpreted** based on later evidence. The SDR captures must have been classic-BT page packets, since both BLE access addresses and BR/EDR sync words share Bluetooth-spec ancestry but appear on different PHYs. SDR can capture both; macOS's BT stack (which can see both) saw only classic-BT.

## Viable paths — ranked

| Path | Effort | Success probability | Trade-off |
|---|---|---|---|
| **A**: BlueRetro fork — pair Pro Controller TO ESP32, fire wake on HOME | 2-3 days | ~95% | Controller can't simultaneously gameplay-pair to Switch 2 (slot conflict, untested) |
| **B**: Physical mod — XIAO ESP32-C3 inside controller, GPIO-tap HOME | 1 evening + soldering | ~90% | Requires soldering skill, ~$8 hardware |
| **D**: Sidestep entirely — Wi-Fi HTTP trigger, BLE remote button, deep-sleep dongle | ~1 evening | ~99% | Doesn't use the Pro Controller |
| **Tier 1 Stage 1**: Disassembly scout (ROM addresses, BLE liveness, OTA magic scheme) | ~30 hours | ~60% useful | Pure information gathering, no risk |
| **Tier 1 Stage 2**: "Hello BLE" custom patch RAM (prove BLE adv emits at all) | ~10 hours after Stage 1 | ~50% conditional on Stage 1 | Real brick risk if OTA magic is cryptographic |
| **Tier 1 Stage 3**: Ship working wake-adv-emitting Pro Controller firmware | 6-12 months total | ~15% combined | All-or-nothing |

## Live unknowns we could still resolve

1. **Is the OTA Signature Magic cryptographic or just a checksum?** Critical for Tier 1. Resolvable by disassembling the ~1 KB of code in patch RAM that generates it.

2. **Are BLE stack code paths actually live in fw 4.33 patch RAM?** Or did Nintendo's compile config dead-strip them? The single `BLE ACL buffer allocation fail` string suggests live; xref tracing would confirm. Resolvable in 4-8 hours of disassembly.

3. **Does writing modified patch RAM via SPI succeed?** The cheapest possible test (Stage 1c — modify a debug string, see if chip still boots). Doable from our existing WebHID page once we port shinyquagsire23's `spi_write` to JS.

4. **Does the Pro Controller's BCM ROM contain a usable BLE adv code path that patch RAM could call into?** Implied yes by "BLE ACL buffer allocation fail" + dual-mode chip nature, but unverified. Resolvable by finding ROM call addresses in patch RAM and matching against known Broadcom symbol patterns (InternalBlue's BCM4339/BCM43430 work).

5. **Can a Switch 1 Pro Controller simultaneously bond to the ESP32 (for wake triggering) AND the Switch 2 (for gameplay)?** Critical for Path A's user experience. The SPI bonding record at `0x2000` in mfro's dump shows a single 32-byte host entry immediately followed by `0xFF` erased flash — no second-slot structure is populated, and unlike the Switch 2 Joy-Con's `count * 0x28` table (`ndeadly-switch2-research/memory_layout.md:106`), there's no item-count byte suggesting an array. Stock Switch 1 Pro Controller firmware appears to be single-host. An earlier version of this doc framed this as "the two-bonding-slot NVRAM" — that was a confusion with Switch 2 Joy-Con layout and not supported by the Switch 1 SPI dump. Resolvable definitively by Path A implementation + experiment.

## See also

- [`wake-packet.md`](wake-packet.md) — byte-level decode of Switch 2 wake adv
- [`switch1-pro-controller-wake.md`](switch1-pro-controller-wake.md) — overview of why this is hard
- [`procon-trigger-test-results.md`](procon-trigger-test-results.md) — empirical test killing Path 0
- [`firmware-hunt-results.md`](firmware-hunt-results.md) — byte-pattern hunt across SPI flash
- [`spi-dump-diff-results.md`](spi-dump-diff-results.md) — DS1-universal + DS2-OTA findings
- [`firmware-analysis.md`](firmware-analysis.md) — firmware encryption scope (corrected)
- [`references-inventory.md`](references-inventory.md) — what's in `references/`
- [`../plans/tier1-disassembly-plan.md`](../plans/tier1-disassembly-plan.md) — concrete plan for Tier 1 Stages 1-2
- [`../sessions/session-2026-04-20-procon.md`](../sessions/session-2026-04-20-procon.md) — chronological log
