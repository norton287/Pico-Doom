#!/usr/bin/env python3
#
# Copyright (c) 2024 RP2040 Doom contributors
# SPDX-License-Identifier: BSD-3-Clause
#
"""Append a raw binary (e.g. doom1.whx) at a flash address to a program UF2.

The result is a single UF2 you can drag onto the RPI-RP2 / RP2350 BOOTSEL drive to
install both the Doom binary and its WAD data in one go.

    tools/uf2_add_data.py doom_tiny.uf2 doom1.whx 0x10040000 doom_tiny_doom1.uf2
"""
import struct
import sys

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
BLOCK = 512
PAYLOAD = 256
HEADER = struct.Struct("<8I")


def read_blocks(path):
    data = open(path, "rb").read()
    if len(data) % BLOCK:
        sys.exit(f"{path}: not a UF2 file (size is not a multiple of 512)")
    blocks = []
    for off in range(0, len(data), BLOCK):
        m0, m1, flags, addr, size, blockno, numblocks, family = HEADER.unpack_from(data, off)
        end, = struct.unpack_from("<I", data, off + BLOCK - 4)
        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1 or end != UF2_MAGIC_END:
            sys.exit(f"{path}: bad UF2 block at offset {off}")
        blocks.append(dict(flags=flags, addr=addr, size=size, family=family,
                           payload=data[off + 32: off + 32 + 476], raw=data[off: off + BLOCK]))
    return blocks


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    prog_path, bin_path, addr_s, out_path = sys.argv[1:]
    base = int(addr_s, 0)
    if base % PAYLOAD:
        sys.exit("address must be 256 byte aligned")
    blocks = read_blocks(prog_path)

    # the program family is the one with the most blocks (RP2350 UF2s also carry a one block
    # "absolute" family group as an erratum E10 workaround, which we leave untouched)
    counts = {}
    for b in blocks:
        counts[b["family"]] = counts.get(b["family"], 0) + 1
    family = max(counts, key=counts.get)
    prog_end = max(b["addr"] + b["size"] for b in blocks if b["family"] == family)
    if prog_end > base:
        sys.exit(f"program ends at {prog_end:#x}, which overlaps the data at {base:#x}")

    blob = open(bin_path, "rb").read()
    for off in range(0, len(blob), PAYLOAD):
        chunk = blob[off: off + PAYLOAD].ljust(PAYLOAD, b"\0")
        blocks.append(dict(flags=UF2_FLAG_FAMILY_ID, addr=base + off, size=PAYLOAD, family=family,
                           payload=chunk.ljust(476, b"\0")))

    main_blocks = [b for b in blocks if b["family"] == family]
    out = bytearray()
    groups = {}
    for b in blocks:
        groups.setdefault(b["family"], []).append(b)
    for fam, group in groups.items():
        if fam != family:
            # e.g. picotool's RP2350-E10 workaround block, which must be kept exactly as generated
            for b in group:
                out += b["raw"]
            continue
        for i, b in enumerate(group):
            out += HEADER.pack(UF2_MAGIC0, UF2_MAGIC1, b["flags"], b["addr"], b["size"], i, len(group), fam)
            out += b["payload"]
            out += struct.pack("<I", UF2_MAGIC_END)
    open(out_path, "wb").write(out)
    end = base + len(blob)
    print(f"{out_path}: {len(main_blocks)} blocks, program ..{prog_end:#x}, data {base:#x}..{end:#x} "
          f"({(end - 0x10000000) / (1024 * 1024):.2f}MB of flash used before saves)")


if __name__ == "__main__":
    main()
