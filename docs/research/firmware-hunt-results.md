# Hunting the wake packet inside the Pro Controller firmware

Goal: locate where the Switch 1 Pro Controller's BCM20734 firmware constructs the BLE wake advertisement, to determine whether the wake adv shape can be modified from the controller side.

Method: byte-pattern search across mfro's publicly-available Pro Controller SPI dump (`pro_spi.bin`, 524 KB) and its extracted patch RAM region (`tmp0`, ~84 KB) for the static constants we expect to see in a wake-adv template.

Conclusion (TL;DR): **the wake adv is dynamically constructed at runtime, not stored as a template.** Constants and host MAC are not present as a contiguous blob anywhere in the SPI flash. The construction code itself lives in the encrypted BCM ROM, not the unencrypted patch RAM.

## What we searched for

The Switch 2 wake adv mfr data (per [`wake-packet.md`](wake-packet.md), corroborated by [ndeadly's spec](https://github.com/ndeadly/switch2_controller_research/blob/master/bluetooth_interface.md)) has a very recognizable byte signature:

```
02 01 06  1B FF  53 05  01 00 03  7E 05  XX YY  00 01 81  <host MAC reversed>  0F 00 00 ...
```

The Switch 1 Pro Controller version (paired to a Switch 1, which is what mfro's dump represents) would have:
- PID `0x2009` (Switch 1 Pro Controller) instead of Switch-2 PIDs
- The Switch 1's host MAC (`95 22 99 05 00 1A` per the bonding record at SPI `0x2000`) instead of a Switch 2's

If the wake adv were stored as a static template in flash, we'd find these distinctive byte sequences. We searched for every reasonable variant.

## Search results

| Pattern | Meaning | `pro_spi.bin` (full SPI) | `tmp0` (patch RAM) |
|---|---|---|---|
| `02 01 06 1B FF 53 05` | Full adv + mfr data prefix | **0 hits** | 0 hits |
| `1B FF 53 05` | Mfr data start | **0 hits** | 0 hits (1 spurious `1B FF` at instruction-encoding offset) |
| `53 05 01 00 03 7E 05` | Nintendo company + header + VID (the unique 7-byte signature) | **0 hits** | 0 hits |
| `53 05 01 00` | Nintendo company + header start | **0 hits** | 0 hits |
| `03 7E 05` | Header + VID | **0 hits** | 0 hits |
| `7E 05 09 20` | VID + Switch 1 Pro Controller PID | **0 hits** | 0 hits |
| `00 01 81` | State flag triplet (bytes 14-16 of mfr data) | 0 in patch RAM | 3 spurious matches in instruction stream |
| `95 22 99 05 00 1A` | Bonded host MAC (from SPI `0x2000`) | **1 hit, exclusively at `0x2000`** (the bonding record itself) | **0 hits** |

`grep -boE` against `xxd -p` output, then divided by 2 for byte offsets. The full search session is reproducible with the commands captured at the bottom of this doc.

## Interpretation

Combining the negative search results with what we *did* find:

1. **The bonded host MAC exists in flash exactly once**, at the bonding-record slot `0x2000` documented by dekuNukem and CTCaer's jc_toolkit. It's not duplicated into a pre-built wake adv template anywhere.

2. **None of the wake-adv constants (`0x0553`, `0x057E`, `0x2009`, mfr data prefix) exist as adjacent bytes anywhere in 524 KB of flash.** This rules out: a static wake adv template in patch RAM, a template in the bonding record, a template in the OTA staging banks at `0x10000`/`0x28000`, or a template stashed in the unused regions at `0x6000`/`0xE000`/`0x18000`.

3. **The patch RAM strings reveal what *is* in patch RAM**: HID transport (`mybthidtransport.cpp`), UART transport (`uarttransport.cpp`), LED driver (`dimmerleddriver.cpp`), battery monitor fixes, and HCI sleep-mode command handlers (`ukyo_hci_SetSleepModeParam`, `ukyo_hci_SleepModeCmd`, `ukyo_hci_TransitionToMpafMode`). The path strings include Nintendo's internal codenames `nintendo/raizoEP2/../ukyo/`. These are application-layer + power-management patches — **not** the LL/HCI core stack that constructs adv PDUs.

4. **`ukyo_hci_*` handles entering sleep, not waking the host.** Different function. Going to sleep is a controller→chip command sequence; waking the host is a chip→radio adv emission with the host MAC embedded.

The most parsimonious model:

- The wake adv mfr data buffer is **assembled in RAM at the moment of emission** by code in the encrypted BCM ROM
- That code reads the bonded host MAC from SPI `0x2000`, reads hard-coded constants encoded as Thumb immediates or from data tables in ROM, and assembles the 26-byte mfr data buffer
- It then calls the standard Bluetooth HCI command `LE Set Advertising Data` (opcode `0x2008`) followed by `LE Set Advertising Enable` (`0x200A`) to start advertising
- The patch RAM extends/overrides ROM behavior in narrow application-layer ways but does not replace this core LL/HCI flow

## What this means for "modify wake adv from controller side"

This is a **softer falsification** of the optimism we held briefly when we discovered the patch RAM is unencrypted. It corrects the prior overcorrection.

| Claim | Status |
|---|---|
| "BCM firmware is fully AES-locked, custom-firmware path is impossible" | Wrong — patch RAM is open |
| "The patch RAM is the realistic attack surface for changing the wake adv" | **Also wrong** — the wake adv code path isn't in the patch RAM, it's in the ROM that the patch RAM hooks into |
| "Modifying the wake adv from the controller side requires hooking ROM functions via patch RAM, with no usable wake-adv-construction code visible in the open SPI region" | **Current accurate position** |

To change the wake adv shape from the controller side, you'd need to:

1. Identify the ROM address of the function that constructs the wake adv mfr data buffer (or the function that calls `le_set_adv_data` for the wake case specifically)
2. Write a patch RAM hook that intercepts either the function's output buffer or the HCI command parameters before they reach the radio
3. Flash the modified patch RAM via jc_toolkit's SPI write interface

Step 1 alone is hard. The BCM ROM is encrypted (the chip's ROM bootloader AES-decrypts it transparently into the chip's internal RAM at boot — readable on-chip but not from the SPI dump). Identifying the right ROM function would require either:

- **InternalBlue-style instrumentation** — write a patch that dumps in-RAM ROM contents over HCI as a debug feature, then disassemble. InternalBlue did this for BCM4335/BCM4339/BCM43430 over years; not done for BCM20734.
- **Side-channel observation** — watch what the patch RAM hooks call into ROM and triangulate the LL/HCI entry points. Tractable but slow.
- **Glitching / chip decap** — physical attacks on the BCM ROM. Months-to-years of skilled work.

**Bottom line at the time of writing**: controller-side wake adv modification is feasible in principle but requires either pioneering BCM20734 InternalBlue work or a public ROM dump that doesn't currently exist. It is no longer "impossible by encryption" — it's "blocked by missing prior art."

The simpler attack of **rewriting the bonded host MAC at SPI `0x2000`** to point the wake adv at a Switch 2 also doesn't work: the controller would still emit a Switch-1-shaped adv (PID `0x2009`, possibly different PDU layout) targeted at the Switch 2's MAC, and the Switch 2's wake filter would reject it for the wrong PID and structure. (Also note: the controller is classic-BT-only in practice — see below.)

## Updates after this doc was written

Two subsequent findings further narrow what's possible:

1. **The Pro Controller emits zero BLE adv on fw 4.33** ([`procon-trigger-test-results.md`](procon-trigger-test-results.md)). So there is no "wake adv we want to detect" on BLE at all — the wake/reconnect traffic is classic-BT paging, invisible to an ESP32 BLE radio. Path 0 (ESP32 scan-and-react) is dead.

2. **SPI writes to patch RAM are rejected on fw 4.33** with status `0x01` "write protected" ([`spi-write-test-results.md`](spi-write-test-results.md)). Even a perfect ROM-symbol-discovery and a perfect patch RAM modification can't be delivered via the standard subcommand `0x11` write path that jc_toolkit and shinyquagsire23's 2017 code use. Nintendo added region-based write protection after 2017.

## The verdict on Path 4 (controller firmware mod)

**Effectively dead on current firmware** (Tier 1 combined success probability ~2-3% per `spi-write-test-results.md:62`). Kept as a long-tail option only if someone reverse-engineers Nintendo's OTA unlock command or finds a patch RAM memory-safety vulnerability.

The strategic implication: the ESP32-as-bridge architecture in [`../status-and-next-steps.md`](../status-and-next-steps.md) (Path A: pair Pro Controller TO ESP32 via classic BT, relay HOME presses) or Path B (physical piggyback mod) are the viable paths to the original goal. Path 0 (scan-and-react) is empirically dead. This doc's original conclusion that Path 0 was "the highest-ROI choice" was wrong — it was based on the then-prevailing hypothesis (from HelloRayH's SDR captures) that the controller emits detectable BLE adv, which was later falsified.

## Reproducibility

The exact commands used (run from `references/mfro-switch-controller-testing/`):

```bash
# Search for the unique adv prefix and mfr data start
xxd -p pro_spi.bin | tr -d '\n' | grep -boE "5305010003"
xxd -p pro_spi.bin | tr -d '\n' | grep -boE "1bff5305"
xxd -p pro_spi.bin | tr -d '\n' | grep -boE "0201061b"

# Search for VID + Switch 1 Pro Controller PID
xxd -p pro_spi.bin | tr -d '\n' | grep -boE "7e050920"

# Search for the bonded host MAC
xxd -p pro_spi.bin | tr -d '\n' | grep -boE "9522990500"

# Same searches against patch RAM only
xxd -p tmp0 | tr -d '\n' | grep -boE "5305010003"
xxd -p tmp0 | tr -d '\n' | grep -boE "9522990500"

# Strings analysis to confirm what code IS in patch RAM
strings -a -n 4 -t x tmp0 | grep -iE "wake|adv|sleep|hci|le_set|raizo|ukyo"
```

All commands return zero matches except the bonded-MAC search against `pro_spi.bin`, which returns exactly one hit at `0x2000`. The strings analysis returns the `ukyo_hci_*` sleep-mode handler family and the `nintendo/raizoEP2/../ukyo/` source path strings.
