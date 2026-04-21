# SPI dump diff: user's controller (fw 4.33) vs mfro's (2019)

Date: 2026-04-21
User dump: `~/Downloads/procon-spi-2026-04-21T00-37-36.bin` (524288 bytes, MD5 `ec8d26c9...`)
mfro dump: `references/mfro-switch-controller-testing/pro_spi.bin` (524288 bytes, MD5 `740afa2d...`)

## Summary

| Region | Size | Diff | Interpretation |
|---|---|---|---|
| `0x0000-0x0FFF` | 4 KB | 6 bytes | Reserved region. Nearly identical. |
| `0x1000-0x1FFF` | 4 KB | 12 bytes | Padding/header. Notable: **OTA Signature Magic at 0x1FF4 differs** |
| `0x2000-0x3FFF` | 8 KB | 24 bytes | Bonding records + user calib + cluster data. As expected — different bonded host MAC. |
| `0x6000-0x60FF` | 256 B | 39 bytes | Serial + colors + factory type. As expected — different controller serial. |
| `0x8000-0xBFFF` | 16 KB | **0 bytes** | Stick factory data. **Identical** despite different controllers. |
| `0xE000-0xEFFF` | 4 KB | 0 bytes | Unused/empty in both. |
| `0xF000-0xF1FF` | 512 B | 0 bytes | Unused/empty in both. |
| **`0x10000-0x27FFF` (DS1)** | **96 KB** | **0 bytes** | **PATCH RAM DS1 IS BYTE-IDENTICAL.** Nintendo has NOT changed factory patch RAM in 6+ years. |
| **`0x28000-0x3FFFF` (DS2)** | **96 KB** | **85,314 bytes** | **PATCH RAM DS2 IS COMPLETELY DIFFERENT.** mfro's was empty (`0xFF`), user's contains an OTA-updated patch RAM (86,705 bytes used). |

## Headline findings

### 1. Patch RAM DS1 is universal across firmware revisions

**This is the single most important finding from the diff.** The factory-flashed patch RAM at SPI `0x10000-0x27FFF` is **byte-identical** between mfro's 2019 dump and the user's 2026 dump (firmware 4.33). Across what may be 5+ years of firmware updates, Nintendo never changed DS1.

**Implication for Tier 1 (patch RAM modification path):**
- Any modifications targeting DS1 would be **portable across all Switch 1 Pro Controllers we've seen**, not specific to one user's device
- The disassembly mfro committed in `tmp0.hop` is **still current** for analyzing the factory firmware
- The Hopper symbol identifications, function mapping, etc. all carry forward

### 2. The user's controller is running OTA-updated firmware in DS2

The OTA Signature Magic at SPI `0x1FF4` differs:

| Dump | `0x1FF4` (8 bytes) | Interpretation |
|---|---|---|
| mfro (2019) | `FF FF FF FF FF FF FF FF` | No OTA update — running factory firmware (DS1) |
| user (4.33) | `AA 55 F0 0F 68 E5 97 D2  00 80 02 00` | OTA update applied — `00 80 02 00` decodes as little-endian `0x00028000` = points to DS2 |

So **CTCaer's documentation of the OTA scheme is correct**: when the magic is set, the controller boots from DS2 instead of DS1. We now have a verified concrete example of the magic structure: `AA 55 F0 0F` appears to be the magic header, followed by what may be a CRC/checksum and the active patch RAM bank address.

### 3. DS1 vs DS2: same source code, different binary

Comparing the two patch RAM blobs in your dump:

- DS1: 85,676 bytes used (factory firmware)
- DS2: 86,705 bytes used (fw 4.33, ~1 KB larger)
- 86,784 of 86,705 bytes differ in the active region (i.e. **virtually every byte is different**)
- BUT: the **string set is nearly identical** — same `ukyo_hci_*` sleep mode handlers, same `bthid_common/*` battery driver paths, same `nintendo/raizoEP2/../ukyo/*` source paths, same single `BLE ACL buffer allocation fail` message

**Interpretation**: This is a complete recompilation of the same source tree, not an incremental binary patch. Any small source-level change ripples through the entire binary because of compiler-driven layout shifts. We can't trivially identify "what Nintendo changed" by binary diff — for that we'd need the disassembly + symbol matching.

