#!/usr/bin/env python3
"""Generates the alpha-cutout foliage textures (needles, fern fronds, leaves, grass).

Output: assets/textures/foliage/<name>.png (RGBA, sRGB color, alpha = coverage).
Deterministic: fixed seeds. Requires Pillow.
"""
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "textures" / "foliage"
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


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, image in (("needles", needles()), ("fern", fern()), ("leaves", leaves()), ("grass", grass())):
        image.save(OUT / f"{name}.png", optimize=True)
        print(f"  foliage/{name}.png {image.size}")


if __name__ == "__main__":
    main()
