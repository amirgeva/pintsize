"""Draws the Windows program icon, res/pintsize.ico: Dot's red coupe, angled up and to the right.

The car is built from the same parts, outline, highlight and shadow rules as Gfx::buildCars and
Gfx::drawRace in src/gfx.cpp, so the icon matches the sprite seen in the game. Small sizes are
drawn pixel for pixel; larger ones scale up a 20, 24 or 32 pixel drawing to keep the chunky look.

    python tools/make_icon.py [preview.png]

Needs Pillow. The optional argument also writes a sheet of every size, for checking by eye.
"""
import io
import math
import pathlib
import struct
import sys

from PIL import Image

TYRE, GLASS, CHROME = (0x16, 0x16, 0x1A), (0x24, 0x34, 0x4E), (0xD8, 0xDE, 0xE6)
BODY, DARK, LIGHT = "body", "dark", "light"
OUTLINE = (0x10, 0x10, 0x14)
RED = (226, 58, 52)  # driverColor(0), Dot
SHADOW_ALPHA = 100

# (u0, u1, v0, v1, colour, taper), copied from vehicleParts(VehicleKind::Coupe).
COUPE = [
    (-8, -4, -6.5, 6.5, TYRE, 0), (4, 8, -6.5, 6.5, TYRE, 0),
    (-10, 10, -5, 5, BODY, 0.25), (-10, -8, -5, 5, DARK, 0), (-5, 3, -4, 4, DARK, 0),
    (-3, 0.5, -3.4, 3.4, LIGHT, 0), (0.5, 3.5, -3.6, 3.6, GLASS, 0), (-6, -3.8, -3.4, 3.4, GLASS, 0),
    (8.5, 10, -3.5, 3.5, CHROME, 0),
]
HEADING = -math.pi / 4  # screen y points down, so this faces up and to the right

# Icon size -> size of the pixel drawing it is scaled up from.
SIZES = {16: 16, 20: 20, 24: 24, 32: 32, 40: 20, 48: 24, 64: 32, 128: 32, 256: 32}


def scale_color(c, f):
    return tuple(min(255, int(x * f)) for x in c)


def car_pixels(grid, zoom, cx, cy):
    """The car on a grid x grid canvas, centred at (cx, cy): rows of RGB tuples or None."""
    ca, sa = math.cos(HEADING), math.sin(HEADING)
    paint = {BODY: RED, DARK: scale_color(RED, 0.6), LIGHT: scale_color(RED, 1.3)}
    frame = [[None] * grid for _ in range(grid)]
    for y in range(grid):
        for x in range(grid):
            px, py = (x + 0.5 - cx) / zoom, (y + 0.5 - cy) / zoom
            u, v = px * ca + py * sa, -px * sa + py * ca
            for u0, u1, v0, v1, c, taper in COUPE:
                if u < u0 or u > u1:
                    continue
                s = 1 - taper * (u - u0) / (u1 - u0)
                if v0 * s <= v <= v1 * s:
                    frame[y][x] = paint.get(c, c)

    def solid(x, y):
        return 0 <= x < grid and 0 <= y < grid and frame[y][x] is not None

    out = [[None] * grid for _ in range(grid)]
    for y in range(grid):
        for x in range(grid):
            c = frame[y][x]
            if c is None:
                if solid(x - 1, y) or solid(x + 1, y) or solid(x, y - 1) or solid(x, y + 1):
                    out[y][x] = OUTLINE
            elif not solid(x - 1, y) or not solid(x, y - 1):
                out[y][x] = scale_color(c, 1.25)
            else:
                out[y][x] = c
    return out


def draw(grid):
    """The largest car, with its shadow, that fits the grid with a pixel to spare on every side."""
    sx, sy = max(1, round(grid * 2 / 32)), max(1, round(grid * 3 / 32))  # the in-game shadow offset
    best = None
    zoom = grid / 32
    while True:
        # Centre the car and its shadow together.
        cx, cy = grid / 2 - sx / 2, grid / 2 - sy / 2
        car = car_pixels(grid, zoom, cx, cy)
        filled = [(x, y) for y in range(grid) for x in range(grid) if car[y][x] is not None]
        x0, x1 = min(x for x, _ in filled), max(x for x, _ in filled) + sx
        y0, y1 = min(y for _, y in filled), max(y for _, y in filled) + sy
        if x0 < 1 or y0 < 1 or x1 > grid - 2 or y1 > grid - 2:
            break
        best = car, sx, sy
        zoom += grid / 32 * 0.02
    car, sx, sy = best
    img = Image.new("RGBA", (grid, grid), (0, 0, 0, 0))
    for y in range(grid):
        for x in range(grid):
            if car[y][x] is not None and x + sx < grid and y + sy < grid:
                img.putpixel((x + sx, y + sy), (0, 0, 0, SHADOW_ALPHA))
    for y in range(grid):
        for x in range(grid):
            if car[y][x] is not None:
                img.putpixel((x, y), car[y][x] + (255,))
    return img


def bmp_entry(img):
    """A 32-bit DIB as stored in .ico files: BGRA rows bottom-up, then a 1-bit transparency mask."""
    w, h = img.size
    px = img.load()
    xor = b"".join(bytes((px[x, y][2], px[x, y][1], px[x, y][0], px[x, y][3]))
                   for y in reversed(range(h)) for x in range(w))
    stride = (w + 31) // 32 * 4
    mask = bytearray()
    for y in reversed(range(h)):
        row = bytearray(stride)
        for x in range(w):
            if px[x, y][3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + xor + bytes(mask)


def write_ico(path, images):
    """Writes PNG data from 64 pixels up, which keeps the file small, and plain bitmaps below that."""
    blobs = []
    for img in images:
        if img.width >= 64:
            buf = io.BytesIO()
            img.save(buf, format="PNG", optimize=True)
            blobs.append(buf.getvalue())
        else:
            blobs.append(bmp_entry(img))
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for img, blob in zip(images, blobs):
        out += struct.pack("<BBBBHHII", img.width % 256, img.height % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    path.write_bytes(out + b"".join(blobs))


def main():
    drawings = {grid: draw(grid) for grid in sorted(set(SIZES.values()))}
    images = [drawings[grid].resize((size, size), Image.NEAREST) for size, grid in sorted(SIZES.items())]
    target = pathlib.Path(__file__).resolve().parent.parent / "res" / "pintsize.ico"
    target.parent.mkdir(exist_ok=True)
    write_ico(target, images)
    print(f"wrote {target} ({target.stat().st_size} bytes, sizes {sorted(SIZES)})")

    if len(sys.argv) > 1:
        sheet = Image.new("RGBA", (sum(i.width + 8 for i in images) + 8, 272), (120, 150, 110, 255))
        x = 8
        for img in images:
            sheet.alpha_composite(img, (x, 264 - img.height))
            x += img.width + 8
        sheet.save(sys.argv[1])
        print(f"wrote {sys.argv[1]}")


if __name__ == "__main__":
    main()
