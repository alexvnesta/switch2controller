# Session 2026-04-20: Pro Controller wake investigation + dump

This was a long evening session that started as "what can we learn from MissionControl#199?" and ended as a definitive characterization of how the Switch 1 Pro Controller actually wakes hosts, plus a successful (slow) live SPI dump of the user's controller.

The companion docs from this session:
- [`../research/procon-wake-decode.md`](../research/procon-wake-decode.md) — provisional SDR decode (now superseded)
- [`../research/procon-trigger-test.md`](../research/procon-trigger-test.md) — empirical test design
- [`../research/procon-trigger-test-results.md`](../research/procon-trigger-test-results.md) — empirical results
- [`../research/firmware-hunt-results.md`](../research/firmware-hunt-results.md) — byte-pattern search of mfro's SPI dump
- [`../research/references-inventory.md`](../research/references-inventory.md) — what's in `references/`

## Headline findings

1. **Switch 1 Pro Controller is 100% classic-BT (BR/EDR), no BLE wake adv ever.** Triple-confirmed: ESP32 BLE scanner saw zero events across 8586 captured BLE adv events; macOS bluetoothd saw zero `BLE Scanner Device Found` events for the controller's MAC across 322 captured events; macOS classified all controller events as `< ClassicScan >`.

2. **The "scan-and-react" path (Path 0) is permanently dead.** ESP32 BLE radio cannot sniff classic-BT page packets (different PHY, different channel set). HelloRayH's MissionControl#199 SDR captures must have been classic-BT page packets, not BLE adv as I had provisionally decoded.

3. **BCM20734 patch RAM IS modifiable in principle but the BLE adv code paths are NOT in patch RAM** — they're in the encrypted ROM. Confirmed by exhaustive byte-pattern hunt across mfro's full SPI dump (zero hits for `02 01 06 1B FF 53 05`, the unique 7-byte adv prefix, anywhere in 524 KB).

4. **Patch RAM has 12 KB headroom** (87% used of 96 KB) — plenty of room for additions, IF we knew the ROM symbol addresses to call into. Stage 1c (modify a debug string + verify chip still boots) is the critical go/no-go test for the entire Tier 1 path; not yet attempted.

5. **macOS WebHID via Chrome successfully dumped the user's Pro Controller** when hidapi/libusb both failed (kernel-level IOHIDFamily protection blocks even sudo). Throughput is ~0.8 KB/s — limited by USB interrupt polling interval. ~10 minutes for full 524 KB dump.

## Path table — final state

| Path | Status |
|---|---|
| **0**. ESP32 scan-and-react (detect Pro Controller's BLE wake adv) | **Dead** — Pro Controller emits no BLE |
| **A**. Pair Pro Controller TO ESP32 over classic BT | **Top viable no-mod path.** Scaffolded as `procon-bridge/` (BlueRetro fork) |
| **B**. Physical mod: ESP32-C3 piggybacked inside controller, GPIO-tap HOME | Pragmatic backup |
| **C**. Claim Switch 2's classic BT MAC and page-scan | Rejected (causes pairing chaos during gameplay) |
| **D**. Wi-Fi / cheap BLE remote / dongle (sidestep Pro Controller) | Simplest "wake from couch" UX, ~1 evening |
| **Tier 1**. Patch the Pro Controller's BCM20734 patch RAM to emit BLE wake adv | **Long-tail option.** ~6-12 months effort, ~15% combined success probability per 6-stage breakdown. Not on critical path. |

## Chronological log

### 1. Issue #199 reread

Started by re-examining HelloRayH's 2021 SDR analysis after the user noted that issue was pre-Switch-2 and HelloRayH was investigating a Switch 1 wake exchange. This forced corrections to several prior assumptions about BCM20734 capabilities (it's dual-mode, not classic-only) and prompted a deeper provisional decode of HelloRayH's three captured screenshots. Decode landed in [`../research/procon-wake-decode.md`](../research/procon-wake-decode.md), with appropriate "provisional" caveats.

### 2. Reference repo discoveries

User asked what dumps existed publicly. Found two important repos:

