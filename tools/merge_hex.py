#!/usr/bin/env python3
"""
Merge multiple Intel HEX files into a single image (non-overlapping addresses,
including UICR region if present).

Usage:
    python3 merge_hex.py <output.hex> <input1.hex> [<input2.hex> ...]
"""

import sys


def parse_hex(path):
    data = {}
    base = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] != ':':
                continue
            count = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            typ = int(line[7:9], 16)
            payload = bytes.fromhex(line[9:9 + count * 2])
            if typ == 0:  # data
                a = base + addr
                for i, b in enumerate(payload):
                    data[a + i] = b
            elif typ == 4:  # extended linear address
                base = int.from_bytes(payload, 'big') << 16
            elif typ == 2:  # extended segment address
                base = int.from_bytes(payload, 'big') << 4
            elif typ == 1:  # EOF
                break
    return data


def write_hex(data, out_path, rec_len=32):
    lines = []
    addrs = sorted(data)
    segments = []
    for a in addrs:
        if segments and a == segments[-1][1]:
            segments[-1][1] += 1
        else:
            segments.append([a, a + 1])

    def emit(offset, payload):
        n = len(payload)
        body = bytes([n, (offset >> 8) & 0xFF, offset & 0xFF, 0]) + payload
        ck = (-sum(body)) & 0xFF
        lines.append(':' + body.hex().upper() + f'{ck:02X}')

    def emit_base(b):
        hi = (b >> 16) & 0xFFFF
        body = bytes([2, 0, 0, 4, (hi >> 8) & 0xFF, hi & 0xFF])
        ck = (-sum(body)) & 0xFF
        lines.append(':' + body.hex().upper() + f'{ck:02X}')

    cur_base = None
    for s, e in segments:
        pos = s
        while pos < e:
            b = (pos >> 16) << 16  # 64KB-aligned base for this position
            if cur_base != b:
                emit_base(b)
                cur_base = b
            next_boundary = cur_base + 0x10000
            chunk = min(rec_len, e - pos, next_boundary - pos)
            payload = bytes(data[p] for p in range(pos, pos + chunk))
            emit(pos - cur_base, payload)
            pos += chunk
    lines.append(':00000001FF')
    with open(out_path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f"Wrote: {out_path} ({len(lines)} records)")




ADAFRUIT_SETTINGS_ADDR = 0xFF000  # Adafruit bootloader settings page


def mark_app_valid(merged, app_path):
    """Write BANK_VALID_APP marker into the Adafruit bootloader settings page.

    When flashing a full image with J-Link directly (no UF2 flow), the
    bootloader settings page stays erased (0xFFFF) and bootloader_app_is_valid()
    may reject the app. UF2 flashing normally writes this marker after a
    successful update. Structure (bootloader_types.h):
      0xFF000 bank_0     (uint16) = 0x0001 (BANK_VALID_APP)
      0xFF002 bank_0_crc (uint16) = 0x0000 (0 = CRC check disabled)
      0xFF004 bank_1     (uint16) stays erased (0xFFFF = BANK_INVALID_APP)
      0xFF008 bank_0_size(uint32) = application size
    """
    app_data = parse_hex(app_path)
    # Use the address span (max-min+1) rather than the number of dictionary
    # entries: if the hex has address holes, len() underestimates the app
    # size that the bootloader uses for its bank validity/CRC checks
    # (review 2026-08-13 item 3.4).
    app_size = max(app_data) - min(app_data) + 1
    merged[ADAFRUIT_SETTINGS_ADDR + 0x0] = 0x01  # bank_0 low  (BANK_VALID_APP)
    merged[ADAFRUIT_SETTINGS_ADDR + 0x1] = 0x00
    merged[ADAFRUIT_SETTINGS_ADDR + 0x2] = 0x00  # bank_0_crc low
    merged[ADAFRUIT_SETTINGS_ADDR + 0x3] = 0x00
    # bank_1 (0xFF004-0xFF005) left erased on purpose
    merged[ADAFRUIT_SETTINGS_ADDR + 0x8] = app_size & 0xFF
    merged[ADAFRUIT_SETTINGS_ADDR + 0x9] = (app_size >> 8) & 0xFF
    merged[ADAFRUIT_SETTINGS_ADDR + 0xA] = (app_size >> 16) & 0xFF
    merged[ADAFRUIT_SETTINGS_ADDR + 0xB] = (app_size >> 24) & 0xFF
    print(f"  [settings] bank_0=BANK_VALID_APP(0x0001) crc=0 "
          f"app_size=0x{app_size:X} @ 0x{ADAFRUIT_SETTINGS_ADDR:X}")



def main():
    args = sys.argv[1:]
    mark_valid = False
    if '--mark-app-valid' in args:
        mark_valid = True
        args.remove('--mark-app-valid')
    if len(args) < 3:
        print(__doc__)
        sys.exit(1)
    out_path = args[0]
    inputs = args[1:]

    merged = {}
    for path in inputs:
        data = parse_hex(path)
        overlap = set(merged) & set(data)
        if overlap:
            print(f"ERROR: address overlap with {path}:",
                  [hex(x) for x in sorted(overlap)[:10]])
            sys.exit(1)
        merged.update(data)
        print(f"  + {path}: {len(data)} bytes (0x{min(data):08X}-0x{max(data):08X})")

    if mark_valid:
        mark_app_valid(merged, inputs[-1])

    write_hex(merged, out_path)
    print(f"  = total {len(merged)} bytes (0x{min(merged):08X}-0x{max(merged):08X})")


if __name__ == '__main__':
    main()
