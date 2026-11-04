#!/usr/bin/env python3
"""Generates the alpha-cutout foliage textures (needles, fern fronds, leaves, grass).

Output: assets/textures/foliage/<name>.png (RGBA, sRGB color, alpha = coverage).
Cards are composed from scanned CC0 leaf atlases (ambientCG, fetched by tools/fetch_assets.py
into .cache/ambientcg/): sprites are cut out of each atlas and arranged into branch, frond,
leaf-cluster and grass cards. Without the cache, the old procedural drawings are used.
Deterministic: fixed seeds. Requires Pillow.
"""
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "textures" / "foliage"
CACHE = ROOT / ".cache" / "ambientcg"
SS = 2  # Supersampling factor.


def jitter(rng, color, amount):
    return tuple(max(0, min(255, int(c + rng.uniform(-amount, amount)))) for c in color) + (255,)


def finalize(image, size, fill):
    """Downsample, then put a flat color under transparent pixels so mipmaps do not darken edges."""
    image = image.resize(size, Image.LANCZOS)
    background = Image.new("RGBA", size, fill + (0,))
    rgb = Image.alpha_composite(background, image)
    alpha = image.split()[3]
    rgb.putalpha(alpha)
    return rgb


def needles(seed=1, size=(512, 512)):
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    # Several twigs fanning out from the left edge, each covered with needles.
    for twig in range(7):
        y0 = h * (0.12 + 0.76 * twig / 6) + rng.uniform(-20, 20)
        angle = rng.uniform(-0.25, 0.25)
        length = w * rng.uniform(0.75, 0.95)
        x, y = 0.0, y0
        steps = 40
        for i in range(steps):
            t = i / steps
            nx = x + math.cos(angle) * length / steps
            ny = y + math.sin(angle) * length / steps
            draw.line([(x, y), (nx, ny)], fill=(70, 50, 30, 255), width=int(6 * SS * (1 - t * 0.6)))
            for side in (-1, 1):
                for _ in range(3):
                    a = angle + side * rng.uniform(0.5, 1.1)
                    nl = rng.uniform(22, 40) * SS * (1 - t * 0.4)
                    ex, ey = nx + math.cos(a) * nl, ny + math.sin(a) * nl
                    color = jitter(rng, (38, 62, 30), 14)
                    draw.line([(nx, ny), (ex, ey)], fill=color, width=max(2, int(2.5 * SS)))
            x, y = nx, ny
            angle += rng.uniform(-0.04, 0.04)
    return finalize(image, size, (40, 62, 30))


def fern(seed=2, size=(256, 1024)):
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    cx = w / 2
    # Frond: stem from bottom to top, pinnae decreasing toward the tip.
    draw.line([(cx, h), (cx, h * 0.03)], fill=(70, 90, 35, 255), width=5 * SS)
    pairs = 26
    for i in range(pairs):
        t = i / pairs
        y = h * (0.95 - 0.9 * t)
        length = w * 0.48 * math.sin(math.pi * (0.12 + 0.88 * t)) * (0.9 + 0.1 * rng.random())
        for side in (-1, 1):
            # Each pinna: chain of small lobes.
            lobes = max(3, int(9 * (1 - t * 0.6)))
            for j in range(lobes):
                u = (j + 0.5) / lobes
                px = cx + side * length * u
                py = y - length * u * 0.25
                r = (length / lobes) * 0.75 * (1 - u * 0.5)
                color = jitter(rng, (62, 110, 36), 16)
                draw.ellipse([px - r, py - r * 0.6, px + r, py + r * 0.6], fill=color)
    return finalize(image, size, (60, 105, 35))


def leaves(seed=3, size=(512, 512)):
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for _ in range(55):
        cx, cy = rng.uniform(0.12, 0.88) * w, rng.uniform(0.12, 0.88) * h
        length = rng.uniform(40, 70) * SS
        width = length * rng.uniform(0.35, 0.5)
        angle = rng.uniform(0, math.tau)
        points = []
        for k in range(24):
            t = k / 23
            # Leaf outline: pointed ellipse.
            along = (t - 0.5) * length
            across = math.sin(math.pi * t) ** 0.8 * width * 0.5
            points.append((along, across))
        points += [(a, -b) for a, b in reversed(points)]
        poly = [(cx + a * math.cos(angle) - b * math.sin(angle), cy + a * math.sin(angle) + b * math.cos(angle)) for a, b in points]
        color = jitter(rng, (58, 92, 30), 22)
        draw.polygon(poly, fill=color)
        tip = (cx + math.cos(angle) * length * 0.5, cy + math.sin(angle) * length * 0.5)
        base = (cx - math.cos(angle) * length * 0.5, cy - math.sin(angle) * length * 0.5)
        draw.line([base, tip], fill=(color[0] + 20, color[1] + 25, color[2] + 10, 255), width=SS)
    return finalize(image, size, (58, 92, 30))


