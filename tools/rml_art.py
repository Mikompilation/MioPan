"""Generate the RmlUi menus' decorative images into resources/rml/images.

Run from anywhere:  python tools/rml_art.py

Pure Python (zlib + struct), no imaging library, so it runs on any checkout.
Everything is drawn from geometry with supersampling, and every image is
straight (not premultiplied) RGBA -- the backend premultiplies on load.

The look is the game's: a crimson butterfly glowing out of black, the film
grain over everything, and the gold frames and amber cursor bar of the game's
own windows.
"""

import math
import os
import random
import struct
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "resources", "rml", "images")


def write_png(path, width, height, pixels):
    """pixels: flat list of (r, g, b, a) tuples, 0..255."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixels[y * width + x])

    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def lerp(a, b, t):
    return a + (b - a) * t


def blur(values, width, height, radius):
    """Three box passes each way: close enough to a gaussian."""
    for _ in range(3):
        out = [0.0] * (width * height)
        for y in range(height):
            row = y * width
            acc = 0.0
            window = 2 * radius + 1
            for x in range(-radius, radius + 1):
                acc += values[row + min(max(x, 0), width - 1)]
            for x in range(width):
                out[row + x] = acc / window
                acc += values[row + min(x + radius + 1, width - 1)]
                acc -= values[row + max(x - radius, 0)]
        values = out
        out = [0.0] * (width * height)
        for x in range(width):
            acc = 0.0
            window = 2 * radius + 1
            for y in range(-radius, radius + 1):
                acc += values[min(max(y, 0), height - 1) * width + x]
            for y in range(height):
                out[y * width + x] = acc / window
                acc += values[min(y + radius + 1, height - 1) * width + x]
                acc -= values[max(y - radius, 0) * width + x]
        values = out
    return values


# ---------------------------------------------------------------------------
#  The butterfly.
# ---------------------------------------------------------------------------

def wing_radius(phi):
    """Distance from the body to the wing edge at angle phi (radians, 0 is
    straight out to the right, positive up) -- one side's forewing and
    hindwing as two lobes, the forewing swept up and back like the game's
    crimson butterflies."""
    deg = math.degrees(phi)
    fore = 1.00 * math.exp(-((deg - 34.0) / 34.0) ** 2)
    hind = 0.74 * math.exp(-((deg + 38.0) / 28.0) ** 2)
    # A scalloped trailing edge and a slight point at the forewing tip.
    scallop = 0.035 * math.sin(deg * 0.55)
    tip = 0.10 * math.exp(-((deg - 52.0) / 9.0) ** 2)
    return max(fore, hind) + scallop + tip


def butterfly(width=192, height=144, ss=3):
    W, H = width * ss, height * ss
    cx, cy = W / 2.0, H * 0.47
    scale = W * 0.47
    cover = [0.0] * (W * H)     # wing coverage
    tone = [0.0] * (W * H)      # 0 near the body .. 1 at the edge
    body = [0.0] * (W * H)
    for y in range(H):
        for x in range(W):
            dx = (x - cx) / scale
            dy = (cy - y) / scale
            ax = abs(dx)
            if ax < 1e-6 and abs(dy) < 1e-6:
                continue
            phi = math.atan2(dy, ax)
            if phi < -1.45 or phi > 1.45:
                continue
            r = math.hypot(ax, dy)
            edge = wing_radius(phi)
            if r < edge and ax > 0.015:
                i = y * W + x
                cover[i] = 1.0
                tone[i] = r / edge
            # The body: a slim ellipse down the middle.
            if (dx / 0.035) ** 2 + ((dy + 0.02) / 0.30) ** 2 < 1.0:
                body[y * W + x] = 1.0
    # Antennae: two thin arcs up and out.
    for side in (-1, 1):
        for k in range(400):
            t = k / 399.0
            ax = side * (0.02 + 0.16 * t)
            ay = 0.26 + 0.32 * t - 0.10 * t * t
            px = int(cx + ax * scale)
            py = int(cy - ay * scale)
            for oy in range(-ss // 2, ss // 2 + 1):
                for ox in range(-ss // 2, ss // 2 + 1):
                    if 0 <= px + ox < W and 0 <= py + oy < H:
                        body[(py + oy) * W + px + ox] = 1.0

    # Downsample.
    def down(src):
        out = [0.0] * (width * height)
        for y in range(height):
            for x in range(width):
                acc = 0.0
                for sy in range(ss):
                    row = (y * ss + sy) * W + x * ss
                    for sx in range(ss):
                        acc += src[row + sx]
                out[y * width + x] = acc / (ss * ss)
        return out

    cover_d = down(cover)
    tone_d = down([tone[i] * cover[i] for i in range(W * H)])
    body_d = down(body)
    glow = blur([max(cover_d[i], body_d[i]) for i in range(width * height)],
                width, height, 6)

    pixels = []
    for i in range(width * height):
        c = cover_d[i]
        t = tone_d[i] / c if c > 0.0 else 0.0
        # Deep blood at the root, lantern crimson across the wing, a darker
        # rim at the very edge -- the wing lit from within.
        if t < 0.55:
            k = t / 0.55
            r, g, b = lerp(70, 212, k), lerp(6, 34, k), lerp(14, 52, k)
        else:
            k = (t - 0.55) / 0.45
            r, g, b = lerp(212, 120, k ** 2), lerp(34, 12, k ** 2), lerp(52, 22, k ** 2)
        wing_a = c
        # The body is near-black against the wings.
        bd = body_d[i]
        r = lerp(r, 24, bd)
        g = lerp(g, 6, bd)
        b = lerp(b, 8, bd)
        a = max(wing_a, bd)
        # The halo under it all.
        halo = glow[i] * 0.55
        if a < 1.0:
            ha = halo * (1.0 - a)
            out_a = a + ha
            if out_a > 0:
                r = (r * a + 200 * ha) / out_a
                g = (g * a + 20 * ha) / out_a
                b = (b * a + 40 * ha) / out_a
            a = out_a
        pixels.append((int(clamp(r / 255.0) * 255), int(clamp(g / 255.0) * 255),
                       int(clamp(b / 255.0) * 255), int(clamp(a) * 255)))
    return width, height, pixels


# ---------------------------------------------------------------------------
#  The rest.
# ---------------------------------------------------------------------------

def vignette(size=256):
    """Black at the edges, clear in the middle, a breath of red between --
    stretched over the whole screen behind a menu."""
    pixels = []
    for y in range(size):
        for x in range(size):
            dx = (x + 0.5) / size * 2.0 - 1.0
            dy = (y + 0.5) / size * 2.0 - 1.0
            d = math.sqrt(dx * dx * 0.85 + dy * dy)
            a = clamp((d - 0.35) / 1.0) ** 1.6
            red = clamp(1.0 - abs(d - 0.95) / 0.5) * 0.35
            pixels.append((int(40 * red), int(3 * red), int(6 * red), int(a * 235)))
    return size, size, pixels


def grain(size=128, seed=1997):
    """Film grain to tile behind a menu: a faint speckle, mostly dark.  Kept
    well under the game's own grain, which is already on the picture behind
    it -- this only gives the flat black of the panels a surface."""
    rng = random.Random(seed)
    pixels = []
    for _ in range(size * size):
        v = rng.random()
        if v < 0.45:
            pixels.append((0, 0, 0, int(rng.random() ** 3 * 40)))
        elif v < 0.6:
            pixels.append((235, 220, 200, int(rng.random() ** 5 * 18)))
        else:
            pixels.append((0, 0, 0, 0))
    return size, size, pixels


def _cursor(width, height, horizontal_of):
    """The game's own cursor (DrawCmnSelCsr): an amber bar with a hot yellow
    core, between two thin gold rails, all fading out along the row."""
    pixels = []
    for y in range(height):
        v = (y + 0.5) / height * 2.0 - 1.0
        body = math.exp(-(v / 0.50) ** 2) * 0.80
        core = math.exp(-(v / 0.14) ** 2)
        rail = max(clamp(1.0 - abs(y - 1.0) / 1.0), clamp(1.0 - abs(y - (height - 2.0)) / 1.0))
        for x in range(width):
            h = horizontal_of((x + 0.5) / width)
            b_a = body * h
            c_a = core * h
            r_a = rail * clamp(h * 1.3)
            # amber body, yellow-white core, gold rails
            a = clamp(b_a + c_a * 0.6 + r_a * 0.9)
            if a <= 0.0:
                pixels.append((0, 0, 0, 0))
                continue
            r = (232 * b_a + 255 * c_a * 0.6 + 226 * r_a * 0.9) / (b_a + c_a * 0.6 + r_a * 0.9)
            g = (112 * b_a + 214 * c_a * 0.6 + 178 * r_a * 0.9) / (b_a + c_a * 0.6 + r_a * 0.9)
            bl = (28 * b_a + 140 * c_a * 0.6 + 72 * r_a * 0.9) / (b_a + c_a * 0.6 + r_a * 0.9)
            pixels.append((int(r), int(g), int(bl), int(a * 255)))
    return width, height, pixels


def cursor_glow(width=256, height=32):
    """For a row read from the left: brightest at the left, dying away to the
    right."""
    return _cursor(width, height,
                   lambda u: clamp(1.0 - u) ** 1.3 * clamp(u / 0.03))


def cursor_bar(width=256, height=32):
    """For a centred item: brightest in the middle, dying away to both ends."""
    return _cursor(width, height,
                   lambda u: clamp(1.0 - abs(u * 2.0 - 1.0)) ** 1.1)


def rule(width=512, height=12):
    """A gold hairline that fades at both ends, with a small diamond at its
    centre -- the frame line of the game's windows."""
    pixels = []
    cy = (height - 1) / 2.0
    for y in range(height):
        for x in range(width):
            u = (x + 0.5) / width * 2.0 - 1.0
            fade = clamp(1.0 - abs(u)) ** 0.8
            line = clamp(1.0 - abs(y - cy) / 0.9)
            dx = abs(x + 0.5 - width / 2.0)
            dy = abs(y + 0.5 - height / 2.0)
            diamond = clamp((5.0 - (dx * 0.9 + dy * 1.2)) / 1.0)
            a = max(line * fade * 0.85, diamond)
            # Gold, like the game's window frames, with a hot centre.
            r = lerp(176, 250, diamond)
            g = lerp(136, 214, diamond)
            b = lerp(60, 120, diamond)
            pixels.append((int(r), int(g), int(b), int(clamp(a) * 255)))
    return width, height, pixels


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, image in (("butterfly.png", butterfly()),
                        ("vignette.png", vignette()),
                        ("grain.png", grain()),
                        ("cursor_glow.png", cursor_glow()),
                        ("cursor_bar.png", cursor_bar()),
                        ("rule.png", rule())):
        width, height, pixels = image
        write_png(os.path.join(OUT, name), width, height, pixels)
        print("wrote", name, width, "x", height)


if __name__ == "__main__":
    main()
