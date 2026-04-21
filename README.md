# switch2controller

Wake a Nintendo Switch 2 from sleep using a cheap ESP32 as a BLE beacon — reverse-engineered from scratch.

**Status**: wake-beacon working. Pressing a button on the ESP32 wakes a configured Switch 2. The ongoing open problem is triggering that wake from a Switch 1 Pro Controller HOME press, which turns out to be much harder than it sounds.

---

## Pick your path

### 🔧 "I just want to wake my Switch 2"

Flash the wake-beacon firmware to an ESP32, capture two MAC addresses, press the BOOT button. See **[`docs/setup.md`](docs/setup.md)** — ~10 minutes if you have an ESP32 already.

### 📖 "I want to understand what was found"

The highlights, each linked to the primary doc:

| Finding | Where |
|---|---|
| **Switch 2 wake beacon: exact 31-byte structure** (no encryption, no pairing handshake) | [`docs/research/wake-packet.md`](docs/research/wake-packet.md) |
| **The Switch 1 Pro Controller emits zero BLE** on stock firmware — it uses classic-BT paging. Triple-confirmed empirically. Kills the "scan-and-react" approach. | [`docs/research/procon-trigger-test-results.md`](docs/research/procon-trigger-test-results.md) |
| **Pro Controller patch RAM is write-protected on fw 4.33** — subcommand `0x11` writes to DS1/DS2 return "write protected", while color-region writes succeed. Blocks modifying the controller firmware. | [`docs/research/spi-write-test-results.md`](docs/research/spi-write-test-results.md) |
| **DS1 patch RAM is byte-identical across 6+ years** of Pro Controller firmware revisions. mfro's 2019 disassembly is still current. | [`docs/research/spi-dump-diff-results.md`](docs/research/spi-dump-diff-results.md) |
| **Byte 16 = `0x81` is the wake-trigger flag** in the adv payload — `0x00` during pairing, `0x81` only during sleep-reconnect. This byte was present in prior implementations but its state-dependent meaning was undocumented. | [`docs/research/wake-packet.md`](docs/research/wake-packet.md) |

One-stop synthesis of everything: **[`docs/research/findings.md`](docs/research/findings.md)**.

### 🛠️ "I want to continue the research"

Start with **[`docs/status-and-next-steps.md`](docs/status-and-next-steps.md)** — the top-level path table with recommended next moves. Then **[`docs/README.md`](docs/README.md)** for the full doc index.

Three live paths to wiring up a Pro Controller as a wake trigger:

- **Path A — BlueRetro fork** (scaffold at [`procon-bridge/`](procon-bridge/)): pair the Pro Controller TO an ESP32 over classic BT, fire the wake burst on HOME-press. Top viable no-mod path. ~2-3 days. Open trade-off: single-host bond slot on stock firmware may mean the controller can't simultaneously gameplay-pair to the Switch 2.
- **Path B — physical piggyback mod**: XIAO ESP32-C3 soldered inside the controller, GPIO-tap on the HOME pad. Preserves gameplay pairing with the Switch 2. ~1 evening + soldering. See [`docs/plans/pro-controller-piggyback-mod.md`](docs/plans/pro-controller-piggyback-mod.md).
- **Path D — sidestep**: Wi-Fi HTTP trigger, or a $3 BLE remote button. Ships in an afternoon. Doesn't use the Pro Controller specifically.

Dead ends (kept documented for audit trail):
- Path 0 (ESP32 scan-and-react) — empirically killed: controller emits no BLE
- Tier 1 (custom Pro Controller firmware via patch RAM) — blocked by SPI write protection on fw 4.33

---

## How the wake beacon works

The Switch 2 listens on BLE advertising channels 37/38/39 during sleep, watching for a 31-byte advertisement containing:
- Nintendo's Company ID (`0x0553`) + USB VID (`0x057E`)
- A bonded Joy-Con's PID (`0x2066` or `0x2067`) or Pro Controller 2's PID (`0x2069`)
- A specific state flag byte (`0x81` = "paired peer reconnecting in sleep")
- The Switch 2's own BT MAC address, reversed

When a beacon with this exact structure arrives from an address that has previously bonded with the Switch, the BT chip wakes the host. No encryption, no pairing handshake, no LTK required — bonding is the only authentication.

Full byte-level breakdown: [`docs/research/wake-packet.md`](docs/research/wake-packet.md).

## Repository layout

```
docs/                          Research, plans, session logs. See docs/README.md for the index.
  setup.md                       Setup instructions for the wake-beacon firmware
  status-and-next-steps.md       Top-level path table and recommended next moves
  research/                      Empirical findings (most of the knowledge lives here)
  plans/                         Forward-looking work plans (some blocked/superseded)
  sessions/                      Chronological research logs

esp32/                         PlatformIO project — the working wake-beacon firmware
  src/switch2_wake.c             Wake beacon (main firmware; NimBLE-based)
  src/ble_scanner.c.disabled     Scanner firmware (swap with .c/.c.disabled to use)
  src/procon_scanner.c           Pro Controller wake-trigger detector (Path 0 test)
  src/secrets.h.example          Template; copy to secrets.h and fill in

procon-bridge/                 Fork of BlueRetro scaffolded for Path A (pair Pro
                               Controller to ESP32, relay HOME→wake). See procon-bridge/README.md.

tools/                         Standalone analysis utilities
  procon_spi_dump.html           WebHID SPI dumper (macOS via Chrome)
  procon_spi_write_test.html     WebHID SPI write probe (for Tier 1 research)
  ghidramcp/                     Ghidra MCP plugin for collaborative patch-RAM analysis

references/                    Cloned external projects (gitignored — see docs/research/references-inventory.md)
captures/                      Live captures from this project (gitignored)
firmware/                      Archived GuliKit firmware samples (encrypted at rest; abandoned path)
```

