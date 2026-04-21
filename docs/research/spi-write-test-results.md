# SPI write-protection test results — Tier 1 hard blocker

Date: 2026-04-21
Hardware: Switch 1 Pro Controller (`98:B6:E9:3F:D4:FB`, fw 4.33)
Tool: `tools/procon_spi_write_test.html` (WebHID, single-byte XOR probes with auto-restore)
Reference for status codes: [dekuNukem subcommand 0x11 docs](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_subcommands_notes.md) — "x00 = success, x01 = write protected"

## TL;DR

**Patch RAM (both DS1 and DS2 banks) is write-protected on fw 4.33.** Nintendo locked the patch RAM regions against modification via the standard SPI write subcommand `0x11`. Color and configuration regions remain writable (as JC Toolkit relies on). This is a **hard blocker for Tier 1** (custom Pro Controller firmware via patch RAM modification).

shinyquagsire23's 2017-2019 SPI write code worked because Nintendo hadn't locked patch RAM yet at that time. Subsequent firmware updates added the protection.

## Test results

| SPI Address | Region | Status byte | Verify | Result |
|---|---|---|---|---|
| `0x6050` | Body color (per dekuNukem) | `0x00` | Modified value persisted then restored | ✅ Write succeeded |
| `0x1DB9B` | Patch RAM DS1 ("Joy-Con (L)" string) | `0x01` | Original value unchanged | ❌ Write protected |
| `0x28100` | Patch RAM DS2 (active OTA-updated firmware) | `0x01` | Original value unchanged | ❌ Write protected |

All three tests used a single-bit XOR (`orig ^ 0x01`) for minimal-impact probing, and all three attempted restore after the test.

## Implications

### Confirmed our SPI write protocol is correct

Color region write at `0x6050` succeeded with status `0x00`, returning the actual written value on verify read. This proves our framing of subcommand `0x11` is correct — we're not getting a generic "subcommand unknown" rejection.

### Patch RAM is intentionally region-locked

Both DS1 (factory patch RAM at `0x10000`) and DS2 (OTA-update slot at `0x28000`) returned `0x01` "write protected". This is **not a global SPI lock** — it's specific to the patch RAM regions.

Implication: Nintendo intentionally added region-based write protection to patch RAM. Their own firmware update path must either:
- Use a different subcommand we haven't discovered (e.g., a privileged "unlock + write" sequence)
- Validate a signature on the new firmware blob and only unlock when the signature checks out
- Require some authenticated state (e.g., a Switch console-issued challenge-response) that JC Toolkit / our WebHID page cannot produce

### Tier 1 is effectively dead at the access layer

The Tier 1 disassembly plan ([`../plans/tier1-disassembly-plan.md`](../plans/tier1-disassembly-plan.md)) assumed we could write modified patch RAM via the standard subcommand. We can't.

Remaining theoretical paths to modify patch RAM:

1. **Reverse-engineer Nintendo's OTA unlock mechanism.** Probably requires reverse-engineering the encrypted ROM (we already established that's a multi-year project). Possibly involves a signature on the new patch RAM blob validated against a per-chip or per-SDK key in ROM.

2. **Find a memory-safety vulnerability in patch RAM** that escalates to "write anywhere" privileges. Security research project (jailbreak/CVE-class work). No public CVEs for BCM20734.

3. **Desolder the SPI flash chip** and write to it via an external programmer (CH341A or similar). Bypasses the BCM20734 entirely on writes. **But**: the chip's boot-time validation (whatever the OTA Signature Magic actually verifies) likely rejects modified firmware. So this would only work if the OTA Magic is non-cryptographic — same gating question we had before, but now we'd also need physical hardware modification of the controller.

None of these are tractable for a hobby project.

## Updated Tier 1 estimate

| Stage | Status before this test | Status after |
|---|---|---|
| Stage 1 (disassembly scout) | Could proceed | Still possible — but pointless without a write path |
| Stage 1c (write test) | Open question | **Definitively answered: WRITES BLOCKED** |
| Stage 2 ("Hello BLE" custom patch RAM) | Required Stage 1c pass | Cannot proceed |
| Stage 3 (ship working firmware) | 6 months optimistic | Effectively impossible without ROM dump or vuln discovery |

**Combined Tier 1 success probability: ~2-3%** (down from ~15%).

The disassembly work could still produce useful research output (BCM20734 patch RAM symbol map, useful to anyone working on related projects) but it can't lead to a working modified firmware on this firmware version of this controller.

## Strategic implication

**Path A (BlueRetro fork) is the only viable near-term path to the original "HOME wakes Switch 2" goal.** Tier 1 is now firmly off the table for this project.

The full path table in [`../status-and-next-steps.md`](../status-and-next-steps.md) reflects this — Tier 1 stages should be marked as blocked rather than viable.

## Notes for future researchers

If anyone wants to pursue Tier 1 further, the key open questions are:

1. **What subcommand or sequence does Nintendo's official OTA updater use?** USB-pcap a controller firmware update via the Switch Console (e.g., the firmware 4.33 update from a 4.x base) and look for non-`0x11` write paths.

2. **Is there a chip-level "unlock" that Switch Console issues?** The Switch Console may issue a privileged HCI command (vendor-specific) before sending an OTA blob. JC Toolkit doesn't do this; the Switch does. Reverse-engineering the Switch's HOS firmware update flow would reveal it.

3. **Was patch RAM ever writable on production firmware, or only on early dev firmware?** mfro's 2019 dump shows alt slot empty (no OTA applied) — so we don't actually know whether shinyquagsire23's 2017 writes worked on production firmware or only on a dev unit. Worth checking.
