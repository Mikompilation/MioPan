"""Generate the EU retail release's CD table, miopan_cdtable_eu_retail.inc.

Run from anywhere:  python tools/cd_table_from_disc.py <image of the EU disc>

The image is the retail "Project Zero II: Crimson Butterfly" PAL disc
(SLES_523.84, 2004), as a plain .iso.  Its executable is stripped -- no symbols
and no file names -- so cd_dat_tbl is found by what it looks like: the longest
run of 12-byte rows (flags and start sector, size, packed size) that all fit
inside the disc's IMG_BD.BIN.  The rows are then checked against the
prototype's table in system.c, whose names they inherit: the same number of
rows, the same files present, the same files packed, and file_ext_tbl
following the table byte for byte.  Any disagreement stops the script rather
than emitting a table that would put a name on the wrong file.

Pure Python, std only.
"""

import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYSTEM_C = os.path.join(ROOT, "zero_rom", "zero2np", "src", "system", "os", "system.c")
OUT = os.path.join(ROOT, "zero_rom", "zero2np", "src", "miopan", "io",
                   "miopan_cdtable_eu_retail.inc")

SECTOR = 2048


def iso_root(f):
    """The root directory's (offset, size), from the primary volume descriptor."""
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    if pvd[1:6] != b"CD001" or pvd[0] != 1:
        sys.exit("not an ISO 9660 image")
    block = struct.unpack_from("<H", pvd, 128)[0]
    root = pvd[156:190]
    return struct.unpack_from("<I", root, 2)[0] * block, struct.unpack_from("<I", root, 10)[0], block


def iso_list(f, offset, size, block):
    f.seek(offset)
    data = f.read(size)
    at = 0
    entries = {}
    while at < len(data):
        length = data[at]
        if length == 0:
            at = (at // SECTOR + 1) * SECTOR
            continue
        rec = data[at:at + length]
        at += length
        name = rec[33:33 + rec[32]].decode("latin-1").split(";")[0].rstrip(".")
        if name in ("\0", "\1"):
            continue
        entries[name.upper()] = (struct.unpack_from("<I", rec, 2)[0] * block,
                                 struct.unpack_from("<I", rec, 10)[0])
    return entries


def prototype_tables():
    """system.c's cd_dat_tbl rows and file_ext_tbl, as the reconstruction has them."""
    src = open(SYSTEM_C, encoding="utf-8", errors="replace").read()

    def block(start):
        i = src.index(start)
        return src[i:src.index("\n};", i)]

    rows = [tuple(int(x) for x in m.groups()) for m in re.finditer(
        r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}",
        block("static CD_DAT_TBL cd_dat_tbl"))]
    ext_block = block("static char file_ext_tbl")
    ext = bytes(int(x) for x in re.findall(r"\d+", ext_block[ext_block.index("{"):]))
    return rows, ext


def row_at(elf, p):
    word, size, packed = struct.unpack_from("<3I", elf, p)
    return (word & 1, (word >> 1) & 1, word >> 2, size, packed)


def plausible(row, img_size):
    packed, exist, sector, size, packed_size = row
    if not exist:
        return sector == 0 and not packed
    if size > 0x7fffffff or packed_size > 0x7fffffff:
        return False
    if not packed and packed_size != size:
        return False
    return sector * SECTOR + packed_size <= img_size


def find_table(elf, img_size):
    """(file offset, row count) of the longest run of rows with the most files."""
    phoff = struct.unpack_from("<I", elf, 0x1c)[0]
    phnum = struct.unpack_from("<H", elf, 0x2c)[0]
    best = (0, 0, 0)
    for k in range(phnum):
        ptype, off, _, _, filesz = struct.unpack_from("<5I", elf, phoff + 32 * k)
        if ptype != 1:
            continue
        for phase in (0, 4, 8):
            rows = files = last = start = 0
            p = off + phase
            while p + 12 <= off + filesz:
                row = row_at(elf, p)
                if plausible(row, img_size):
                    if rows == 0:
                        start, files, last = p, 0, 0
                    rows += 1
                    if row[1]:
                        files += 1
                        last = rows
                    if files > best[0]:
                        best = (files, start, last)
                else:
                    rows = 0
                p += 12
    return best[1], best[2]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        root, size, block = iso_root(f)
        entries = iso_list(f, root, size, block)
        if "IMG_BD.BIN" not in entries or "SYSTEM.CNF" not in entries:
            sys.exit("no IMG_BD.BIN or SYSTEM.CNF on that image")
        f.seek(entries["SYSTEM.CNF"][0])
        cnf = f.read(entries["SYSTEM.CNF"][1]).decode("latin-1")
        boot = re.search(r"BOOT2\s*=\s*cdrom0:\\([^;\r\n]+)", cnf, re.I)
        if boot is None or boot.group(1).upper() not in entries:
            sys.exit("SYSTEM.CNF names no executable on the image")
        elf_name = boot.group(1).upper()
        f.seek(entries[elf_name][0])
        elf = f.read(entries[elf_name][1])
        img_size = entries["IMG_BD.BIN"][1]

    offset, count = find_table(elf, img_size)
    rows = [row_at(elf, offset + 12 * k) for k in range(count)]
    proto, proto_ext = prototype_tables()
    print(f"{elf_name}: cd_dat_tbl at file offset {offset:#x}, {count} rows, "
          f"{sum(r[1] for r in rows)} files; IMG_BD.BIN {img_size} bytes")

    if count != len(proto):
        sys.exit(f"{count} rows, the prototype has {len(proto)}: the names would not fit")
    for no, (r, p) in enumerate(zip(rows, proto)):
        if r[0] != p[0] or r[1] != p[1]:
            sys.exit(f"row {no}: packed/present {r[:2]}, the prototype's {p[:2]}")
    ext_at = elf.find(proto_ext, offset + 12 * count)
    if ext_at < 0 or ext_at - (offset + 12 * count) > 16:
        sys.exit("file_ext_tbl does not follow the table as the prototype's")

    moved = sum(1 for r, p in zip(rows, proto) if r[1] and (r[3], r[4]) != (p[3], p[4]))
    print(f"every row agrees with the prototype's; {moved} files are stored at another size")

    with open(OUT, "w", newline="\n") as out:
        out.write("/* The EU retail release's cd_dat_tbl, row for row against system.c's.\n"
                  f" * Generated by tools/cd_table_from_disc.py from the disc's {elf_name};\n"
                  " * do not edit.  { cmp_flg, exist_flg, start_sector, size, cmp_size } */\n")
        for r in rows:
            out.write("{ %d, %d, %d, %d, %d },\n" % r)
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
