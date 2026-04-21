#!/usr/bin/env python3
"""
Dump SPI flash from a Switch Pro Controller over USB HID.

Protocol per dekuNukem/Nintendo_Switch_Reverse_Engineering:
  USB-HID-Notes.md           — USB handshake (0x80 0x01..0x05)
  bluetooth_hid_subcommands_notes.md — subcommand 0x10 (SPI Flash Read)

Read-only: only sends 0x10 (read) subcommands. Never writes SPI.
Output: pro_spi_dump.bin (524288 bytes typically, padded with 0xFF on errors)
"""

import sys
import time
import hid

VID = 0x057E
PID = 0x2009  # Switch 1 Pro Controller

# USB HID Output Report 0x80 commands per dekuNukem
USB_CMD_REQUEST_INFO     = 0x01
USB_CMD_HANDSHAKE        = 0x02
USB_CMD_BAUD_3M          = 0x03  # switch to 3 Mbit/s baud
USB_CMD_DISABLE_TIMEOUT  = 0x04  # disable USB timeout (so controller doesn't drop)
USB_CMD_ENABLE_TIMEOUT   = 0x05

# HID Output Report 0x01 = subcommand+rumble; subcommand 0x10 = SPI Flash Read
SUBCMD_SPI_READ = 0x10

SPI_FLASH_SIZE = 0x80000   # 524288 bytes (4 Mbit, MX25U4033E)
SPI_READ_CHUNK = 0x1D      # max bytes per read response (29)


def hex_b(b):
    return ' '.join(f'{x:02X}' for x in b)


def open_device():
    devs = [d for d in hid.enumerate(VID, PID) if d.get('interface_number', 0) == 0]
    # Prefer the gamepad-usage interface (usage_page 0x01, usage 0x05) if multiple
    if not devs:
        raise SystemExit("No Pro Controller found on USB. Plug in via USB-C cable.")
    # On macOS hidapi, both 'interfaces' return the same physical device.
    # Use the path of the first one.
    path = devs[0]['path']
    h = hid.device()
    h.open_path(path)
    h.set_nonblocking(False)
    return h


def send_usb_cmd(h, cmd):
    """USB output report 0x80 with command byte."""
    pkt = [0x80, cmd] + [0x00] * 30
    h.write(bytes(pkt))


def send_subcmd(h, subcmd, data=b'', packet_num=[0]):
    """HID output report 0x01: rumble (neutral) + subcommand."""
    # Output report 0x01 layout (USB):
    #   [0x80, 0x92, 0x00, 0x31, 0x00, 0x00, 0x00, 0x00,
    #    rumble_l(4), rumble_r(4), subcmd, ...subcmd_data...]
    # Per dekuNukem USB-HID-Notes: USB output reports must be wrapped
    # in 0x80 0x92 ... pre-handshake; post-handshake we use plain 0x01.
    rumble_neutral = [0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40]
    pkt = [0x01, packet_num[0] & 0xF] + rumble_neutral + [subcmd] + list(data)
    pkt += [0x00] * (49 - len(pkt))  # pad to 49 bytes
    packet_num[0] = (packet_num[0] + 1) & 0xF
    h.write(bytes(pkt))


def read_response(h, timeout_ms=200, want_subcmd=None):
    """Read until we get a response (report 0x21) for our subcmd."""
    deadline = time.time() + timeout_ms / 1000
    while time.time() < deadline:
        data = h.read(64, timeout_ms=int((deadline - time.time()) * 1000))
        if not data:
            continue
        # Standard input reports:
        #   0x81 = USB ACK
        #   0x21 = subcommand reply
        #   0x30 = standard input report (we'll ignore these)
        if data[0] == 0x81:
            return ('usb_ack', bytes(data))
        if data[0] == 0x21:
            # data[14] = subcommand ack/id
            if want_subcmd is None or (len(data) > 14 and data[14] == want_subcmd):
                return ('subcmd_reply', bytes(data))
        # else: input report 0x30 etc., skip
    return (None, None)


