#!/usr/bin/env python3
"""Convert an Intel HEX file into an Adafruit UF2 image for the nRF52840.

The Adafruit nRF52 bootloader uses ABSOLUTE flash addresses inside UF2
blocks (unlike RP2040 relative addresses), so the app hex (which links at
0x26000 for the ZMK/adafruit-bootloader layout) converts directly.

Usage:
    python3 make_uf2.py <input.hex> <output.uf2>
"""

import struct
import sys

UF2_MAGIC0 = 0x0A324655  # "UF2\n"
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
FAMILY_NRF52840 = 0xADA52840
FLAG_FAMILYID = 0x00002000  # familyID field present
PAYLOAD = 476
BLOCK = 512


def parse_hex(path):
    data = {}
    base = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] != ":":
                continue
            count = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            typ = int(line[7:9], 16)
            payload = bytes.fromhex(line[9:9 + count * 2])
            if typ == 0:
                a = base + addr
                for i, b in enumerate(payload):
                    data[a + i] = b
            elif typ == 4:
                base = int.from_bytes(payload, "big") << 16
            elif typ == 2:
                base = int.from_bytes(payload, "big") << 4
            elif typ == 1:
                break
    return data


def make_uf2(data, out_path):
    addrs = sorted(data)
    start = addrs[0]
    end = addrs[-1]
    size = end - start + 1
    print(f"  app region 0x{start:08X}-0x{end:08X} size=0x{size:X}")

    # Pad the last block; everything else is contiguous bytes (holes filled
    # with 0xFF - the bootloader programs the region as-is).
    raw = bytearray(0xFF for _ in range(size))
    for a, b in data.items():
        raw[a - start] = b

    nblocks = (size + PAYLOAD - 1) // PAYLOAD
    out = []
    for i in range(nblocks):
        off = i * PAYLOAD
        chunk = raw[off:off + PAYLOAD]
        chunk += bytes(0xFF for _ in range(PAYLOAD - len(chunk)))
        hdr = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC0,
            UF2_MAGIC1,
            FLAG_FAMILYID,
            start + off,
            len(chunk),
            i,
            nblocks,
            FAMILY_NRF52840,
        )
        out.append(hdr + chunk + struct.pack("<I", UF2_MAGIC_END))
    with open(out_path, "wb") as f:
        f.write(b"".join(out))
    print(f"  wrote {out_path}: {nblocks} blocks, first block addr "
          f"0x{start:08X}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    make_uf2(parse_hex(sys.argv[1]), sys.argv[2])