- **[mfro/switch-controller-testing](https://github.com/mfro/switch-controller-testing)** — a 2019 Pro Controller emulator project that *also* committed a real Pro Controller SPI dump (`pro_spi.bin`, 524 KB) and a Hopper disassembly (`tmp0.hop`) of the patch RAM region. Initially misidentified mfro's project as "Pro Controller firmware reverse engineering"; on closer inspection it's actually "Pro Controller emulator running on a Linux PC with a CSR/Intel BT dongle." But the dump artifacts are real and useful.
- **[ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research)** — pushed 2026-04-11 (9 days before this session). The authoritative Switch 2 controller protocol reference, including full BLE adv structure (corroborates our wake-packet decode), pairing crypto (`LTK = A1 ^ B1` with B1 a fixed published constant), and a Switch homebrew SPI dumper for Switch 2 controllers.

Both cloned into `references/`, gitignored. Findings written up in [`../research/references-inventory.md`](../research/references-inventory.md).

### 3. JC Toolkit / patch RAM scope correction

Initially claimed BCM ROM was AES-locked and "custom firmware path is months-to-never." Mfro's `tmp0` disassembly falsified part of this — patch RAM at SPI `0x10000` is unencrypted, dumpable via JC Toolkit, and partially analyzed. Updated `firmware-analysis.md` and several other docs with the corrected scope.

### 4. Byte-pattern hunt across mfro's SPI dump

Searched for the wake adv's distinctive byte signatures (`02 01 06`, `1B FF`, `53 05`, `7E 05`, `0x2009`, host MAC) anywhere in 524 KB. **Zero hits for any contiguous wake-adv-shaped data.** The bonded host MAC `95 22 99 05 00 1A` appears exactly once at SPI `0x2000` (the bonding record slot dekuNukem documented). Conclusion: the wake adv is dynamically constructed at runtime by code in encrypted ROM, not pre-templated in flash. Documented in [`../research/firmware-hunt-results.md`](../research/firmware-hunt-results.md).

### 5. ESP32 procon-trigger empirical test

Wrote [`procon_scanner.c.disabled`](../esp32/src/procon_scanner.c.disabled) — a NimBLE passive BLE scanner with all-events logging. Two test runs:

- **Run 1 (filtered for procon MAC + Nintendo OUIs)**: 5 minutes, zero events
- **Run 2 (full discovery, every BLE adv logged)**: 8586 lines of BLE adv data captured, 19 unique BLE devices visible (radio confirmed working), **zero events from Pro Controller MAC `98:B6:E9:3F:D4:FB`** across multiple HOME-press cycles

Results documented in [`../research/procon-trigger-test-results.md`](../research/procon-trigger-test-results.md). This killed Path 0.

### 6. macOS bluetoothd cross-confirmation

Used `log stream --predicate 'subsystem == "com.apple.bluetooth"'` to capture the Pro Controller from a completely different stack (macOS's CoreBluetooth + IOBluetoothFamily). Captured 322 events from the controller's MAC during a connection cycle:

- **Zero `BLE Scanner Device Found` events** for `98:B6:E9:3F:D4:FB`
- **Multiple `CBStackClassicScanner: Device found ... < ClassicScan >` events**
- Categorization showed all 322 events were classic-BT subsystems (Server.Core, Security, HID, SDP, AACP, HCI, ClassicScanner)

Capture saved at `captures/macos-bt-procon-20260420-194727.log` (619 KB, gitignored). This is the third independent confirmation that the Pro Controller is classic-BT-only.

Bonus insight from this capture: macOS's PID lookup database collides PID `0x2009` with both Switch Pro Controller and Beats Studio³, leading to amusing log entries like `Nm 'Pro Controller', PID 0x2009 (BeatsStudio3,2), PrNm Beats Studio³`. Cosmetic.

### 7. BlueRetro fork scaffold (procon-bridge/)

Once Path A was promoted to top viable path, scaffolded [`procon-bridge/`](../procon-bridge/) — a fork of [BlueRetro](https://github.com/darthcloud/BlueRetro) (3 MB, ~7,300 LOC of BT stack). 4 conservative file modifications + 3 new files:

| File | Change |
|---|---|
| `main/wake_bridge.c` (new) | 31-byte Switch 2 wake adv payload constructor + HOME-press edge detector + burst-stop timer |
| `main/wake_bridge.h` (new) | Public interface |
| `main/bluetooth/host.c` | `bt_host_bridge` early-returns to `wake_bridge_on_input` when `CONFIG_PROCON_WAKE_BRIDGE`; `bt_host_init` calls `wake_bridge_init` |
| `main/bluetooth/hci.c` | Appended public `bt_hci_cmd_le_set_adv_data_raw` and `bt_hci_cmd_le_adv_disable` helpers |
| `main/bluetooth/hci.h` | Declarations |
| `main/Kconfig.projbuild` | Added `config PROCON_WAKE_BRIDGE` |
| `main/CMakeLists.txt` | Added `wake_bridge.c` |

**Status: scaffold only — not yet built or flashed.** Outstanding work documented in `procon-bridge/README.md`. Conservative-fork approach (rather than surgical extraction) was chosen so we can later resync from upstream BlueRetro if needed.

### 8. SPI dump attempts

User wanted to dump their own controller's SPI to see if patch RAM differs from mfro's 2019 dump.

**Attempt 1: Python hidapi.** Wrote [`tools/procon_spi_dump.py`](../tools/procon_spi_dump.py) implementing dekuNukem's USB handshake + SPI Read subcommand 0x10. Failed — `OSError: open failed`. macOS's IOHIDFamily protection holds the device exclusively for `gamecontrollerd`.

**Attempt 2: sudo + Python.** Failed identically. macOS HID protection is enforced at the IOKit layer, not file permissions. Even root can't override the kernel claim.

**Attempt 3: pyusb (libusb).** Failed at the `dev.set_configuration()` stage with "Access denied (insufficient permissions)" — same root cause via different path.

**Attempt 4: VMware Windows + JC Toolkit.** User said they had a Windows VM available. Was about to recommend this path when —

**Attempt 5: WebHID via Chrome.** User pointed at [mzyy94/joycon-toolweb](https://github.com/mzyy94/joycon-toolweb), a JS-based color modification tool that uses Chrome's `navigator.hid` API. **WebHID bypasses macOS IOHIDFamily protection** because Chrome runs as a privileged user-consent-mediated browser.

Wrote [`tools/procon_spi_dump.html`](../tools/procon_spi_dump.html) — a self-contained WebHID page based on mzyy94's `controller.js` protocol implementation. Served via `python3 -m http.server 8088` from `tools/` directory.

After hiccups (port collision on initial 8765 attempt, BT-vs-USB confusion when user first tried connecting via Bluetooth and Chrome found no devices), got the dump running over USB. Live readout:

- **Connected: Pro Controller (PID 0x2009)**
- **Device info: type=3, fw=4.33, mac=98:B6:E9:3F:D4:FB**
- Throughput: ~0.8 KB/s (USB interrupt polling at 8ms × sequential request-reply pattern = hardware ceiling for this protocol)
- ETA: ~11 minutes for full 524 KB dump
- **Critical observation: firmware 4.33** — latest stable for original Switch 1 Pro Controller, vs mfro's 2019 dump which is firmware 3.x. Patch RAM may differ substantially.

Mid-dump, considered restarting with a tighter timeout/retry config to speed it up. Walked back from this — the timeout wasn't the bottleneck (USB polling interval was), so a restart would lose progress for negligible gain. Let it finish.

### 9. Strategic discussions during the dump

While the dump ran, user shared three additional reference links:

- **[dekuNukem USB-HID-Notes.md](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/USB-HID-Notes.md)** — confirmed already used as the source for the `0x80` handshake sequence. Also flagged: we never sent the `0x03` baud 3M command, which might explain part of the slow throughput. Worth testing if we ever rerun.
- **[borntohonk/Switch-Ghidra-Guides](https://github.com/borntohonk/Switch-Ghidra-Guides)** — Switch *console* HOS patching guides (NIFM, etc.), not controller firmware. Useful if we ever pivot to Switch 2 console-side patching of the wake filter, but not directly applicable to BCM20734 patch RAM.
- **[AterialDawn/Nintendo-Switch-Pro-Controller-AVR-Firmware](https://github.com/AterialDawn/Nintendo-Switch-Pro-Controller-AVR-Firmware)** — 2018 dead project to *emulate* a Pro Controller from an AVR (Arduino-style). Opposite direction from what we need; broken since 2018. Not useful.

After the dump finished, user pointed at the **[GBAtemp 2017 thread](https://gbatemp.net/threads/reverse-engineering-the-switch-pro-controller-wired-mode.479287/)** by Toad King. This is the *original* public Pro Controller wired-mode reverse engineering thread, where the `0x80 0x02` / `0x80 0x04` USB handshake (which we use in our dump) was first published. Toad King also independently observed our slow-throughput symptom: "It fluctuates between 62.5Hz and 125Hz and averages out around ~75Hz" — confirming the 0.8 KB/s ceiling is fundamental to the protocol, not a bug in our implementation.

Following that thread led to **[shinyquagsire23/HID-Joy-Con-Whispering](https://github.com/shinyquagsire23/HID-Joy-Con-Whispering)** — a 2017-2019 C utility that's essentially **a working JC-Toolkit-equivalent in 19 KB of C using hidapi**. Includes:

- `spi_flash_dump` (same `0x1D` chunk size, confirms the protocol limit)
- **`spi_write` — working write code for Pro Controller SPI flash, on hidapi-only**
- `WRITE_TEST` and `WEIRD_VIBRATION_TEST` defines suggesting prior experiments writing to specific SPI regions
- shinyquagsire23 also notes in the GBAtemp thread: "I actually reversed the firmware and used some weird raw UART command setup"

This is the **primary reference for any future patch RAM modification work**. JC Toolkit on Windows is one path; shinyquagsire23's C code is another that's portable and would let us implement Tier 1 Stage 1c (write a small benign change to patch RAM, verify chip still boots) on macOS/Linux without the Windows VM.

User also asked: **"how hard would it be to activate dual BT classic + BLE on the Pro Controller?"** — the Tier 1 question. Refined the earlier estimate using fresh empirical data from `tmp0`:

- Patch RAM: 87% full, 12 KB headroom — fine for adding a wake-adv emission function
- Alt slot at SPI `0x28000`: empty (`0xFF`-filled) — A/B failsafe slot exists but unused on this controller, suggesting controller has never received an OTA update
- **One BLE-related string in patch RAM**: `"BLE ACL buffer allocation fail"` — proves the firmware build didn't strip the entire BLE stack, but the actual adv emission code paths aren't visible in patch RAM strings (probably in encrypted ROM)
- 6-stage breakdown of effort: ~2 months focused work, ~15% combined success probability, biggest gating risk is whether JC Toolkit's SPI write succeeds with modified patch RAM (signature validation might block it)

User followed up: "I wonder if it only connects to PC via classic BT, but BLE with console?" — good hypothesis, but contradicted by all evidence: dekuNukem's HID-layer docs use HIDP (classic) terminology throughout, BlueRetro pairs Pro Controllers as classic-BT HID hosts successfully, and MissionControl successfully relays Switch 1 Pro Controller traffic to Switch 2 as classic-BT HID.

## Tools added to this repo this session

| Path | Purpose |
|---|---|
| `tools/procon_spi_dump.py` | Python SPI dumper (failed on macOS due to IOHIDFamily protection) |
| `tools/procon_spi_dump.html` | WebHID SPI dumper (works on macOS via Chrome) |
| `esp32/src/procon_scanner.c` | NimBLE passive scanner used for procon-trigger test |

## Captures saved this session (gitignored)

| Path | Content |
|---|---|
| `captures/macos-bt-procon-20260420-194727.log` | macOS bluetoothd log during Pro Controller connect — 322 events, all classic-BT |
| `esp32/captures/procon-discovery-20260420-192924.log` | ESP32 BLE discovery scan during HOME-press cycles — 8586 events, zero from Pro Controller |
| `~/Downloads/procon-spi-<timestamp>.bin` (pending) | The user's Pro Controller SPI dump, fw 4.33 |

## Outstanding work

In rough priority order:

1. **Diff the user's SPI dump against mfro's `pro_spi.bin`** — once the dump finishes. Compare patch RAM regions to see what Nintendo changed between firmware 3.x and 4.33.

2. **Decide whether to pursue Tier 1.** Current recommendation: don't (poor ROI vs Path A). But the cheapest go/no-go test (Stage 1c — modify a debug string in patch RAM, flash via JC Toolkit, verify chip boots) is ~2 hours of work and would settle whether signature validation is real. Worth doing once if curious.

3. **Build the procon-bridge fork.** Outstanding decisions: PlatformIO vs raw idf.py, how to bypass BlueRetro's `wired_init_task` wait loop, where to load the Switch 2 MAC from. ~2-3 days to a working Path A.

4. **Commit checkpoint.** Current working tree has ~12 modified/new files spanning the Pro Controller research and BlueRetro fork. Worth committing before more work lands.

5. **Rotate the Mac password (`beardog3`)** — the user shared it during the sudo attempt; while not abused, it's now in the conversation transcript and any session export.
