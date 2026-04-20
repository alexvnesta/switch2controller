# switch2_wake — ESP32 BLE wake dongle

Minimal PlatformIO + ESP-IDF (NimBLE) project that sends the Switch 2 wake beacon. On boot it auto-fires a 2-second burst after a 3-second delay. Pressing the BOOT button (GPIO0) sends another burst on demand.

Default target: ESP32-WROOM-32 classic (board `esp32dev`).

## Setup in VS Code

1. Install the **PlatformIO IDE** extension in VS Code
2. Open this `esp32/` folder (`File → Open Folder…`)
3. PlatformIO will auto-detect `platformio.ini` and download the toolchain on first build (~5 min, one time)

## Before flashing

Edit `src/switch2_wake.c` and set your Switch 2's BT MAC:

```c
static const uint8_t TARGET_SWITCH_MAC[6] = {
    0x98, 0xE2, 0x55, 0x99, 0x1F, 0x97,  // ← your Switch 2's BT address, in reading order
};
```

Optional: `SOURCE_MAC` — if left all-zero, NimBLE uses the chip's factory public address. If wake doesn't work, set it to a MAC already bonded to that Switch 2.

## How to find your Switch 2's BT address

- Run a BLE scanner app on your phone (nRF Connect for Android/iOS, LightBlue on iOS) while the Switch 2 is on — it'll show up in the scan
- Or: pair your Switch 2 with something that exposes paired-device MACs (Android Bluetooth settings does)

## Build & flash

From the `esp32/` directory:

```bash
# Using the PlatformIO VS Code extension: click the ✓ (Build) then → (Upload) buttons

# Or from the command line:
~/.platformio/penv/bin/pio run                    # build
~/.platformio/penv/bin/pio run -t upload          # flash
~/.platformio/penv/bin/pio device monitor         # serial monitor
```

The serial port is pre-configured to `/dev/cu.usbserial-0001` in `platformio.ini`. Change it if yours differs.

First boot auto-fires a burst. Hold the dongle <1 m from your sleeping Switch 2. If wake works, the screen comes on and the serial log prints `burst complete`.

## If it doesn't wake

Verify in order:

1. **TARGET_SWITCH_MAC** — confirm it's your actual Switch 2's BT address
2. Try setting `SOURCE_MAC` to a real bonded controller's MAC
3. **Check serial log** — should show `burst started, 2000 ms` then `burst complete`
4. **Sniff what you transmit** — another BLE scanner next to the dongle should see a 31-byte manufacturer-data advert with `7E 05 66 20` inside
5. **Distance** — Switch 2 BT RX sensitivity during sleep may be reduced; keep close

## Files

```
esp32/
├── platformio.ini              Build/upload config (port, framework)
├── sdkconfig.defaults          NimBLE broadcaster-only, Bluedroid disabled
├── src/
│   └── switch2_wake.c          All the logic (~140 lines)
└── README.md
```
