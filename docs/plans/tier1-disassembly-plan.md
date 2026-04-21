# Tier 1 plan: BCM20734 patch RAM disassembly + "Hello BLE" custom firmware

This is the concrete work plan for evaluating whether the Pro Controller's stock firmware can be modified to emit BLE adv. Splits into independent stages with clear go/no-go gates.

## Why this might be worth doing

If successful, the result is **a stock-looking Pro Controller that natively wakes the Switch 2 via BLE** — no ESP32 dongle, no piggyback mod, no soldering. The controller would behave identically to a Joy-Con 2 from the Switch 2's perspective for wake purposes.

If it fails (which is more likely), we still produce three things of independent value:
- A symbol map for BCM20734 patch RAM (publishable, useful to anyone working on Switch 1 controller firmware)
- Empirical answer to "is the OTA Signature Magic cryptographic" — settles a long-standing community question
- Confirmation of whether BLE stack code is alive in fw 4.33 (vs. dead-stripped)

## Prerequisites (already in place)

- Two SPI dumps for diff/comparison: mfro's 2019 `pro_spi.bin` + our 2026 `procon-spi-2026-04-21T00-37-36.bin`
- mfro's Hopper disassembly project `tmp0.hop` (DS1, factory firmware)
- ESP32 BLE scanner working (`esp32/src/procon_scanner.c`) for verifying BLE adv emission
- WebHID dump page (`tools/procon_spi_dump.html`) — read working; write would need shinyquagsire23's `spi_write` ported
- macOS bluetoothd logging confirmed for cross-checking

## Stage 1: Scout disassembly (10-15 hours)

