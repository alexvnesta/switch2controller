# References inventory

What's in `references/`, what's actually useful, and what was misidentified.

## `references/ndeadly-switch2-research/` — authoritative Switch 2 spec

[ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research) (cloned 2026-04-20).

**This is the definitive reference for the Switch 2 controller protocol.** From the maintainer of MissionControl, started 2026-04 with active updates. Independently corroborates everything we derived empirically and adds large amounts of detail we didn't have.

### Highest-value files

| File | What it gives us |
|---|---|
| `bluetooth_interface.md` | Full Switch 2 BLE spec: GATT layout, three advertisement variants (Standard / Reconnection / Wake Console — all `ADV_IND`), the complete pairing protocol with crypto, HID command framing, vendor-specific HCI commands. **Confirms our wake-packet decode.** |
| `commands.md` | Every HID subcommand documented (73 KB). Reference for any controller-emulation work. |
| `memory_layout.md` | Full 2 MB controller flash layout, including encrypted Initial FW region (header magic `0xAA640001`, ARM Trusted Firmware variant), failsafe FW banks, factory data, pairing info, calibration. |
| `tools/switch2-spi-dump.nro` | Switch homebrew binary — runs on the Switch 2 itself and dumps a paired controller's SPI. **The fastest path to obtaining your own Pro Controller 2 or Joy-Con 2 SPI dump.** |
| `captures/{nrf52840,ubertooth-one,usb}/` | Raw protocol captures from real hardware. |

### What we learn from it (corrections to our prior assumptions)

- **Pro Controller 2 PID is `0x2069`** — and its wake adv uses the same 26-byte manufacturer data structure as Joy-Con 2 wake. Our Switch 2 wake filter analysis previously assumed only Joy-Con PIDs (`0x2066`/`0x2067`) were accepted; the filter is more permissive.
- **The pairing protocol is "pseudo-OOB"**: `LTK = A1 ^ B1` where `B1` is the *fixed* Pro Controller 2 public key `5CF6EE792CDF05E1BA2B6325C41A5F10`. There's no real cryptographic protection — anyone who sniffs the pairing exchange can derive the LTK.
- **The wake adv PDU type is `ADV_IND`** for Switch 2 controllers, not `ADV_DIRECT_IND`. Our [`procon-wake-decode.md`](procon-wake-decode.md) had provisionally parsed HelloRayH's SDR screenshots as `ADV_DIRECT_IND`; that parse is most likely wrong (byte alignment in the hex display threw off the PDU header read). **The Switch 1 Pro Controller uses no BLE adv at all** — confirmed empirically ([`procon-trigger-test-results.md`](procon-trigger-test-results.md)): 0 BLE events captured from the controller's MAC across 8586 surrounding-environment BLE events. Wake/reconnect is classic-BT paging, not BLE.

## `references/mfro-switch-controller-testing/` — Pro Controller emulator (NOT firmware mod)