## Credits

This project stands on a lot of prior work. The pieces we directly leaned on:

- **[tv/switch2-wake-up](https://github.com/tv/switch2-wake-up)** — first public ESP32 + Flipper implementations of the wake beacon
- **[Minkelxy/xiaoai_switch2_wake_up](https://github.com/Minkelxy/xiaoai_switch2_wake_up)** — independent implementation with annotated payload
- **[ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research)** — authoritative Switch 2 BLE/HID/pairing protocol spec, controller flash layout, and Switch homebrew SPI dumper. Independently confirms our wake-packet decode and adds a large amount of detail we didn't have. What we learned from it: [`docs/research/references-inventory.md`](docs/research/references-inventory.md).
- **[HelloRayH in ndeadly/MissionControl#199](https://github.com/ndeadly/MissionControl/issues/199)** — SDR-level captures of the Pro Controller's wake traffic (2021). Our provisional decode as BLE was later superseded by empirical evidence that the captures were classic-BT page packets.
- **[darthcloud/BlueRetro](https://github.com/darthcloud/BlueRetro)** — Switch 1 Pro Controller classic-BT HID host reference; base for Path A
- **[mfro/switch-controller-testing](https://github.com/mfro/switch-controller-testing)** — Pro Controller HID/SDP emulator (Linux + CSR/Intel BT dongle) and a public SPI dump + Hopper disassembly of a real Switch 1 Pro Controller
- **[CTCaer/jc_toolkit](https://github.com/CTCaer/jc_toolkit)** — Switch 1 Joy-Con / Pro Controller SPI backup/restore tool; documents the patch-RAM addresses (`0x10000` / `0x28000`) and OTA signature magic at `0x1FF4`
- **[dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering)** — original Switch 1 controller HID/SPI/USB documentation

## What this project adds

- Self-contained, documented, reproducible setup for the wake-beacon side
- **Scanner firmware** that recovers your Switch 2's BT MAC from Joy-Con advertisements (no nRF sniffer needed)
- Documented the state-dependent meaning of **byte 16** of the wake payload (`0x00` pairing mode vs `0x81` sleep-reconnect — the byte was present in prior implementations, the semantics weren't written down)
- NVS-backed runtime config (change MACs without reflashing)
- **Empirical settlement of the "does the Pro Controller emit BLE wake adv" question** — triple-confirmed no, across ESP32 scan + macOS bluetoothd + classification flags. Closes a long-running open question from MissionControl#199.
- **Empirical settlement of the "can we write patch RAM on fw 4.33" question** — no, Nintendo added region-based write protection sometime after shinyquagsire23's 2017 work.
- Scaffold for **Path A** (BlueRetro fork for Pro-Controller-to-ESP32-to-Switch-2 relay)

---

## Disclaimer

This is reverse engineering of a protocol Nintendo hasn't documented publicly. Use at your own risk. Not affiliated with Nintendo.

### About this repository's accuracy

**This is an unfinished work-in-progress knowledge synopsis, not a 100%-accurate reference.** It's a snapshot of one person's investigation, intended primarily to give the next researcher a running start rather than to be authoritative. Specific caveats:

- **Written with heavy AI assistance (Claude Code).** Much of the prose, organization, and some of the technical analysis in `docs/` was produced by or with an LLM. The underlying empirical work (SPI dumps, BLE scans, firmware flashing, byte-pattern hunts) was run against real hardware, but AI-assisted synthesis can introduce confident-sounding errors, especially in areas where the model interpolates between genuine observations. Chip identifications, address offsets, and protocol claims should be **verified against primary sources** (dekuNukem, ndeadly/switch2_controller_research, CTCaer/jc_toolkit, mfro/switch-controller-testing) before being built on.

- **Some docs are superseded but kept.** When a hypothesis was falsified by later experiment, the original doc is annotated with a prominent `⚠️ SUPERSEDED` or `⚠️ BLOCKED` header rather than deleted, so the research trail is auditable. If you're cherry-picking individual files, check for the header before trusting the content. The most notable one: `docs/research/procon-wake-decode.md` provisionally decoded HelloRayH's SDR screenshots as BLE `ADV_DIRECT_IND`; later empirical testing (`docs/research/procon-trigger-test-results.md`) showed the controller emits zero BLE at all and uses classic-BT paging.

- **Facts likely to be wrong or incomplete**: chip identifications in deeper subsystems (main application MCU, DSP), exact ROM symbol mappings, speculation about Broadcom SDK ABI, anything phrased as a probability estimate or effort estimate.

- **Facts that are well-grounded**: the 31-byte Switch 2 wake adv structure (cross-validated against two independent prior implementations and ndeadly's authoritative spec, plus empirically working firmware); the empirical "Pro Controller emits zero BLE" finding (triple-confirmed across ESP32 scan + macOS bluetoothd + classification flags); the SPI write protection finding (reproducible via `tools/procon_spi_write_test.html`).

When in doubt, treat this repo as a structured set of leads and open questions, not as ground truth. Corrections via PR welcome.