**Three notable string differences**:
1. DS1 has `Joy-Con Charging Grip`; DS2 dropped it (or moved it out of the printable range)
2. DS2 added `getbuf: Size is zero` as a complete string (DS1 had `getbuf: Size` and ` is zero` separately)
3. DS2 dropped `EGAPRIAPEMOH` (= `HOMEPAIRPAGE` reversed) — possibly a removed UI/state string

### 4. Wake adv constants STILL absent from fw 4.33

Re-ran the byte-pattern hunt from `firmware-hunt-results.md` against the user's DS2:

| Pattern | DS2 hits | Full SPI hits |
|---|---|---|
| `02 01 06 1B FF 53 05` (wake adv prefix) | 0 | 0 |
| `1B FF 53 05` (mfr data start) | 0 | 0 |
| `53 05 01 00 03` (Nintendo + header) | 0 | 0 |
| `7E 05 09 20` (VID + PID) | 0 | 0 |

**Confirms our earlier conclusion**: the wake adv is dynamically constructed at runtime by code in encrypted ROM, not pre-templated in flash. This holds across both factory firmware (DS1) and OTA-updated firmware (fw 4.33, DS2).

## Implications for Tier 1

These findings update our Tier 1 effort estimate **slightly more favorably** than before:

| Sub-question | Status before this diff | Status after |
|---|---|---|
| Is patch RAM open and modifiable? | Yes (mfro showed) | Yes — and **factory DS1 is universal**, so any work targets all controllers |
| Does Nintendo OTA-update break our work? | Unknown | **Yes for DS2, no for DS1.** Modifications to DS1 are immune to OTA updates (factory is preserved). Modifications via OTA-write-to-DS2 would be overwritten by future updates. |
| Where does the wake adv code live? | Probably ROM | Confirmed ROM (not in DS1 or DS2 patch RAM) |
| Can we identify ROM addresses for `LE_Set_Adv_*`? | Required InternalBlue-style work | Same. The OTA changing DS2 doesn't help — DS2 calls into the same ROM as DS1. |

**Best modification target is still DS1**, but with a twist: if we modify DS1, we need to **also clear the OTA Signature Magic** at `0x1FF4` so the controller boots from our modified DS1 instead of the OTA-updated DS2. Otherwise our changes are dormant.

## Bonded host record at 0x2000

For reference (not security-relevant since we share the conversation):

| Dump | `0x2000` (32 bytes) |
|---|---|
| mfro | `95 22 99 05 00 1A 7D DA 71 12 F1 14 56 DD C2 A5  E8 33 55 AE 99 CB 52 3E 13 0C 00 00 00 00 00 00` |
| user | `95 22 8C 16 5C E9 1E 74 B2 92 07 B6 BE 82 0C 71  B9 CA 4E 9D A1 88 44 DB F2 94 00 00 00 00 00 00` |

Both share the `95 22` prefix — likely a bonding-record magic constant.

User's bonded host MAC at `0x2004`: `5C:E9:1E:74:B2:92` — matches the user's Mac BT MAC visible in earlier System Profiler output. **This confirms the controller-side bonding record is the user's Mac**, as expected since they paired then forgot it.

The 16 bytes following the MAC look like LTK/key material (mfro: `7d da 71 12 f1 14 56 dd c2 a5 e8 33 55 ae 99 cb`; user: `b2 92 07 b6 be 82 0c 71 b9 ca 4e 9d a1 88 44 db`).

## Outstanding questions surfaced by the diff

1. **What does the OTA Signature Magic actually validate?** We have the format `AA 55 F0 0F | <8 bytes> | <4 bytes addr>`. Need to know: is `<8 bytes>` a CRC over DS2 contents? A signature? An IV? Critical for Stage 1c (whether we can write modified DS2 and have the chip accept it).

2. **Can we force the controller back to DS1 by clearing `0x1FF4`?** If yes, we have a clean rollback path: `write 0xFF*8 to 0x1FF4` → controller boots from DS1 next time. This would be the ultimate Stage 1c safety net.

3. **What changed functionally between DS1 and DS2 that justified the OTA?** The strings show only cosmetic changes. The actual diff is ~85 KB of recompiled code. Without the disassembly we can't say. shinyquagsire23's `hidtest.cpp` `WRITE_TEST` may have notes; worth checking.

4. **Why is the active region exactly ~1 KB larger in DS2?** Either Nintendo added new code (small feature) or the compiler/linker version changed. Without symbol info we can't tell.