[mfro/switch-controller-testing](https://github.com/mfro/switch-controller-testing) (cloned 2026-04-20). Last commit 2019-08, single commit titled "Luigi combo".

**Initially appeared to be Pro Controller firmware reverse engineering. It is not.** The project is a **Pro Controller emulator** running on Linux against a CSR or Intel USB Bluetooth dongle, used for input automation (the "luigi combo" in `luigi.txt` is a Smash Bros macro).

### What's actually in the binaries

| File | Identified as | Source | Useful for our wake goal? |
|---|---|---|---|
| `pro_spi.bin` | **Real Switch 1 Pro Controller SPI flash dump** (524 KB = 4 Mbit, matches MX25U4033E) | A real Pro Controller mfro had | **Yes** — useful as a reference SPI layout. Last paired host MAC at `0x2000` (`95:22:99:05:00:1A`), 16-byte LTK-shaped block at `0x2008`. Patch RAM at `0x10000`. |
| `spi1` | **Identical** to `pro_spi.bin` (`cmp` confirms byte-for-byte) | Duplicate | No additional value |
| `tmp0` | **Pro Controller's BCM20734 patch RAM, extracted from `pro_spi.bin` starting at `0x10000`** (first 32 bytes match exactly) | Extracted from above | **Yes** — this is the unencrypted ARM Thumb patch-RAM blob. Its existence falsifies our earlier "BCM firmware is fully AES-locked" claim — at least the patch RAM region is readable and modifiable. |
| `tmp0.hop` | Hopper disassembler project for `tmp0` | mfro's analysis | Yes — saves us doing the disassembly from scratch if we want to reverse the patch RAM |
| `pro_spi.hop` | Hopper project for the full SPI dump | mfro's analysis | Yes |
| `rom1` | **BCM20703A1 firmware** (819 KB) — string table contains `"Copyright (c) Broadcom Corp. 2007 BCM2049"`, `"BCM20703A1"`, `"Broadcom Debug Port"`, plus `mpaf/app/hidd/` source paths from Broadcom's HID stack | mfro's PC-side USB Bluetooth dongle, NOT a Pro Controller | **No, but interesting** — confirms what a full unencrypted Broadcom BT firmware looks like (BCM20703A1 is a related dongle chip from the same family as BCM20734) |
| `rom2` | Likely **patch RAM from the same PC dongle** (49 KB, contains BCM patch ROM identifier `"AUM1.03B00"`) | PC dongle | No |
| `rom.hop` | Hopper project for `rom1` | mfro's analysis | Reference only |
| `bt_*.cc/h`, `main.cc`, `sdp.h`, `fiber.cc` | C++ Pro Controller emulator running on Linux. Uses HCI vendor commands to spoof the dongle's BD_ADDR (CSR-style `0xc2` prefix and Intel-style `OGF_VENDOR_CMD 0x31`). Implements full SDP descriptor with VID `0x057E`, the Pro Controller HID descriptor, and `SDP_ATTR_HID_RECONNECT_INITIATE` + `SDP_ATTR_HID_REMOTE_WAKEUP` set true. | mfro's code | **Yes for HID/SDP reference**, no for wake |
| `patches.txt` | A list of write-`0x00fb` patches at sequential SPI addresses — looks like attempts to bypass a checksum or signature check. Not wake-related. | mfro's experiments | No |
| `luigi.txt` | Button-press macros for Smash Bros Luigi combos | mfro's actual goal | No |

### Two takeaways from the mfro pass

1. **Pro Controller patch RAM is dumpable, unencrypted, and disassembled** (`tmp0` + `tmp0.hop`). Initially appeared to be the most plausible attack surface for "modify the wake adv shape from the controller side." Two follow-ups downgraded this: (a) byte-pattern hunt across the full 524 KB SPI dump found zero hits for the wake-adv constants anywhere in flash ([`firmware-hunt-results.md`](firmware-hunt-results.md)) — the adv is constructed by ROM code at runtime, not templated in patch RAM; (b) on fw 4.33, writes to patch RAM are rejected with status `0x01` "write protected" ([`spi-write-test-results.md`](spi-write-test-results.md)) — shinyquagsire23's 2017 SPI write code no longer works. Patch RAM stays valuable as a symbol-discovery resource (strings confirm Broadcom `MpafMode` / `ukyo_hci_*` / `BLE ACL buffer allocation fail` — this is Broadcom BT stack firmware with Nintendo's `ukyo` application layer on top) but isn't a viable modification path on current firmware.
2. **mfro's BT emulator code is a useful reference for the HID/SDP layer** if we ever want to build a more complete Pro Controller emulator on the ESP32 side (e.g. to make Path A from `status-and-next-steps.md` work cleanly). It shows the SDP descriptor a host expects, the HID report descriptor, and the right HCI vendor commands for several BT chip families.

## `references/tv/` and `references/minkelxy/`

Source from the two prior public Switch 2 wake-beacon implementations. Already credited in `wake-packet.md`. Used as cross-validation for the byte structure during the original session.

## `references/BlueRetro/`

Switch 1 Pro Controller classic-BT HID host reference, cited in the abandoned [`../plans/blueretro-fork-plan.md`](../plans/blueretro-fork-plan.md). Excluded from the repo via `.gitignore` (you'd clone it locally if you wanted to revisit that path).
