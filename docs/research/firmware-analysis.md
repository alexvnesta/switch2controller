# Firmware-diff dead end

We spent ~1 hour attempting to reverse-engineer the wake mechanism by byte-diffing GuliKit controller firmware (pre-wake V3.31 vs post-wake V5.07). This document records why it failed and what we learned.

## The hope

GuliKit's changelog explicitly claims V5.03 "Enable[d] the Switch 2 wake-up feature." If we could diff pre-wake vs post-wake firmware and isolate the changed code, we would see exactly what a working wake implementation looks like on a real commercial controller.

## What we did

1. Downloaded three GuliKit BT module firmware versions:
   - `NS39_BT_V3.23.zip` (pre-wake, from archive.org)
   - `GuliKit_NS38_NS39_NS59_BT_V3.31.zip` (last pre-wake, from GuliKit CDN)
   - `GuliKit_NS38_NS39_NS59_BT_V5.07.zip` (post-wake, from GuliKit.com)

2. Extracted each `.exe` flasher (all three are 2,245,632 bytes — same MFC Windows flasher stub, only contents differ).

3. Analyzed section layout — all three flashers have identical PE section layout.

4. Ran byte-level diff:
   - `.text` (flasher code): **0 bytes different**
   - `.rdata`: 3 bytes different — the literal `V3.31` → `V5.07` version string in UTF-16
   - `.data`: 358,479 bytes different, all within file offset `0x171000`–`0x1ca000` (~357 KB)
   - `.rsrc`, `.reloc`: identical

5. Extracted the high-entropy firmware blob from `.data`.

## Why it dead-ended

**The firmware blob is encrypted at rest.** Entropy measured at 7.99 bits/byte (max possible for binary data), flat across all 330 KB of the high-entropy region. Characteristic of AES-CCM-encrypted data.

A follow-up research pass confirmed this is **not GuliKit-specific**: it's how consumer Bluetooth controller MCUs work. A 8BitDo `.dat` file measured 7.998 bits/byte entropy. Telink/Realtek/Nordic BT SoCs have hardware-enforced AES-CCM code authentication in their bootloaders — vendors do not choose to encrypt; the chip requires it.

The decryption key lives in ROM on the BT chip itself and is inaccessible without hardware attacks (fault injection, ROM dump via JTAG, etc.).

## What we learned anyway

1. **GuliKit's flasher is plain**: Visual Studio 2010 + MFC, using an embedded 8.7 KB `DeviceUsb.dll` (also identical across versions) that wraps Windows HID APIs. The flasher does not transform the firmware blob — it's transmitted as-is over USB HID to the BT chip, which decrypts internally. This means **USB sniffing would not reveal plaintext either**.

2. **Multiple VID/PID values appear in `.text`**: the flasher code contains x86 instructions referencing `7e 05` (Nintendo VID) but only in places that match normal x86 conditional-jump patterns — these are **false positives from random byte matches**, not data. The firmware blob is the real target and it's encrypted.

3. **The first divergence byte at offset `0x1712e0`** in the firmware blob is a single byte (`0x43` → `0x4b` → `0xfb` across versions). Likely a payload length field or version number inside a header. The bytes before it are MFC RTTI metadata identical across versions, not firmware.

4. **330 KB blob size** is consistent with a BT module's flash capacity (typical BT SoCs have 256–512 KB flash).

## Conclusion

**Firmware diffing is the wrong approach.** Instead, the path that worked was finding community reverse-engineering that used **RF/BLE sniffing** (nRF52840 BLE sniffer, SDR on 2.4 GHz) to capture the wake packet on the wire. That approach bypasses on-chip encryption entirely and yielded the definitive 31-byte packet structure.

See [`wake-packet.md`](./wake-packet.md) for the result.

## Important scope correction (added after finding mfro's prior work)

The encryption claim above is correct **for GuliKit's firmware blob and for the BCM ROM region**, but **not for the Pro Controller's BCM20734 patch RAM**. The patch RAM lives in the controller's external SPI flash starting at `0x10000` and is **plain ARM Thumb code, unencrypted**. This is confirmed by:

- [`mfro/switch-controller-testing/tmp0`](https://github.com/mfro/switch-controller-testing) — a 84 KB extraction of the patch RAM region from a real Pro Controller's SPI dump, with a Hopper disassembly project (`tmp0.hop`) already done. See [`references-inventory.md`](references-inventory.md) for the full inventory.
- [CTCaer's jc_toolkit](https://github.com/CTCaer/jc_toolkit) — reads/writes this region as part of its standard SPI backup/restore workflow.
- [ndeadly/switch2_controller_research/memory_layout.md](https://github.com/ndeadly/switch2_controller_research/blob/master/memory_layout.md) — documents the equivalent layout for Switch 2 controllers, where only the Initial FW region (`0x0`–`0x10FFF`) is encrypted with an ARM-Trusted-Firmware-derived header `0xAA640001`; everything else (factory data, patch RAM banks, pairing info, calibration) is in the clear.

What this means: a "modify the wake adv from the controller side" path is *not* gated by ROM decryption. It's gated by (a) understanding the patch RAM ABI well enough to hook the ROM function that constructs wake advs, and (b) finding a write path to inject the modified patch RAM (jc_toolkit can write SPI). This is hard, but it's "weeks of focused Broadcom RE work" hard, not "months-to-never of chip glitching" hard. Worth keeping in mind as a Path 4 option, even if Paths 0/B remain more pragmatic.

**Further refinement after a byte-pattern hunt across mfro's SPI dump** ([`firmware-hunt-results.md`](firmware-hunt-results.md)): the wake adv constants and host MAC are NOT pre-templated anywhere in the SPI flash. The wake adv is dynamically constructed at runtime by code in the encrypted BCM ROM, not in the open patch RAM. So the patch RAM is open and modifiable, but the specific code we'd want to modify isn't there — to change the wake adv shape we'd need to identify and hook the relevant ROM function, which requires either pioneering BCM20734 InternalBlue work or a ROM dump that doesn't currently exist publicly. Path 4 stays a long-tail option, not on the critical path.

## Files in this repo

```
firmware/
  pre-wake/
    v3.23/  NS39_BT_V3.23.exe + extracted DeviceUsb_v3.23.dll
    v3.31/  GuliKit_NS38_NS39_NS59_BT_V3.31.exe + extracted DLL + blob
  post-wake/
    v5.07/  GuliKit_NS38_NS39_NS59_BT_V5.07.exe + extracted DLL
  main-lineage/
    GuliKit_NS39_V4.1.zip (main controller firmware, separate from BT)
```

Kept for archival/future re-analysis if someone manages to extract the chip's ROM decryption key.
