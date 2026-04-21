# Research Notes

Chronological log of investigative steps, what they revealed, and why they mattered. Useful context for anyone extending this work.

## Starting point

The Switch 2 does not let original Switch 1 Pro Controllers wake it from sleep. Third-party controllers (GuliKit KK3 Max, 8BitDo Ultimate 2 BT, Mobapad Chitu 2) shipped firmware updates adding this capability. The underlying protocol was undocumented by Nintendo.

BlueRetro (v25.10-beta) had reverse-engineered Switch 2 controller-to-console communication, including a 4-step pairing handshake with two hardcoded 16-byte challenge blobs. Initial hypothesis: wake requires this full handshake plus a final HID report with the HOME button pressed.

## Step 1: Research how third-party vendors achieved wake

**Key finding**: every vendor's user-facing pairing instructions include a mandatory "remove and reattach a Joy-Con" ritual. GuliKit themselves stated the KK3 Max's BT chip is limited and can't pair "as conveniently as other new controllers like ES." Hypothesis at that point: third parties don't reproduce Nintendo's crypto handshake — they exploit the Joy-Con reattach flow to piggyback on an already-bonded Joy-Con's wake eligibility.

This turned out to be partially correct (the Joy-Con ritual IS the bond step), but the underlying wake mechanism was simpler than we thought.

## Step 2: Download and byte-diff GuliKit BT firmware

See [`firmware-analysis.md`](../research/firmware-analysis.md) for details.

Result: `.text` identical, all diff in `.data` `0x171000`–`0x1ca000`, blob is AES-encrypted by the BT chip's bootloader. Dead end for static analysis.

## Step 3: Hunt for unencrypted third-party firmware

Research confirmed **no commercial Switch 2 wake controller ships plaintext firmware**. Measured 8BitDo `.dat` at 7.998 bits/byte entropy (same as GuliKit). This is a BT SoC industry standard — hardware-enforced AES-CCM code authentication.

## Step 4: Search for community RF/BLE captures

The breakthrough. Two independent projects had already done the work:

1. **tv/switch2-wake-up** — used an nRF52840 BLE sniffer to capture what their Joy-Cons transmit when pressing HOME to wake. Published the captured 31-byte payload and a working ESPHome / Flipper Zero replay implementation.

2. **Minkelxy/xiaoai_switch2_wake_up** — independent implementation, same 31-byte structure, annotated with comments explaining each field.

3. **HelloRayH (in ndeadly/MissionControl#199)** — confirmed via SDR and an old 2.4 GHz RF chip that wake happens on BLE advertising channels 37/38/39, that "the wake-up code is only related to the host" (Switch 2's BT address), and ran into trouble with BLE access-code sync-word generation while attempting a from-scratch implementation.

**All three converge on the same model**: wake is a BLE non-connectable advertising beacon containing Nintendo VID/PID + the target Switch's own BT address. No pairing, no encryption, no GATT.

## Why this wasn't in BlueRetro

BlueRetro impersonates the **Switch 2 console** and receives input from a real Pro Controller 2 / Joy-Con. The 4-step pairing handshake and challenge blobs that BlueRetro documents are part of the post-wake **controller-to-console** communication once both are awake. They are not involved in wake.

Wake is a separate mechanism — the BT chip stays in a low-power scan mode during sleep, watching advertising channels for a specific pattern, and wakes the host CPU when it sees that pattern. No decryption is needed to recognize the pattern because the pattern IS the authentication (it contains the console's own BT address, which an attacker wouldn't know without first getting physical proximity to sniff it).

## Open questions

1. **Exactly which fields of the beacon are mandatory?** Both working implementations copy the full 31 bytes, but we haven't tested which bytes can be varied. Candidates to fuzz: the `01 00 03` header, `00 01 81` middle constant, the `0F 00 ...` trailing pad, the PID (is 0x2066 required, or would 0x2069/0x2067 also work?).

2. **Does the source MAC really need to be bonded, or just "plausibly-Nintendo-VID-shaped"?** Worth testing an unbonded random MAC — if it works, wake has zero authentication beyond knowing the target's BT address.

3. **What's the Switch 2's wake-scan duty cycle?** The 1–2 second burst duration from tv's implementation suggests the Switch's BT chip only samples every few hundred ms. A shorter burst might also work.

4. **Is the `0x0553` company ID meaningful?** It doesn't match Nintendo's Bluetooth SIG assignment (Nintendo uses 0x0553 and 0x0054 — actually `0x0553` IS assigned to Nintendo in the Bluetooth SIG CIC database). So this is Nintendo's BT SIG manufacturer ID, different from their USB VID 0x057E.

## If someone extends this

High-leverage next experiments:
- Test with a random/unbonded source MAC to confirm whether bonding matters
- Fuzz individual bytes of the payload to find the minimum sufficient pattern
- Measure the Switch 2's wake-scan duty cycle (bisect on burst duration)
- Test whether other PIDs (0x2069 Pro Controller 2, 0x2067 Joy-Con R) also work in field F
