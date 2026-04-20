# Pro Controller piggyback mod (Path B)

**Status: not yet built.** This is a plan for a future session or a contributor.

Adds a tiny ESP32 inside a Switch 1 Pro Controller that wakes the Switch 2 when you press HOME. The Pro Controller keeps its normal pairing/gameplay relationship with the Switch 2; the ESP32 just piggybacks on the controller's battery and taps HOME electrically.

## Why this path

The original goal was "press HOME on my Switch 1 Pro Controller and wake the Switch 2." We explored several approaches:

- **Passive RF sniffing of the Pro Controller's wake-attempt**: confirmed infeasible. Classic BT pages are addressed and filtered at baseband; nobody has built a passive sniffer on ESP32.
- **ESP32 impersonates Switch 2's classic BT MAC**: causes pairing confusion, rejected.
- **ESP32 pairs to Pro Controller as the Switch**: Pro Controller becomes wake-only, can't use for gameplay.
- **This mod (Path B)**: soldered HOME-button tap. Pro Controller keeps full gameplay functionality, ESP32 sees HOME via GPIO instead of RF.

## Bill of materials (~$10)

- **Seeed XIAO ESP32-C3** (21 × 17.5 × 3.2 mm) — ~$5
- 3× thin enameled magnet wire (30-34 AWG), ~30 cm each
- Kapton tape or heat-shrink for insulation
- Small amount of hot glue or silicone for strain relief
- Soldering iron with a fine tip (0.5-1mm)
- Existing Switch 1 Pro Controller

## Circuit overview

```
Pro Controller internals:
                                      ┌─────────────────┐
  [3.7V Li-ion battery] ───+──────────┤ XIAO VBAT/5V    │
                           │          │                 │
                           └── 3.7V ──┤ (internal LDO   │
                                      │   → 3.3V)       │
  [Pro Controller GND] ────────────┬──┤ GND             │
                                   │  │                 │
  [HOME button pad]                │  │                 │
    ├── to Pro Controller MCU      │  │                 │
    └────────── tap wire ──────────┼──┤ GPIO D0         │
                                   │  │                 │
                                   │  │ (deep sleep     │
                                   │  │  wake on GPIO)  │
                                   │  └─────────────────┘
```

The HOME button is a momentary SPST switch. One side is grounded, the other pulls low when pressed. We tap the non-ground side; XIAO's internal pull-up detects the press on GPIO D0.

## Power budget

With XIAO configured for deep sleep + GPIO wake:

| State | Current | Notes |
|---|---|---|
| Deep sleep (99%+ of the time) | ~43 µA | datasheet verified |
| Active BLE advertising | ~40 mA | only during 2-second wake burst |

Pro Controller battery: 1300 mAh. Passive drain from ESP32: `1300 / 0.043 = 30,000 hours ≈ 3.4 years` of pure standby.

**Critical**: the XIAO has a red power LED that draws ~1 mA. It must be **desoldered** before installing, otherwise standby drops to ~55 days per charge. See Seeed's removal guide: the LED is a 0603 package next to the USB-C connector.

## Step-by-step

### 1. Prepare the XIAO

1. Desolder the power LED.
2. Flash the firmware (see [firmware section](#firmware) below).
3. Verify deep-sleep current with a µA meter (should be ~43 µA). If much higher, check LED removal and firmware deep-sleep config.

### 2. Open the Pro Controller

Follow the [iFixit Pro Controller teardown](https://www.ifixit.com/Guide/Nintendo+Switch+Pro+Controller+Battery+Replacement/127714) up to the point where the battery is exposed. Stop before removing the main PCB.

### 3. Identify tap points

- **Battery +**: the red wire from the battery connector to the PCB. Measure with a multimeter — should read ~4.2V when fully charged.
- **Battery −**: the black wire.
- **HOME button non-ground side**: probe both terminals of the HOME tactile switch with the PCB powered. The side that reads ~3.3V in idle and drops to 0V when pressed is the one you want.

### 4. Solder three wires

- VBAT+ → XIAO `VBAT`/`5V` pad
- GND → XIAO `GND`
- HOME tap → XIAO `D0` (GPIO 2 on C3)

Use magnet wire (thin, flexible), not jumper cables. Run them along existing wire channels. Insulate with Kapton tape.

### 5. Fit the XIAO

The XIAO fits cleanly behind the battery or in the empty cavity near one of the grip shells. Avoid:
- Covering any screw holes
- Blocking the rumble motors
- Pinching wires when the shell closes

Secure with hot glue on the XIAO's bottom (not the antenna side).

### 6. Reassemble and test

Close the controller. Turn it on. Press HOME. Your Switch 2 should wake.

## Firmware

Minimal variant of the existing `switch2_wake.c` with deep sleep wake-on-GPIO:

```c
// Additional to the existing firmware:

#include "esp_sleep.h"

// In app_main, AFTER the wake burst completes:
esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 0);  // wake when HOME pulls low
esp_deep_sleep_start();

// The chip resets on wake, so just re-entering app_main on every HOME press.
// No main loop needed.
```

Build for the **Seeed XIAO ESP32-C3** target:

```ini
[env:xiao_esp32c3]
platform = espressif32
board = seeed_xiao_esp32c3
framework = espidf
```

And use `TRIGGER_GPIO 2` (D0 on XIAO C3).

No BOOT button. No LED signaling. No NVS config — bake MACs in at compile time from `secrets.h` (already supported). Keep firmware minimal to preserve flash space.

## Risks

- **Soldering to a tactile switch pad is risky.** If you lift the pad, you've bricked the HOME button on the controller. Take your time. Pre-tin the wire, use just enough heat.
- **Shorts.** Triple-check wire insulation before closing the shell. A short across the battery can damage the Li-ion cell and/or catch fire.
- **Warranty.** Opening the controller voids the Nintendo warranty, obviously.
- **Pairing conflicts.** The ESP32 will transmit BLE advertisements as the Joy-Con source MAC. If your actual Joy-Con is also broadcasting, both advertising sources could confuse nearby scanners. Mitigation: use a source MAC that's NOT one of your real Joy-Cons — any bonded MAC should work (we haven't verified whether bonding is actually required; see `docs/research-notes.md` open questions).

## Alternative without opening the controller

If you'd rather not mod the controller, these work without any hardware changes:

1. **Dongle on Switch 2's USB port** with a physical button (already working — see `esp32/src/switch2_wake.c`)
2. **Wi-Fi + phone shortcut** (planned, not yet built — `docs/status-and-next-steps.md` → "Future work")
3. **Cheap BLE controller** paired to ESP32 as wake remote (any button wakes the Switch — planned)

## Open questions

1. **Does the HOME switch on the Pro Controller have debouncing already?** If so, GPIO polling with minimal debounce in our firmware is fine. If not, add 20ms software debounce.
2. **Can we hitch onto the rumble motor's power line to avoid a separate battery tap?** Would save one wire but may introduce noise during rumble events. Probably not worth it.
3. **Is there a way to also detect CAPTURE button as a secondary trigger?** Not useful for wake, but could fire a "take screenshot on the ESP32's status OLED" or similar extension.
