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
