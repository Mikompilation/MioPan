#!/usr/bin/env python3
"""Export MioPan's original glyph atlases as transparent RGBA PNGs."""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib

LIMIT = 32 * 1024 * 1024
GAME = "ff2"
BANKS = [(1120, 36, 16136, 128),
 (2144, 36, 16138, 256),
 (4192, 36, 16140, 256),
 (0, 44, 16200, 256),
 (2048, 44, 16202, 256),
 (4096, 44, 16204, 256)]

def take(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError('Truncated or invalid font data')
    return data[offset:offset + size]

def unpack(fmt, data, offset=0):
    return struct.unpack(fmt, take(data, offset, struct.calcsize(fmt)))

def decompress(data):
    if data[:4] == b'PK4\0' or unpack('<I', data)[0] <= 4096:
        return data
    size, _, block_size, count, start, table, _, _ = unpack('<8I', data)
    if not 0 < size <= LIMIT or not 0 < block_size <= LIMIT or count > 65536 or count != (size + block_size - 1) // block_size:
        raise ValueError('Not a supported font pack or CMP file')
    result = bytearray()
    for index in range(count):
        kind, stored = unpack('<HH', data, table + index * 4)
        block = take(data, start, stored)
        start += (stored + 15) & ~15
        wanted = min(block_size, size - len(result))
        if kind != 1:
            result.extend(take(block, 0, wanted))
            continue
        history = bytearray(4096)
        cursor, pos, flags, bits = 4078, 0, 0, 0
        out = bytearray()
        while len(out) < wanted:
            if not bits:
                flags = take(block, pos, 1)[0]
                pos += 1
                bits = 8
            if flags & 1:
                chunk = take(block, pos, 1)
                pos += 1
                length, match = 1, None
            else:
                lo, hi = take(block, pos, 2)
                pos += 2
                match, length = lo | ((hi & 240) << 4), (hi & 15) + 3
            for n in range(length):
                value = chunk[0] if match is None else history[(match + n) & 4095]
                if len(out) < wanted:
                    out.append(value)
                    history[cursor] = value
                    cursor = (cursor + 1) & 4095
            flags >>= 1
            bits -= 1
        result.extend(out)
    return bytes(result)

class Iso:
    def __init__(self, path):
        self.file = path.open('rb')
        pvd = self.read(16 * 2048, 2048)
        if pvd[1:6] != b'CD001':
            raise ValueError('Expected an ISO9660 disc image')
        root = pvd[156:190]
        entries = self.read(unpack('<I', root, 2)[0] * 2048, unpack('<I', root, 10)[0])
        self.files = {}
        at = 0
        while at < len(entries):
            length = entries[at]
            if not length:
                at = (at // 2048 + 1) * 2048
                continue
            entry = take(entries, at, length)
            name = take(entry, 33, take(entry, 32, 1)[0]).decode('ascii').split(';')[0]
            self.files[name] = (unpack('<I', entry, 2)[0] * 2048, unpack('<I', entry, 10)[0])
            at += length
    def read(self, at, size):
        if size < 0 or size > LIMIT:
            raise ValueError('Disc record is too large')
        self.file.seek(at)
        data = self.file.read(size)
        if len(data) != size:
            raise ValueError('Truncated disc record')
        return data
    def get(self, name):
        return self.read(*self.files[name])

def disc_packs(path, language):
    iso = Iso(path)
    image, image_size = iso.files['IMG_BD.BIN']
    if 'SLES_523.84' not in iso.files:
        raise ValueError('FF2 ISO extraction supports the European build used by MioPan; pass font packs directly for other discs')
    elf = iso.get('SLES_523.84')
    marker = struct.pack('<II', 33904, 5824)
    at = elf.find(marker)
    if at < 28 or elf.find(marker, at + 1) >= 0:
        raise ValueError('Cannot identify this FF2 disc file table')
    flags, size, stored = unpack('<III', elf, at - 28 + (4 + language) * 12)
    offset = (flags >> 2) * 2048
    length = stored if flags & 1 else size
    records = [(offset, length)]
    for offset, length in records:
        if offset + length > image_size:
            raise ValueError('Font pack is outside the disc data image')
        yield iso.read(image + offset, length)

def pictures(pack):
    pack = decompress(pack)
    count = unpack('<I', pack, 8 if pack[:4] == b'PK4\0' else 0)[0]
    if not 0 < count <= 4096:
        raise ValueError('Invalid texture pack count')
    for at in unpack('<' + 'I' * count, pack, 16):
        if take(pack, at, 4) != b'TIM2' or unpack('<H', pack, at + 6)[0] != 1:
            raise ValueError('Expected a single-picture TIM2 member')
        head = at + (128 if pack[at + 5] == 1 else 16)
        total, clut, image, header, colours = unpack('<IIIHH', pack, head)
        take(pack, head, total)
        if header < 48 or header + image + clut > total:
            raise ValueError("Invalid TIM2 member sizes")
        if not image:
            continue
        typ = pack[head + 19]
        width, height = unpack('<HH', pack, head + 20)
        tex0 = unpack('<Q', pack, head + 24)[0]
        yield pack, head, header, image, clut, colours, typ, width, height, tex0

def atlases(packs):
    found = {}
    for packed in packs:
        for data, head, header, image, clut, colours, typ, width, height, tex0 in pictures(packed):
            identity = (tex0 & 16383, tex0 >> 20 & 63, tex0 >> 37 & 16383)
            for bank, spec in enumerate(BANKS):
                if identity != spec[:3]:
                    continue
                logical_height = spec[3]
                if typ != 4 or width != 512 or not 0 < height <= logical_height or image < width * height // 2 or header < 48 or colours != 16 or clut != 64 or data[head + 18] & 63 != 3:
                    raise ValueError(f'Unsupported layout for font bank {bank}')
                palette = take(data, head + header + image, 64)
                rgba = bytearray(width * logical_height * 4)
                pixels = take(data, head + header, width * height // 2)
                for pixel in range(width * height):
                    colour = (pixels[pixel // 2] >> ((pixel & 1) * 4) & 15) * 4
                    rgba[pixel * 4:pixel * 4 + 4] = palette[colour:colour + 3] + bytes([min(255, palette[colour + 3] * 2)])
                if bank in found:
                    raise ValueError(f'Duplicate font bank {bank}; supply one language at a time')
                found[bank] = (width, logical_height, rgba)
    if len(found) != len(BANKS):
        raise ValueError('Missing font banks: ' + ', '.join(str(i) for i in range(len(BANKS)) if i not in found))
    return found

def fingerprint(packs):
    parts = sorted(hashlib.sha256(decompress(pack).rstrip(b'\0')).hexdigest() for pack in packs)
    return hashlib.sha256(('pan-font-packs-v1:' + GAME).encode() + b'\0' + ''.join(parts).encode()).hexdigest()

def png(width, height, rgba):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    rows = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, help='Read the original disc directly')
    parser.add_argument('--language', type=int, choices=range(5), default=0, help='0 English, 1 French, 2 German, 3 Spanish, 4 Italian')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('packs', nargs='*', type=Path, help='Alternatively, the original font pack (CMP compression accepted)')
    args = parser.parse_args()
    try:
        if bool(args.iso) == bool(args.packs):
            raise ValueError('Choose either --iso or font pack paths')
        for path in args.packs:
            if path.stat().st_size > LIMIT:
                raise ValueError('Font pack is too large')
        data = list(disc_packs(args.iso, args.language)) if args.iso else [p.read_bytes() for p in args.packs]
        identity = fingerprint(data)
        args.out = args.out / identity
        images = atlases(data)
        paths = {bank: args.out / f'hd_texture_font_{bank}.png' for bank in images}
        if any(p.exists() for p in paths.values()):
            raise ValueError('Output already contains font PNGs; choose another folder')
        args.out.mkdir(parents=True, exist_ok=True)
        for bank, (width, height, rgba) in sorted(images.items()):
            with paths[bank].open("xb") as output:
                output.write(png(width, height, rgba))
        print(f'Exported {len(images)} original font banks to {args.out}')
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f'Font export failed: {error}\n')

if __name__ == '__main__':
    main()