Pure information gathering. Zero risk to controller. Tools needed: Ghidra (free, recommended) or Hopper (mac-only, $99 — can use mfro's existing project).

### 1a. Set up disassembly environment (1-2 hours)

- Open `references/mfro-switch-controller-testing/tmp0` (84 KB extracted DS1 patch RAM)
- Configure as **ARM Cortex-M Thumb mode**, base address `0x10000` (matches SPI offset)
- For Ghidra: use the SVD loader for Cortex-M startup; manual function detection on the Thumb branch at offset 0
- Confirm: opening should show recognizable Thumb function prologues (`b5XX push {...}`)

### 1b. Map patch RAM structure (3-5 hours)

Goal: identify the patch table, function pointer references to ROM, and the application-layer entry points.

Concrete tasks:
- Identify the **patch table** — Broadcom patch RAM convention is a header followed by `(rom_address, patch_function_address)` pairs. The bootloader walks this table to install hooks.
- Catalog every **`bl <address>`** instruction whose target is outside the patch RAM range (`0x10000`–`0x27FFF` for DS1) — these are calls into ROM. Each is a ROM symbol Nintendo's linker knew.
- Trace the **`BLE ACL buffer allocation fail` string xref** upward. Determine: is the function that prints this message reachable from any other live code path? If yes, BLE infrastructure is wired in. If no, dead code; Tier 1 dies here.

Output: a list of ~50-200 ROM addresses called from patch RAM, with surrounding context (calling-convention args). This is the **raw material for ROM symbol identification**.

### 1c. Identify candidate BLE adv emission paths (4-6 hours)

Goal: find ROM addresses that correspond to `LE_Set_Adv_Param`, `LE_Set_Adv_Data`, `LE_Set_Adv_Enable` (or Broadcom's internal equivalents like `dhm_LEAdvSetAdvData`).

Concrete tasks:
- Cross-reference ROM call addresses against **InternalBlue's BCM4339/BCM43430 symbol maps** (see [InternalBlue project](https://github.com/seemoo-lab/internalblue)). The BCM20734 is older but same Broadcom SDK family — many internal function offsets were stable across the family.
- Look for the **HCI command buffer allocation pattern**: typically `bcs_buf_alloc(size)` → write opcode + length + payload → `hci_send` (or `mpaf_msgQ_post`).
- Specifically look for code paths that construct payload buffers ending with the bytes `0x06, 0x20` (LE_Set_Adv_Param opcode), `0x08, 0x20` (LE_Set_Adv_Data), or `0x0A, 0x20` (LE_Set_Adv_Enable) **as data being written into a buffer**, not just instruction noise.

Output: best-guess ROM addresses for the three LE adv functions, with confidence ratings.

### 1d. Reverse the OTA Signature Magic (2-3 hours)

Goal: understand the byte structure at SPI `0x1FF4` (`AA 55 F0 0F | 8 bytes | 4-byte address`).

Concrete tasks:
- Find the patch RAM function that **writes** this magic during OTA. It's probably called from the OTA-receive HID command handler.
- Determine: are the 8 mystery bytes a CRC32, CRC16, AES-CMAC, or RSA signature? CRC variants are easy (open-source impl + verify against known-good DS2). CMAC requires a key (probably ROM-stored). RSA would be observably distinct (signed data is much larger than what fits).

Output: known algorithm for the magic, plus whether we can compute valid magics ourselves.

### Stage 1 go/no-go gate

**Pass conditions** (any one of these means proceed to Stage 2):
- (a) BLE infrastructure is alive in patch RAM AND we identified candidate ROM addresses for LE adv functions
- (b) OTA Signature Magic is non-cryptographic (CRC-class)

**Fail conditions** (Tier 1 effectively dies):
- BLE stack is dead-stripped from fw 4.33 AND mfro's 2019 dump shows the same (no live LE adv path to call)
- OTA Magic is cryptographic AND we have no DS1 modification path either (i.e., chip refuses to boot DS1 even with magic cleared)

## Stage 2: "Hello BLE" custom patch RAM (10-15 hours, conditional on Stage 1 pass)

Minimal modification proving BLE adv works. Real risk of bricking; mitigations below.

### 2a. Port shinyquagsire23's `spi_write` to WebHID (3-5 hours)

`tools/procon_spi_dump.html` already does SPI read. Add SPI write using subcommand `0x11`. Reference: [shinyquagsire23/HID-Joy-Con-Whispering hidtest.cpp `spi_write` function](https://github.com/shinyquagsire23/HID-Joy-Con-Whispering/blob/master/hidtest/hidtest.cpp).

Output: `tools/procon_spi_write.html` page that takes a binary file + address and writes it.

### 2b. Stage 1c go/no-go: write a benign change first (2 hours)

**Critical safety experiment.** Modify a single byte in DS1 — e.g., flip one byte in the `Joy-Con (L)` GAP local-name string from `(L)` to `(R)`. Write it back. Power-cycle. Pair to Mac.

- **If Mac sees the controller name change** → SPI writes are accepted, OTA Magic isn't validating signed content of DS1, we have a working modification path
- **If controller bootloops or refuses to pair** → SPI writes are rejected, signature is being checked, Tier 1 path requires reversing the signature scheme first (see Stage 1d)
- **If controller pairs but name unchanged** → write succeeded but didn't take effect; possibly DS2 is still active and our DS1 change is dormant. Try clearing OTA Magic at `0x1FF4` next, retest.

This is the **single most important experiment in the entire Tier 1 path.** If it fails, everything else stops.

### 2c. Append a "Hello BLE" hook (5-8 hours)

If Stage 2b passes:

- Find a hookable function in patch RAM that's called periodically (e.g., a 1-second timer handler, or a connection state-machine tick)
- Append ~64 bytes of code at the end of DS1 (we have ~12 KB headroom — used 85,676 of 98,304)
- Hook code: on every Nth invocation (say, every 100), build a fixed 7-byte BLE adv payload `02 01 06 03 FF DE AD` and call into ROM's LE adv functions identified in Stage 1c
- Update the patch table to install our hook

Verify with the ESP32 BLE scanner: does it see an adv with `02 01 06 03 FF DE AD` payload? If yes — **proof of concept achieved**.

### 2d. Recovery path (already-known mitigations)

If we brick the controller, recovery options in increasing severity:

1. **Clear OTA Magic** at `0x1FF4` — chip falls back to DS1 (factory). If we modified DS1 instead of DS2, this doesn't recover, BUT...
2. **Re-flash factory DS1** — we have mfro's `pro_spi.bin` byte-identical reference. Write mfro's bytes `0x10000-0x27FFF` to our controller. Restores factory state.
3. **JC Toolkit "Restore Backup"** — full SPI restore from a backup we made before any writes. We dumped first, so we have this.

Brick risk is real but not catastrophic — controller is recoverable as long as JC Toolkit can still talk to it over USB (which only fails if our modification crashes the chip *before* USB enumerates, which is unlikely from a small data change).

## Stage 3 and beyond (out of scope for this plan)

If Stage 2 succeeds (BLE adv emits from patch RAM), the remaining work to ship a real Pro Controller wake feature:

- Construct a Switch-2-shaped wake adv payload (we have the spec) — straightforward
- Trigger it from the HOME button press via a HID-input hook — also straightforward
- Test against a real Switch 2 in sleep — most likely works, since wake-side is just byte-pattern matching
- Polish and document

Stage 3 is estimated at another 4-6 weekends. Total all-stages estimate: **6 months of part-time work** for a working stock-looking Pro Controller wake feature.

But Stage 1+2 alone (~25 hours) gives us **near-definitive answers** to whether the path is viable, and either outcome is high-value.

## Recommended starting investment

**Just Stage 1 (10-15 hours).** Pure disassembly, zero risk. Produces a usable BCM20734 patch RAM symbol map, settles whether BLE is reachable, and tells us whether OTA Magic is cryptographic. Any one of those answers materially changes the project's strategic direction.

After Stage 1, we know whether Stage 2 is worth attempting (~10 hours) or whether to abandon Tier 1 and commit fully to Path A (BlueRetro fork) or Path D (Wi-Fi/BLE remote sidestep).
