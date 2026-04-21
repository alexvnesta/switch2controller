# BlueRetro fork plan: Switch 1 Pro Controller → Switch 2 wake relay

## Goal

Press HOME on a Switch 1 Pro Controller (classic Bluetooth HID) → ESP32 running forked BlueRetro transmits the Switch 2 BLE wake beacon → Switch 2 wakes.

## Why BlueRetro is the right base

BlueRetro already solves the hardest part: pairing with and receiving input from a Switch 1 Pro Controller over classic Bluetooth. Ground-up implementation on ESP-IDF would take weeks; the BlueRetro fork is a localized patch to a working codebase.

**Key bits already present:**

| Capability | File | Status |
|---|---|---|
| Classic BT pairing with Pro Controller | `main/bluetooth/hidp/sw.c` | ✓ works (`bt_hid_sw_init`, 470 lines) |
| Parse Switch 1 HID reports (0x30 native, 0x3F basic) | `main/adapter/wireless/sw.c:504` (`sw_to_generic`) | ✓ works |
| HOME button detection | `main/adapter/wireless/sw.c:31` (`SW_PRO_HOME`) + `BIT(SW_PRO_HOME)` | ✓ works |
| `LE_SET_ADV_PARAM` HCI command | `main/bluetooth/hci.c:803` | ✓ works (currently used for BlueRetro's own advertising) |
| `LE_SET_ADV_DATA` HCI command | `main/bluetooth/hci.c:818` | ✓ works (generic data buffer) |
| `LE_SET_ADV_ENABLE` / `_DISABLE` | `main/bluetooth/hci.c:851`, `:860` | ✓ works |
| All-3-channel advertising (CH37/38/39) | `hci.c:812` `channel_map = 0x07` | ✓ correct default |
| Local MAC settable | `main/bluetooth/hci.c:64` `local_bdaddr[6]` | ✓ already tracked |

The entire classic-BT HID-host protocol (subcommand 0x02 device info, 0x10 SPI reads for calibration, rumble, LEDs, etc.) is implemented and tested.

## Patch outline

### Step 1: Add a wake-beacon transmit path in `hci.c`

New HCI command sequence function `bt_hci_send_sw2_wake_beacon(const uint8_t *target_mac)`:

```c
// New: builds the 31-byte Switch 2 wake payload and issues adv params/data/enable
static void bt_hci_cmd_sw2_wake_set_adv_data(const uint8_t target_mac[6]) {
    uint8_t adv_data[31] = {
        0x02, 0x01, 0x06,                                   // Flags
        0x1B, 0xFF,                                          // Mfg data, 27 bytes
        0x53, 0x05,                                          // Company ID 0x0553
        0x01, 0x00, 0x03,                                    // unknown header
        0x7E, 0x05,                                          // Nintendo VID 0x057E
        0x66, 0x20,                                          // Joy-Con L PID 0x2066
        0x00, 0x01, 0x81,                                    // unknown middle
        // Target Switch 2 BT address, reversed:
        target_mac[5], target_mac[4], target_mac[3],
        target_mac[2], target_mac[1], target_mac[0],
        0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // pad
    };
    struct bt_hci_cp_le_set_adv_data *cp = ...;
    cp->len = sizeof(adv_data);
    memcpy(cp->data, adv_data, sizeof(adv_data));
    bt_hci_cmd(BT_HCI_OP_LE_SET_ADV_DATA, sizeof(*cp));
}
```

Then orchestrate: `LE_SET_ADV_PARAM` (non-connectable, 50 ms interval, ch_map 0x07) → our new `_set_adv_data` → `LE_SET_ADV_ENABLE` → 2-second timer → `LE_SET_ADV_DISABLE`.

### Step 2: Detect HOME in Switch 1 input path

In `main/adapter/wireless/sw.c`, `sw_to_generic` (line 504) already receives every input report. Add a HOME edge-detect and call the new beacon trigger:

```c
static uint32_t last_sw_btns = 0;

int32_t sw_to_generic(struct bt_data *bt_data, struct wireless_ctrl *ctrl_data) {
    // ... existing code ...
    struct sw_native_map *map = (struct sw_native_map *)bt_data->base.input;
    uint32_t home_bit = BIT(SW_PRO_HOME);
    if ((map->buttons & home_bit) && !(last_sw_btns & home_bit)) {
        bt_hci_trigger_sw2_wake();  // new: fires beacon burst
    }
    last_sw_btns = map->buttons;
    // ... existing return path ...
}
```

### Step 3: Store the Switch 2 target MAC persistently

BlueRetro already uses NVS (ESP32 non-volatile storage) for config. Add a new key `"sw2_wake_mac"` holding 6 bytes. Expose it via BlueRetro's existing BLE configuration service (`main/bluetooth/att_cfg.c`) so it can be written from the companion web UI without reflashing.

### Step 4: Set source MAC to a bonded address

`LE_SET_ADV_PARAM` uses `own_addr_type = 0x00` (public). The public address is whatever the BT controller has set via `HCI_Write_BD_ADDR`. BlueRetro already reads/writes `local_bdaddr[6]` (hci.c:64). Add a startup path to set this to a stored "bonded MAC" value before the first adv cycle, or use random addressing for simpler bringup.

**Open question**: does the wake actually require a bonded source MAC, or does it work from any MAC as long as the target-MAC-in-payload is right? Worth testing *without* setting a specific source MAC first.

## Scope estimate

| Task | Lines changed | Risk |
|---|---|---|
| New `bt_hci_send_sw2_wake_beacon` in hci.c | ~60 | Low — reuses existing command paths |
| HOME edge-detect in sw.c | ~10 | Low |
| NVS config for target MAC | ~40 | Low — NVS helpers exist |
| Source MAC override | ~20 | Medium — interacts with existing bt_hci init flow |
| Total | **~130 lines** | Mostly additive |

Single weekend for a working prototype, assuming:
- ESP32 dev board with BlueRetro flashing already working (BlueRetro's [wiki flashing guide](https://github.com/darthcloud/BlueRetro/wiki))
- Your Switch 2's BT address known
- Switch 1 Pro Controller in hand

## Risks / unknowns

1. **BlueRetro's BLE advertising might conflict with its classic-BT pairing.** BlueRetro uses BLE for its own configuration web UI (via `att_cfg.c`). Transmitting a different advertising payload on demand requires pausing the config service advertising, firing our payload, and restoring. Possibly the easier approach is to build a stripped-down version that disables BlueRetro's native advertising and only uses adv for the wake burst.

2. **Source MAC requirement unverified.** If the Switch 2 really only accepts wake beacons from bonded MACs, we need a way to learn what MAC is bonded. Simplest path: pair any BLE controller with the Switch 2 once, note its MAC, set that as the ESP32's source MAC manually.

3. **ESP32 classic-BT and BLE coexistence during burst.** ESP32 Bluedroid supports dual-mode, but transmit scheduling when both stacks are active can be quirky. The Pro Controller link must stay connected during the burst so HOME-release detection continues to work.

4. **Power state of the Pro Controller.** The Pro Controller will try to pair to the ESP32 every boot. Must persist link keys so pairing happens once, not every time.

## Simpler alternative to consider first

If the goal is "wake the Switch 2 by pressing a button that feels like my controller," the 130-line fork is justified. If it's "wake the Switch 2 easily," a standalone ESP32 dongle on a keychain (no fork, ~50 lines total) delivers the same result with none of the classic-BT pairing risk.

**Recommendation**: prototype the dongle first (validates the BLE wake packet works against your specific Switch 2), then tackle the BlueRetro fork once wake is confirmed working in isolation.