def grass(seed=4, size=(512, 512)):
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for _ in range(140):
        x0 = rng.uniform(0.05, 0.95) * w
        height = rng.uniform(0.45, 0.98) * h
        bend = rng.uniform(-0.25, 0.25) * w
        width = rng.uniform(5, 9) * SS
        color = jitter(rng, (52, 78, 28), 16)
        if rng.random() < 0.15:
            color = jitter(rng, (120, 112, 60), 15)  # Dry blades.
        points_left, points_right = [], []
        for k in range(12):
            t = k / 11
            x = x0 + bend * t * t
            y = h - height * t
            half = width * (1 - t) * 0.5
            points_left.append((x - half, y))
            points_right.append((x + half, y))
        draw.polygon(points_left + list(reversed(points_right)), fill=color)
    return finalize(image, size, (52, 78, 28))


# --- Scanned sprites -----------------------------------------------------------------------------

def load_sprites(asset_id, min_area=0.0015, grid=256):
    """Cuts every separate sprite out of an atlas (connected components of the opacity map)."""
    folder = CACHE / asset_id
    if not (folder / "color.png").exists() or not (folder / "opacity.png").exists():
        return []
    color = Image.open(folder / "color.png").convert("RGB")
    alpha = Image.open(folder / "opacity.png").convert("L").resize(color.size)
    width, height = color.size
    small = alpha.resize((grid, grid), Image.BILINEAR)
    px = small.load()
    label = [0] * (grid * grid)
    components = []
    for y in range(grid):
        for x in range(grid):
            if label[y * grid + x] or px[x, y] < 48:
                continue
            index = len(components) + 1
            stack = [(x, y)]
            label[y * grid + x] = index
            cells = []
            while stack:
                cx, cy = stack.pop()
                cells.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < grid and 0 <= ny < grid and not label[ny * grid + nx] and px[nx, ny] >= 48:
                        label[ny * grid + nx] = index
                        stack.append((nx, ny))
            components.append(cells)
    sprites = []
    sx, sy = width / grid, height / grid
    for index, cells in enumerate(components, start=1):
        if len(cells) < min_area * grid * grid:
            continue
        x0 = max(0, min(c[0] for c in cells) - 1)
        y0 = max(0, min(c[1] for c in cells) - 1)
        x1 = min(grid, max(c[0] for c in cells) + 2)
        y1 = min(grid, max(c[1] for c in cells) + 2)
        # Mask of this component only, so neighbors inside the bounding box are dropped.
        mask = Image.new("L", (x1 - x0, y1 - y0), 0)
        mpx = mask.load()
        for cx, cy in cells:
            mpx[cx - x0, cy - y0] = 255
        box = (int(x0 * sx), int(y0 * sy), int(x1 * sx), int(y1 * sy))
        mask = mask.resize((box[2] - box[0], box[3] - box[1]), Image.BILINEAR).filter(ImageFilter.MaxFilter(9))
        sprite = color.crop(box).convert("RGBA")
        a = Image.composite(alpha.crop(box), Image.new("L", mask.size, 0), mask)
        sprite.putalpha(a)
        sprites.append(sprite)
    sprites.sort(key=lambda im: im.size[0] * im.size[1], reverse=True)
    return sprites


def tint(sprite, rgb, brightness=1.0):
    """Multiplies the color by rgb (0..1 per channel) and a brightness factor."""
    r, g, b, a = sprite.split()
    r = r.point(lambda v: min(255, int(v * rgb[0] * brightness)))
    g = g.point(lambda v: min(255, int(v * rgb[1] * brightness)))
    b = b.point(lambda v: min(255, int(v * rgb[2] * brightness)))
    return Image.merge("RGBA", (r, g, b, a))


def place(canvas, sprite, base, target, angle, length, flip=False):
    """Pastes a sprite scaled so its long side is `length` px, rotated by `angle` degrees
    (counter-clockwise) around `base` (0..1 within the sprite), with `base` landing on `target`."""
    w, h = sprite.size
    scale = length / max(w, h)
    image = sprite.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
    bx, by = base
    if flip:
        image = image.transpose(Image.FLIP_LEFT_RIGHT)
        bx = 1.0 - bx
    w, h = image.size
    dx, dy = bx * w - w / 2, by * h - h / 2
    image = image.rotate(angle, resample=Image.BICUBIC, expand=True)
    t = math.radians(angle)
    rx = dx * math.cos(t) + dy * math.sin(t)
    ry = -dx * math.sin(t) + dy * math.cos(t)
    px = int(target[0] - (image.size[0] / 2 + rx))
    py = int(target[1] - (image.size[1] / 2 + ry))
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    layer.paste(image, (px, py), image)
    canvas.alpha_composite(layer)


def horizontal(sprite):
    """Rotates tall sprites to lie horizontally."""
    return sprite.rotate(90, expand=True) if sprite.size[1] > sprite.size[0] else sprite


