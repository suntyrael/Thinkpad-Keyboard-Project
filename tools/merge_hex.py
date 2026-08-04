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
            chunk = min(rec_len, e - pos)
            payload = bytes(data[p] for p in range(pos, pos + chunk))
            emit(pos - cur_base, payload)
            pos += chunk
    lines.append(':00000001FF')
    with open(out_path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f"Wrote: {out_path} ({len(lines)} records)")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    out_path = sys.argv[1]
    inputs = sys.argv[2:]

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

    write_hex(merged, out_path)
    print(f"  = total {len(merged)} bytes (0x{min(merged):08X}-0x{max(merged):08X})")


if __name__ == '__main__':
    main()