def usb_handshake(h):
    """Perform the USB-mode handshake per dekuNukem USB-HID-Notes."""
    print("[*] USB handshake: request info...", flush=True)
    send_usb_cmd(h, USB_CMD_REQUEST_INFO)
    kind, resp = read_response(h)
    if resp:
        print(f"    USB info reply ({kind}): {hex_b(resp[:32])}")
        # MAC is at resp[4:10] in some firmwares
        if len(resp) >= 10 and resp[0] == 0x81 and resp[1] == 0x01:
            mac_le = resp[4:10]
            mac_be = bytes(reversed(mac_le))
            print(f"    Controller MAC (BE): {':'.join(f'{b:02X}' for b in mac_be)}")
    else:
        print("    No USB info reply (continuing anyway)")

    print("[*] USB handshake: handshake...", flush=True)
    send_usb_cmd(h, USB_CMD_HANDSHAKE)
    kind, resp = read_response(h)
    print(f"    Handshake reply: {hex_b(resp[:8]) if resp else 'none'}")

    print("[*] USB handshake: disable timeout...", flush=True)
    send_usb_cmd(h, USB_CMD_DISABLE_TIMEOUT)
    # No reply expected for 0x04


def spi_read(h, addr, length=SPI_READ_CHUNK):
    """Send subcommand 0x10 with 4-byte LE addr + 1-byte length. Returns data bytes."""
    addr_bytes = addr.to_bytes(4, 'little')
    send_subcmd(h, SUBCMD_SPI_READ, addr_bytes + bytes([length]))
    kind, resp = read_response(h, timeout_ms=500, want_subcmd=SUBCMD_SPI_READ)
    if resp is None:
        return None
    # Layout: resp[0]=0x21, resp[14]=0x10 (subcmd ack), resp[15..18]=echoed addr,
    # resp[19]=length, resp[20..20+length]=data
    if len(resp) < 20 + length:
        return None
    return resp[20:20 + length]


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else 'pro_spi_dump.bin'

    print(f"[*] Opening Pro Controller (VID 0x{VID:04X}, PID 0x{PID:04X})...")
    h = open_device()
    try:
        usb_handshake(h)

        print(f"\n[*] Dumping SPI flash to {out_path}")
        print(f"    Size: {SPI_FLASH_SIZE} bytes ({SPI_FLASH_SIZE // 1024} KB)")
        print(f"    Chunks: {SPI_FLASH_SIZE // SPI_READ_CHUNK} reads of {SPI_READ_CHUNK} bytes\n")

        out = bytearray(SPI_FLASH_SIZE)
        # Mark all-FF initially (matches erased-flash state for any failed reads)
        for i in range(SPI_FLASH_SIZE):
            out[i] = 0xFF

        addr = 0
        retries = 0
        last_print = time.time()
        start = time.time()

        while addr < SPI_FLASH_SIZE:
            length = min(SPI_READ_CHUNK, SPI_FLASH_SIZE - addr)
            data = spi_read(h, addr, length)
            if data is None:
                retries += 1
                if retries > 5:
                    print(f"\n[!] Read failed at 0x{addr:06X} after 5 retries; skipping")
                    addr += length
                    retries = 0
                continue
            retries = 0
            out[addr:addr + len(data)] = data
            addr += len(data)

            # Progress every 0.5s
            if time.time() - last_print > 0.5:
                pct = 100 * addr / SPI_FLASH_SIZE
                rate = addr / (time.time() - start)
                eta = (SPI_FLASH_SIZE - addr) / rate if rate else 0
                print(f"\r    0x{addr:06X} / 0x{SPI_FLASH_SIZE:06X}  ({pct:5.1f}%)  "
                      f"{rate/1024:.1f} KB/s  ETA {eta:.0f}s", end='', flush=True)
                last_print = time.time()

        print(f"\n[*] Done in {time.time() - start:.1f}s. Writing {out_path}...")
        with open(out_path, 'wb') as f:
            f.write(out)
        print(f"[*] Wrote {len(out)} bytes to {out_path}")

    finally:
        h.close()


if __name__ == '__main__':
    main()