def vertical(sprite):
    return sprite.rotate(90, expand=True) if sprite.size[0] > sprite.size[1] else sprite


def scanned_needles(sprites, seed=1, size=(512, 512)):
    """Conifer branch card: flat sprays fanning out from the left edge (the branch side)."""
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    sprays = [horizontal(sp) for sp in sprites[:4]]
    count = 7
    for i in range(count):
        y = h * (0.1 + 0.8 * i / (count - 1)) + rng.uniform(-0.03, 0.03) * h
        angle = rng.uniform(-14, 14) + (y / h - 0.5) * -18
        length = w * rng.uniform(0.78, 0.98)
        spray = tint(sprays[i % len(sprays)], (0.62, 0.74, 0.6), rng.uniform(0.75, 1.0))
        # Twig under the spray.
        end = (math.cos(math.radians(-angle)) * length * 0.8, math.sin(math.radians(-angle)) * length * 0.8)
        draw.line([(0, y), (end[0], y + end[1])], fill=(62, 46, 30, 255), width=4 * SS)
        # Atlas sprays point their tips to the right: the base is the left end.
        place(image, spray, (0.02, 0.5), (0, y), angle, length, flip=False)
    return finalize(image, size, (44, 64, 34))


def scanned_fern(sprites, seed=2, size=(256, 1024)):
    """Fern frond: lobed leaflets in pairs along a rachis, shrinking toward the tip."""
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    cx = w / 2
    draw.line([(cx, h), (cx + w * 0.02, h * 0.03)], fill=(74, 92, 40, 255), width=4 * SS)
    pinnae = [horizontal(sp) for sp in sprites[:8]]
    pairs = 22
    for i in range(pairs):
        t = i / pairs
        y = h * (0.95 - 0.9 * t)
        length = w * 0.5 * math.sin(math.pi * (0.12 + 0.88 * t)) * rng.uniform(0.9, 1.05)
        for side in (-1, 1):
            pinna = tint(pinnae[(i * 2 + side) % len(pinnae)], (0.66, 0.82, 0.58), rng.uniform(0.8, 1.0))
            angle = 22 + rng.uniform(-6, 6)
            # Leaflets in the atlas have their stalk at the left: mirror for the left side.
            if side > 0:
                place(image, pinna, (0.0, 0.5), (cx + w * 0.02 * t, y), angle, length)
            else:
                place(image, pinna, (0.0, 0.5), (cx + w * 0.02 * t, y), -angle, length, flip=True)
    return finalize(image, size, (60, 96, 38))


def scanned_leaves(sprites, seed=3, size=(512, 512)):
    """Broadleaf cluster card (maple crowns, bushes): many leaves at random angles."""
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for i in range(70):
        cx, cy = rng.uniform(0.1, 0.9) * w, rng.uniform(0.1, 0.9) * h
        # Leaves deeper in the cluster (drawn first) are darker.
        shade = 0.55 + 0.45 * i / 70
        leaf = tint(sprites[rng.randrange(len(sprites))], (0.7, 0.8, 0.62), shade * rng.uniform(0.85, 1.05))
        place(image, leaf, (0.5, 0.5), (cx, cy), rng.uniform(0, 360), rng.uniform(70, 120) * SS, flip=rng.random() < 0.5)
    return finalize(image, size, (56, 84, 34))


def scanned_grass(sprites, seed=4, size=(512, 512)):
    """Grass tuft card: blades rooted along the bottom edge."""
    rng = random.Random(seed)
    w, h = size[0] * SS, size[1] * SS
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    blades = [vertical(sp) for sp in sprites if max(sp.size) > 7 * min(sp.size)] or sprites
    for i in range(110):
        x = rng.uniform(0.04, 0.96) * w
        height = rng.uniform(0.45, 0.97) * h
        dry = rng.random() < 0.15
        color = (0.9, 0.84, 0.52) if dry else (0.72, 0.86, 0.56)
        blade = tint(blades[rng.randrange(len(blades))], color, rng.uniform(0.7, 1.0) * (0.75 + 0.25 * i / 110))
        place(image, blade, (0.5, 1.0), (x, h), rng.uniform(-14, 14), height, flip=rng.random() < 0.5)
    return finalize(image, size, (52, 76, 30))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cards = (
        ("needles", "LeafSet019", scanned_needles, needles),
        ("fern", "LeafSet026", scanned_fern, fern),
        ("leaves", "LeafSet024", scanned_leaves, leaves),
        ("grass", "Foliage001", scanned_grass, grass),
    )
    for name, atlas, from_scans, procedural in cards:
        sprites = load_sprites(atlas)
        if sprites:
            image = from_scans(sprites)
            source = f"{atlas} ({len(sprites)} sprites)"
        else:
            image = procedural()
            source = "procedural (run tools/fetch_assets.py for scanned leaves)"
        image.save(OUT / f"{name}.png", optimize=True)
        print(f"  foliage/{name}.png {image.size} from {source}")


if __name__ == "__main__":
    main()
