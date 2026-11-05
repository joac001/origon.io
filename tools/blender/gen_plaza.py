"""Plaza de Mayo (Buenos Aires) city geometry, generated headless in Blender from the OSM layout.

Run (after `python3 tools/mapgen/plaza_layout.py`):
    blender --background --factory-startup --python tools/blender/gen_plaza.py

Writes assets/models/maps/plaza_de_mayo/chunk_XX_YY.glb (nodes _LOD0/_LOD1/_LOD2 and _COL) and
maps/plaza_de_mayo.json (scenery entries, trees, spawns, environment). Layout space: X east (plaza
long axis), Y north, Z up; the engine maps (x, y, z) to (x, z, -y).

Materials: "pbr_<texture>@<variant>" use assets/textures/<texture> with the base color as tint; plain
names are flat colors. Footprints and heights come from OpenStreetMap ((c) OpenStreetMap
contributors, ODbL); the landmark facades (Casa Rosada, Cabildo, Catedral, Pirámide de Mayo,
Belgrano) are simplified by hand from reference proportions.
"""
import json
import math
import pathlib
import random

import bpy

ROOT = pathlib.Path(__file__).resolve().parents[2]
LAYOUT = ROOT / ".cache" / "osm" / "plaza_layout.json"
OUTPUT = ROOT / "assets" / "models" / "maps" / "plaza_de_mayo"
MAP_FILE = ROOT / "maps" / "plaza_de_mayo.json"
CHUNK = 80.0

SIDEWALK_TOP = 0.16
SIDEWALK_WIDTH = 3.6

# name -> (texture id or None, tint rgb, roughness, metallic, tile meters)
MATERIALS = {
    "pbr_asphalt_02@road": ("asphalt_02", (0.85, 0.85, 0.85), 0.9, 0.0, 7.0),
    "pbr_rectangular_paving@walk": ("rectangular_paving", (0.95, 0.95, 0.95), 0.85, 0.0, 2.0),
    "pbr_concrete_pavement@plaza": ("concrete_pavement", (1.0, 1.0, 1.0), 0.8, 0.0, 3.0),
    "pbr_granite_tile@curb": ("granite_tile", (1.0, 1.0, 1.0), 0.7, 0.0, 1.5),
    "pbr_aerial_grass_rock@lawn": ("aerial_grass_rock", (0.62, 0.78, 0.45), 0.95, 0.0, 4.0),
    "pbr_marble_01@stone": ("marble_01", (0.93, 0.91, 0.86), 0.55, 0.0, 3.0),
    "pbr_marble_01@trim": ("marble_01", (0.96, 0.95, 0.92), 0.55, 0.0, 2.0),
    "pbr_white_stucco@white": ("white_stucco", (0.97, 0.96, 0.93), 0.85, 0.0, 3.0),
    "pbr_white_stucco@rosada": ("white_stucco", (0.86, 0.62, 0.56), 0.85, 0.0, 3.0),
    "pbr_white_stucco@rosada_trim": ("white_stucco", (0.93, 0.78, 0.72), 0.8, 0.0, 2.0),
    "pbr_painted_plaster_wall@cream": ("painted_plaster_wall", (0.93, 0.88, 0.76), 0.85, 0.0, 3.5),
    "pbr_painted_plaster_wall@sand": ("painted_plaster_wall", (0.84, 0.77, 0.63), 0.85, 0.0, 3.5),
    "pbr_painted_plaster_wall@ochre": ("painted_plaster_wall", (0.86, 0.72, 0.52), 0.85, 0.0, 3.5),
    "pbr_grey_plaster@grey": ("grey_plaster", (0.82, 0.81, 0.78), 0.85, 0.0, 3.5),
    "pbr_grey_plaster@stone": ("grey_plaster", (0.72, 0.69, 0.64), 0.85, 0.0, 3.5),
    "pbr_white_stucco@offwhite": ("white_stucco", (0.9, 0.88, 0.83), 0.85, 0.0, 3.0),
    "roof_tiles": (None, (0.55, 0.26, 0.17), 0.8, 0.0, 2.0),
    "roof": (None, (0.32, 0.31, 0.3), 0.9, 0.0, 4.0),
    "glass": (None, (0.07, 0.09, 0.11), 0.12, 0.7, 1.0),
    "shop_glass": (None, (0.1, 0.12, 0.13), 0.1, 0.6, 1.0),
    "door": (None, (0.22, 0.14, 0.09), 0.6, 0.0, 1.0),
    "iron": (None, (0.06, 0.06, 0.06), 0.45, 0.8, 1.0),
    "bronze": (None, (0.2, 0.22, 0.16), 0.4, 0.9, 1.0),
    "wood": (None, (0.42, 0.27, 0.15), 0.7, 0.0, 1.0),
    "lamp_glass": (None, (0.95, 0.92, 0.8), 0.2, 0.0, 1.0),
    "palm_trunk": (None, (0.42, 0.35, 0.26), 0.95, 0.0, 1.0),
    "palm_leaf": (None, (0.22, 0.33, 0.12), 0.7, 0.0, 1.0),
    "fence_blue": (None, (0.16, 0.24, 0.36), 0.55, 0.6, 1.0),
    "flag_blue": (None, (0.45, 0.68, 0.88), 0.8, 0.0, 1.0),
    "flag_white": (None, (0.95, 0.95, 0.93), 0.8, 0.0, 1.0),
    "car_body": (None, (0.5, 0.5, 0.5), 0.35, 0.6, 1.0),
    "tyre": (None, (0.04, 0.04, 0.04), 0.9, 0.0, 1.0),
    "bus_roof": (None, (0.15, 0.3, 0.45), 0.5, 0.4, 1.0),
    "dark": (None, (0.03, 0.03, 0.03), 0.9, 0.0, 1.0),
    "jersey": (None, (0.78, 0.76, 0.72), 0.85, 0.0, 1.0),
    "paint_white": (None, (0.9, 0.9, 0.88), 0.7, 0.0, 1.0),
}
FACADES = ["pbr_painted_plaster_wall@cream", "pbr_painted_plaster_wall@sand", "pbr_painted_plaster_wall@ochre",
           "pbr_grey_plaster@grey", "pbr_grey_plaster@stone", "pbr_white_stucco@offwhite"]
CAR_COLORS = [(0.05, 0.05, 0.05), (0.75, 0.75, 0.73), (0.55, 0.08, 0.06), (0.1, 0.18, 0.35), (0.35, 0.37, 0.38), (0.85, 0.83, 0.78)]


class Geo:
    """Polygon soup: faces with a material each; UVs are world-planar per face (meters / tile)."""

    def __init__(self):
        self.verts = []
        self.faces = []  # (indices, material)

    def poly(self, points, mat):
        if len(points) < 3:
            return
        base = len(self.verts)
        self.verts.extend(points)
        self.faces.append((list(range(base, base + len(points))), mat))

    def quad(self, a, b, c, d, mat):
        self.poly([a, b, c, d], mat)

    def box(self, center, size, mat, yaw=0.0, top=True, bottom=False):
        cx, cy, cz = center
        hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
        c, s = math.cos(yaw), math.sin(yaw)

        def p(x, y, z):
            return (cx + x * c - y * s, cy + x * s + y * c, cz + z)

        v = [p(-hx, -hy, -hz), p(hx, -hy, -hz), p(hx, hy, -hz), p(-hx, hy, -hz),
             p(-hx, -hy, hz), p(hx, -hy, hz), p(hx, hy, hz), p(-hx, hy, hz)]
        self.quad(v[0], v[1], v[5], v[4], mat)
        self.quad(v[1], v[2], v[6], v[5], mat)
        self.quad(v[2], v[3], v[7], v[6], mat)
        self.quad(v[3], v[0], v[4], v[7], mat)
        if top:
            self.quad(v[4], v[5], v[6], v[7], mat)
        if bottom:
            self.quad(v[3], v[2], v[1], v[0], mat)

    def prism(self, polygon, z0, z1, side, top=None, bottom=False):
        """Extrudes a CCW polygon (outward faces)."""
        n = len(polygon)
        for i in range(n):
            a, b = polygon[i], polygon[(i + 1) % n]
            self.quad((a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1), side)
        if top:
            self.poly([(p[0], p[1], z1) for p in polygon], top)
        if bottom:
            self.poly([(p[0], p[1], z0) for p in reversed(polygon)], bottom)

    def cylinder(self, base, radius, height, mat, sides=10, top=True, radius_top=None):
        rt = radius if radius_top is None else radius_top
        ring0 = [(base[0] + math.cos(2 * math.pi * i / sides) * radius, base[1] + math.sin(2 * math.pi * i / sides) * radius, base[2])
                 for i in range(sides)]
        ring1 = [(base[0] + math.cos(2 * math.pi * i / sides) * rt, base[1] + math.sin(2 * math.pi * i / sides) * rt, base[2] + height)
                 for i in range(sides)]
        for i in range(sides):
            j = (i + 1) % sides
            self.quad(ring0[i], ring0[j], ring1[j], ring1[i], mat)
        if top and rt > 1e-4:
            self.poly(ring1, mat)

    def sphere(self, center, radius, mat, rings=6, sides=12, hemisphere=False):
        last = rings if not hemisphere else rings // 2
        rows = []
        for r in range(last + 1):
            phi = math.pi / 2 - math.pi * r / rings
            rows.append([(center[0] + math.cos(phi) * math.cos(2 * math.pi * i / sides) * radius,
                          center[1] + math.cos(phi) * math.sin(2 * math.pi * i / sides) * radius,
                          center[2] + math.sin(phi) * radius) for i in range(sides)])
        for r in range(last):
            for i in range(sides):
                j = (i + 1) % sides
                self.quad(rows[r + 1][i], rows[r + 1][j], rows[r][j], rows[r][i], mat)

    def extend(self, other):
        base = len(self.verts)
        self.verts.extend(other.verts)
        self.faces.extend(([i + base for i in f], m) for f, m in other.faces)


# --- Geometry helpers ------------------------------------------------------------------------------

def area(poly):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1]))


def centroid(poly):
    return (sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))


def in_poly(p, poly):
    x, y = p
    c = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def offset_convex(poly, d):
    """Offsets a CCW convex polygon outward by d (miter, clamped)."""
    n = len(poly)
    out = []
    for i in range(n):
        p0, p1, p2 = poly[i - 1], poly[i], poly[(i + 1) % n]
        e0 = (p1[0] - p0[0], p1[1] - p0[1])
        e1 = (p2[0] - p1[0], p2[1] - p1[1])
        l0, l1 = math.hypot(*e0) or 1.0, math.hypot(*e1) or 1.0
        n0 = (e0[1] / l0, -e0[0] / l0)
        n1 = (e1[1] / l1, -e1[0] / l1)
        bis = (n0[0] + n1[0], n0[1] + n1[1])
        lb = math.hypot(*bis) or 1.0
        bis = (bis[0] / lb, bis[1] / lb)
        cos_half = max(0.35, bis[0] * n1[0] + bis[1] * n1[1])
        out.append((p1[0] + bis[0] * d / cos_half, p1[1] + bis[1] * d / cos_half))
    return out


def simplify(poly, tolerance=0.25):
    """Drops nearly collinear and duplicate vertices."""
    out = []
    for p in poly:
        if out and math.dist(p, out[-1]) < 0.05:
            continue
        out.append(p)
    changed = True
    while changed and len(out) > 3:
        changed = False
        for i in range(len(out)):
            a, b, c = out[i - 1], out[i], out[(i + 1) % len(out)]
            cross = abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]))
            if cross / max(math.dist(a, c), 1e-3) < tolerance:
                out.pop(i)
                changed = True
                break
    return out


# --- Output buckets ---------------------------------------------------------------------------------

class Chunks:
    """Geometry per chunk and level: lod 0..2 for rendering, 'col' for collision."""

    def __init__(self, bounds):
        self.bounds = bounds
        self.data = {}

    def key(self, x, y):
        return (int(math.floor((x - self.bounds[0]) / CHUNK)), int(math.floor((y - self.bounds[1]) / CHUNK)))

    def get(self, x, y, level):
        k = self.key(x, y)
        return self.data.setdefault(k, {}).setdefault(level, Geo())

    def add(self, x, y, lods, geo):
        """lods: iterable of levels (0, 1, 2, 'col')."""
        for level in lods:
            self.get(x, y, level).extend(geo)


# --- Ground -----------------------------------------------------------------------------------------

def build_ground(layout, chunks):
    b = layout["bounds"]
    margin = 40.0
    x0, y0, x1, y1 = b[0] - margin, b[1] - margin, b[2] + margin, b[3] + margin
    # Asphalt in chunk-sized tiles (keeps chunk bounds small).
    x = x0
    while x < x1:
        y = y0
        while y < y1:
            xe, ye = min(x + CHUNK / 2, x1), min(y + CHUNK / 2, y1)
            g = Geo()
            g.quad((x, y, 0.0), (xe, y, 0.0), (xe, ye, 0.0), (x, ye, 0.0), "pbr_asphalt_02@road")
            chunks.add((x + xe) / 2, (y + ye) / 2, (0, 1, 2, "col"), g)
            y = ye
        x = xe
    # Sidewalks: each city block's hull, offset outward, raised with a curb.
    for block in layout["blocks"]:
        hull = block["hull"]
        if len(hull) < 3:
            continue
        if area(hull) < 0:
            hull = hull[::-1]
        walk = offset_convex(hull, SIDEWALK_WIDTH)
        g = Geo()
        g.prism(walk, -0.2, SIDEWALK_TOP, "pbr_granite_tile@curb", top="pbr_rectangular_paving@walk")
        cx, cy = centroid(walk)
        chunks.add(cx, cy, (0, 1, 2, "col"), g)
    # The plaza: paved, raised, with lawns.
    plaza = layout["plaza"]
    if area(plaza) < 0:
        plaza = plaza[::-1]
    g = Geo()
    g.prism(simplify(plaza, 0.05), -0.2, SIDEWALK_TOP, "pbr_granite_tile@curb", top="pbr_concrete_pavement@plaza")
    chunks.add(0.0, 0.0, (0, 1, 2, "col"), g)
    for bed in layout["grass"]:
        poly = simplify(bed, 0.05)
        if len(poly) < 3:
            continue
        if area(poly) < 0:
            poly = poly[::-1]
        g = Geo()
        g.prism(poly, SIDEWALK_TOP - 0.05, SIDEWALK_TOP + 0.12, "pbr_granite_tile@curb", top="pbr_aerial_grass_rock@lawn")
        cx, cy = centroid(poly)
        chunks.add(cx, cy, (0, 1, 2, "col"), g)
    # Crosswalk stripes at the plaza corners and road center lines would go here; the streets keep
    # plain asphalt for now (cars and barriers give the cover).


# --- Generic buildings ------------------------------------------------------------------------------

def exposed(a, b, normal, others):
    """True when the wall a-b faces open air (no neighbor building right outside it)."""
    for t in (0.2, 0.5, 0.8):
        p = (a[0] + (b[0] - a[0]) * t + normal[0] * 1.2, a[1] + (b[1] - a[1]) * t + normal[1] * 1.2)
        for poly in others:
            if in_poly(p, poly):
                return False
    return True


def facade(geo_lods, a, b, z_base, height, wall, trim, rng, historic):
    """Windows, sills, frames and cornices on the wall a-b (outward normal to the right of a->b)."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    if length < 2.6 or height < 5.0:
        return
    ux, uy = dx / length, dy / length
    nx, ny = uy, -ux
    yaw = math.atan2(uy, ux)
    ground = 4.6
    floor_h = 3.5 if historic else 3.25
    top = height - 1.3
    floors = max(0, int((top - ground) / floor_h))
    bays = max(1, int(length / (4.0 if historic else 3.3)))
    spacing = length / bays
    win_w = min(1.35 if historic else 1.5, spacing * 0.55)
    win_h = 2.1 if historic else 1.8

    def at(t, z, out=0.0):
        return (a[0] + ux * t + nx * out, a[1] + uy * t + ny * out, z_base + z)

    for i in range(bays):
        t = (i + 0.5) * spacing
        # Ground floor: storefronts / doors (dark glass, frame).
        w = min(spacing * 0.7, 2.6)
        gz0, gz1 = 0.25, 3.3
        g0 = Geo()
        g0.quad(at(t - w / 2, gz0, 0.03), at(t + w / 2, gz0, 0.03), at(t + w / 2, gz1, 0.03), at(t - w / 2, gz1, 0.03), "shop_glass")
        for lvl in (0, 1, 2):
            geo_lods[lvl].extend(g0)
        frame = Geo()
        frame.box(at(t, gz1 + 0.12, 0.08), (w + 0.3, 0.16, 0.24), trim, yaw)
        frame.box(at(t - w / 2 - 0.08, (gz0 + gz1) / 2, 0.06), (0.16, 0.12, gz1 - gz0), trim, yaw)
        frame.box(at(t + w / 2 + 0.08, (gz0 + gz1) / 2, 0.06), (0.16, 0.12, gz1 - gz0), trim, yaw)
        geo_lods[0].extend(frame)
        for f in range(floors):
            z0 = ground + f * floor_h + 0.85
            z1 = z0 + win_h
            g = Geo()
            g.quad(at(t - win_w / 2, z0, 0.03), at(t + win_w / 2, z0, 0.03), at(t + win_w / 2, z1, 0.03), at(t - win_w / 2, z1, 0.03), "glass")
            geo_lods[0].extend(g)
            geo_lods[1].extend(g)
            if f % 2 == 0 or floors < 6:
                geo_lods[2].extend(g)
            d = Geo()
            d.box(at(t, z0 - 0.08, 0.12), (win_w + 0.36, 0.24, 0.12), trim, yaw)  # Sill.
            d.box(at(t, z1 + 0.12, 0.07), (win_w + 0.3, 0.14, 0.24 if historic else 0.12), trim, yaw)  # Lintel.
            d.box(at(t - win_w / 2 - 0.07, (z0 + z1) / 2, 0.05), (0.14, 0.1, win_h), trim, yaw)
            d.box(at(t + win_w / 2 + 0.07, (z0 + z1) / 2, 0.05), (0.14, 0.1, win_h), trim, yaw)
            if historic and rng.random() < 0.35:
                # Small balcony with an iron railing.
                d.box(at(t, z0 - 0.15, 0.45), (win_w + 0.7, 0.9, 0.15), trim, yaw)
                d.box(at(t, z0 + 0.4, 0.88), (win_w + 0.7, 0.04, 0.9), "iron", yaw)
            geo_lods[0].extend(d)
    # Cornices: above the ground floor, at the roof line; floor bands on historic fronts.
    c = Geo()
    c.box(at(length / 2, ground - 0.1, 0.12), (length, 0.24, 0.3), trim, yaw)
    c.box(at(length / 2, top + 0.1, 0.3), (length + 0.2, 0.6, 0.45), trim, yaw)
    geo_lods[0].extend(c)
    geo_lods[1].extend(c)
    if historic:
        bands = Geo()
        for f in range(1, floors):
            bands.box(at(length / 2, ground + f * floor_h + 0.1, 0.06), (length, 0.12, 0.16), trim, yaw)
        geo_lods[0].extend(bands)


def building(layout_building, others, chunks, rng):
    poly = simplify(layout_building["polygon"])
    if len(poly) < 3:
        return
    if area(poly) < 0:
        poly = poly[::-1]
    footprint = abs(area(poly))
    height = layout_building.get("height")
    if not height:
        height = 16.0 if footprint < 250 else 24.0 + rng.uniform(-3, 6)
    height = min(max(height, 8.0), 60.0)  # The few towers in range are capped (they would hide the map).
    historic = bool(layout_building.get("historic")) or rng.random() < 0.5
    wall = rng.choice(FACADES)
    trim = "pbr_marble_01@trim" if historic else wall
    lods = {0: Geo(), 1: Geo(), 2: Geo()}
    base = -0.3
    body = Geo()
    body.prism(poly, base, height, wall, top="roof")
    for g in lods.values():
        g.extend(body)
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        e = (b[0] - a[0], b[1] - a[1])
        le = math.hypot(*e)
        if le < 0.3:
            continue
        normal = (e[1] / le, -e[0] / le)
        if exposed(a, b, normal, others):
            facade(lods, a, b, 0.0, height, wall, trim, rng, historic)
    # Roof clutter: water tanks and machine rooms on larger roofs.
    if footprint > 300:
        cx, cy = centroid(poly)
        clutter = Geo()
        for _ in range(rng.randint(1, 3)):
            px, py = cx + rng.uniform(-4, 4), cy + rng.uniform(-4, 4)
            if in_poly((px, py), poly):
                clutter.box((px, py, height + 1.2), (rng.uniform(2.5, 4.5), rng.uniform(2.5, 4.5), 2.4), "pbr_grey_plaster@grey")
        lods[0].extend(clutter)
        lods[1].extend(clutter)
    col = Geo()
    col.prism(poly, base, height, "collision", top="collision")
    cx, cy = centroid(poly)
    for level, g in lods.items():
        chunks.add(cx, cy, (level,), g)
    chunks.add(cx, cy, ("col",), col)


# --- Landmarks ---------------------------------------------------------------------------------------

def facing_edge(poly, target=(0.0, 0.0)):
    """The polygon edge whose outward normal points most toward target (usually the plaza)."""
    best, best_score = None, -1e9
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        e = (b[0] - a[0], b[1] - a[1])
        le = math.hypot(*e)
        if le < 4.0:
            continue
        normal = (e[1] / le, -e[0] / le)
        mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        to = (target[0] - mid[0], target[1] - mid[1])
        lt = math.hypot(*to) or 1.0
        score = (normal[0] * to[0] + normal[1] * to[1]) / lt * 10.0 + le * 0.02
        if score > best_score:
            best, best_score = (a, b), score
    return best


def frame_from_edge(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    nx, ny = uy, -ux

    def at(t, z, out=0.0):
        return (a[0] + ux * t + nx * out, a[1] + uy * t + ny * out, z)

    return length, math.atan2(uy, ux), at


def arch_opening(g, at, t, width, z0, spring, mat_dark, mat_trim, yaw, depth=0.04, segments=8):
    """A dark arched opening (rectangle plus a half-disc) with an archivolt band."""
    r = width / 2
    pts = [at(t - r, z0, depth), at(t + r, z0, depth), at(t + r, spring, depth)]
    for i in range(1, segments):
        ang = math.pi * i / segments
        pts.append(at(t + math.cos(ang) * r, spring + math.sin(ang) * r, depth))
    pts.append(at(t - r, spring, depth))
    g.poly(pts, mat_dark)
    for i in range(segments):
        a0, a1 = math.pi * i / segments, math.pi * (i + 1) / segments
        p0 = at(t + math.cos(a0) * (r + 0.15), spring + math.sin(a0) * (r + 0.15), depth + 0.08)
        p1 = at(t + math.cos(a1) * (r + 0.15), spring + math.sin(a1) * (r + 0.15), depth + 0.08)
        mid = ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (p0[2] + p1[2]) / 2)
        seg_len = math.dist(p0, p1)
        tilt = (a0 + a1) / 2
        g.box(mid, (seg_len + 0.05, 0.16, 0.3), mat_trim, yaw)
        _ = tilt


def casa_rosada(item, chunks):
    poly = simplify(item["polygon"])
    if area(poly) < 0:
        poly = poly[::-1]
    height = 25.0
    wall, trim = "pbr_white_stucco@rosada", "pbr_white_stucco@rosada_trim"
    lods = {0: Geo(), 1: Geo(), 2: Geo()}
    body = Geo()
    body.prism(poly, -0.3, height, wall, top="roof")
    for g in lods.values():
        g.extend(body)
    rng = random.Random(7)
    n = len(poly)
    face = facing_edge(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        if (a, b) == face:
            continue
        facade(lods, a, b, 0.0, height, wall, trim, rng, True)
    # Main front toward the plaza: rusticated base with arches, arched windows above, the central
    # arch with the balcony, a balustrade on top.
    a, b = face
    length, yaw, at = frame_from_edge(a, b)
    g = Geo()
    bays = max(5, int(length / 5.0)) | 1
    spacing = length / bays
    center = bays // 2
    for i in range(bays):
        t = (i + 0.5) * spacing
        if i == center:
            arch_opening(g, at, t, 7.0, 0.2, 9.0, "dark", trim, yaw)
            g.box(at(t, 13.2, 0.9), (9.0, 1.8, 0.3), trim, yaw)  # The balcony.
            g.box(at(t, 13.9, 1.75), (9.0, 0.1, 1.0), "iron", yaw)
            arch_opening(g, at, t, 3.0, 13.4, 16.4, "glass", trim, yaw)
            continue
        arch_opening(g, at, t, spacing * 0.5, 0.4, 3.6, "shop_glass", trim, yaw)
        for z in (8.0, 14.0):
            arch_opening(g, at, t, spacing * 0.32, z, z + 2.4, "glass", trim, yaw)
            g.box(at(t, z - 0.15, 0.15), (spacing * 0.45, 0.3, 0.15), trim, yaw)
        g.box(at(t - spacing / 2, height / 2, 0.15), (0.6, 0.3, height - 1.5), trim, yaw)  # Pilasters.
    for z, h in ((6.2, 0.5), (12.2, 0.4), (height - 1.6, 0.9)):
        g.box(at(length / 2, z, 0.35), (length + 0.4, 0.7, h), trim, yaw)
    for k in range(int(length / 0.7)):
        g.box(at((k + 0.5) * 0.7, height + 0.5, 0.2), (0.22, 0.22, 1.0), trim, yaw)
    g.box(at(length / 2, height + 1.1, 0.2), (length, 0.5, 0.2), trim, yaw)
    lods[0].extend(g)
    lods[1].extend(g)
    col = Geo()
    col.prism(poly, -0.3, height, "collision", top="collision")
    cx, cy = centroid(poly)
    for level, geo in lods.items():
        chunks.add(cx, cy, (level,), geo)
    chunks.add(cx, cy, ("col",), col)


def cabildo(item, chunks):
    poly = simplify(item["polygon"])
    if area(poly) < 0:
        poly = poly[::-1]
    a, b = facing_edge(poly)
    length, yaw, at = frame_from_edge(a, b)
    depth = 14.0
    height = 12.5
    wall, trim = "pbr_white_stucco@white", "pbr_white_stucco@white"
    g = Geo()
    # Body: a box behind the facade line (the arcade sits in its front 4 m).
    corner = [at(0, 0, 0), at(length, 0, 0), at(length, 0, -depth), at(0, 0, -depth)]
    body = [(p[0], p[1]) for p in corner]
    if area(body) < 0:
        body = body[::-1]
    g.prism(body, -0.3, height, wall)
    # Hipped tile roof.
    ridge0, ridge1 = at(depth / 2, height + 3.2, -depth / 2), at(length - depth / 2, height + 3.2, -depth / 2)
    c0, c1, c2, c3 = at(-0.4, height, 0.6), at(length + 0.4, height, 0.6), at(length + 0.4, height, -depth - 0.6), at(-0.4, height, -depth - 0.6)
    g.quad(c0, c1, ridge1, ridge0, "roof_tiles")
    g.quad(c2, c3, ridge0, ridge1, "roof_tiles")
    g.poly([c1, c2, ridge1], "roof_tiles")
    g.poly([c3, c0, ridge0], "roof_tiles")
    # Two-storey arcade: five arches each side of the tower, on both floors.
    bays = 11
    spacing = length / bays
    tower = bays // 2
    for i in range(bays):
        t = (i + 0.5) * spacing
        if i == tower:
            continue
        arch_opening(g, at, t, spacing * 0.62, 0.2, 3.6, "dark", trim, yaw)
        arch_opening(g, at, t, spacing * 0.62, 6.6, 9.6, "dark", trim, yaw)
        g.box(at(t, 6.75, 0.35), (spacing * 0.7, 0.7, 0.12), "iron", yaw)  # Upper railing line.
    g.box(at(length / 2, 6.3, 0.25), (length, 0.5, 0.35), trim, yaw)
    g.box(at(length / 2, height - 0.3, 0.3), (length + 0.4, 0.6, 0.6), trim, yaw)
    # Tower: square stages, the clock, a small dome.
    t = (tower + 0.5) * spacing
    tc = at(t, 0, -2.5)
    stages = ((6.0, 0.0, 16.5), (5.2, 16.5, 22.0), (4.2, 22.0, 26.5))
    for w, z0, z1 in stages:
        g.box((tc[0], tc[1], (z0 + z1) / 2), (w, w, z1 - z0), wall, yaw)
        g.box((tc[0], tc[1], z1 - 0.2), (w + 0.5, w + 0.5, 0.4), trim, yaw)
    arch_opening(g, at, t, 2.6, 0.2, 3.6, "door", trim, yaw, depth=0.55)
    arch_opening(g, at, t, 2.0, 18.2, 20.2, "dark", trim, yaw, depth=0.95)
    clock = at(t, 13.5, 0.62)
    g.cylinder((clock[0], clock[1], clock[2] - 0.05), 1.0, 0.1, "paint_white", sides=16)
    g.sphere((tc[0], tc[1], 26.5), 2.0, "roof_tiles", rings=8, sides=12, hemisphere=True)
    g.cylinder((tc[0], tc[1], 28.4), 0.08, 2.0, "iron", sides=4)
    lods = {0: g, 1: g, 2: g}
    col = Geo()
    col.prism(body, -0.3, height + 1.0, "collision", top="collision")
    col.box((tc[0], tc[1], 13.0), (6.0, 6.0, 26.0), "collision", yaw)
    cx, cy = centroid(body)
    for level, geo in lods.items():
        chunks.add(cx, cy, (level,), geo)
    chunks.add(cx, cy, ("col",), col)


def catedral(item, chunks):
    poly = simplify(item["polygon"])
    if area(poly) < 0:
        poly = poly[::-1]
    height = 21.0
    wall, trim = "pbr_marble_01@stone", "pbr_marble_01@trim"
    g = Geo()
    g.prism(poly, -0.3, height, wall, top="roof")
    a, b = facing_edge(poly)
    length, yaw, at = frame_from_edge(a, b)
    # Portico: steps, twelve Corinthian columns, entablature and pediment.
    steps = 6
    for s in range(steps):
        g.box(at(length / 2, 0.15 + s * 0.3, 7.5 - s * 0.45), (length, 0.9 + s * 0.0, 0.3), trim, yaw)
    floor_z = steps * 0.3
    portico_depth = 6.0
    col_h = 12.0
    for k in range(12):
        t = length * (0.06 + 0.88 * k / 11)
        base = at(t, floor_z, portico_depth - 0.6)
        g.cylinder(base, 0.62, col_h, trim, sides=14, radius_top=0.55)
        g.box((base[0], base[1], floor_z + 0.25), (1.6, 1.6, 0.5), trim, yaw)
        g.box((base[0], base[1], floor_z + col_h - 0.4), (1.5, 1.5, 0.8), trim, yaw)
    ez = floor_z + col_h
    g.box(at(length / 2, ez + 1.1, portico_depth / 2), (length, portico_depth + 0.6, 2.2), trim, yaw)
    p0, p1, p2 = at(0, ez + 2.2, portico_depth), at(length, ez + 2.2, portico_depth), at(length / 2, ez + 8.0, portico_depth)
    q0, q1, q2 = at(0, ez + 2.2, 0), at(length, ez + 2.2, 0), at(length / 2, ez + 8.0, 0)
    g.poly([p0, p1, p2], trim)
    g.quad(p1, q1, q2, p2, "roof")
    g.quad(q0, p0, p2, q2, "roof")
    for t in (length * 0.3, length * 0.5, length * 0.7):
        g.box(at(t, floor_z + 3.0, 0.08), (2.4, 0.2, 6.0), "door", yaw)
    # Dome over the crossing.
    cx, cy = centroid(poly)
    g.cylinder((cx, cy, height), 7.0, 6.0, wall, sides=20)
    g.sphere((cx, cy, height + 6.0), 7.0, "fence_blue", rings=10, sides=20, hemisphere=True)
    g.cylinder((cx, cy, height + 12.8), 1.0, 3.0, trim, sides=10)
    lods = {0: g, 1: g, 2: g}
    col = Geo()
    col.prism(poly, -0.3, height, "collision", top="collision")
    col.box(at(length / 2, ez / 2 + 1.0, portico_depth / 2 + 1.0), (length, portico_depth + 2.0, ez + 2.0), "collision", yaw)
    for level, geo in lods.items():
        chunks.add(cx, cy, (level,), geo)
    chunks.add(cx, cy, ("col",), col)


def piramide(item, chunks):
    cx, cy = centroid(item["polygon"])
    g = Geo()
    col = Geo()
    m = "pbr_white_stucco@white"
    for i, (w, h) in enumerate(((7.0, 0.5), (6.0, 0.5), (5.0, 0.5))):
        g.box((cx, cy, SIDEWALK_TOP + 0.25 + i * 0.5), (w, w, 0.5), "pbr_marble_01@trim")
    z = SIDEWALK_TOP + 1.5
    g.box((cx, cy, z + 1.8), (3.6, 3.6, 3.6), m)
    g.box((cx, cy, z + 3.75), (4.0, 4.0, 0.3), "pbr_marble_01@trim")
    # Obelisk shaft tapering to a pyramidion.
    shaft = 11.0
    s0, s1 = 1.5, 1.05
    base_z = z + 3.9
    pts0 = [(cx - s0, cy - s0), (cx + s0, cy - s0), (cx + s0, cy + s0), (cx - s0, cy + s0)]
    pts1 = [(cx - s1, cy - s1), (cx + s1, cy - s1), (cx + s1, cy + s1), (cx - s1, cy + s1)]
    for i in range(4):
        j = (i + 1) % 4
        g.quad((*pts0[i], base_z), (*pts0[j], base_z), (*pts1[j], base_z + shaft), (*pts1[i], base_z + shaft), m)
    apex = (cx, cy, base_z + shaft + 1.2)
    for i in range(4):
        j = (i + 1) % 4
        g.poly([(*pts1[i], base_z + shaft), (*pts1[j], base_z + shaft), apex], m)
    # Liberty on top: a stylized figure.
    top = base_z + shaft + 1.2
    g.cylinder((cx, cy, top), 0.45, 2.2, "paint_white", sides=8, radius_top=0.25)
    g.sphere((cx, cy, top + 2.45), 0.28, "paint_white", rings=5, sides=8)
    # Low iron fence around it.
    r = 6.5
    for k in range(4):
        ang = k * math.pi / 2
        mx, my = cx + math.cos(ang) * r, cy + math.sin(ang) * r
        g.box((mx, my, SIDEWALK_TOP + 0.5), (0.06 if k % 2 == 0 else 2 * r, 2 * r if k % 2 == 0 else 0.06, 1.0), "iron")
    col.box((cx, cy, SIDEWALK_TOP + 1.0), (7.0, 7.0, 2.0), "collision")
    col.box((cx, cy, z + 9.0), (3.6, 3.6, 18.0), "collision")
    chunks.add(cx, cy, (0, 1, 2), g)
    chunks.add(cx, cy, ("col",), col)


def belgrano(item, chunks):
    cx, cy = centroid(item["polygon"])
    g = Geo()
    g.box((cx, cy, SIDEWALK_TOP + 0.3), (8.0, 5.0, 0.6), "pbr_marble_01@trim")
    g.box((cx, cy, SIDEWALK_TOP + 2.6), (5.5, 2.8, 4.0), "pbr_grey_plaster@stone")
    g.box((cx, cy, SIDEWALK_TOP + 4.75), (6.0, 3.2, 0.3), "pbr_marble_01@trim")
    z = SIDEWALK_TOP + 4.9
    # Horse (body, neck, head, legs) and rider, facing west toward the Cabildo... the statue faces the
    # Casa Rosada's front on the plaza side: heading -X.
    g.box((cx, cy, z + 2.0), (3.0, 1.0, 1.1), "bronze")
    g.box((cx - 1.7, cy, z + 2.8), (0.9, 0.7, 1.5), "bronze", yaw=0.0)
    g.box((cx - 2.25, cy, z + 3.35), (0.9, 0.45, 0.5), "bronze")
    for dx in (-1.1, 1.1):
        for dy in (-0.3, 0.3):
            g.cylinder((cx + dx, cy + dy, z), 0.13, 1.5, "bronze", sides=6)
    g.cylinder((cx + 0.3, cy, z + 2.5), 0.32, 1.3, "bronze", sides=8, radius_top=0.24)
    g.sphere((cx + 0.3, cy, z + 4.0), 0.22, "bronze", rings=5, sides=8)
    # The flag he carries.
    g.cylinder((cx + 0.1, cy + 0.4, z + 2.6), 0.04, 3.6, "bronze", sides=5)
    for k, mat in enumerate(("flag_blue", "flag_white", "flag_blue")):
        g.quad((cx + 0.1, cy + 0.4, z + 6.0 - k * 0.45), (cx - 1.8, cy + 0.4, z + 5.9 - k * 0.45),
               (cx - 1.8, cy + 0.4, z + 5.45 - k * 0.45), (cx + 0.1, cy + 0.4, z + 5.55 - k * 0.45), mat)
    col = Geo()
    col.box((cx, cy, SIDEWALK_TOP + 2.5), (5.5, 2.8, 5.0), "collision")
    col.box((cx, cy, SIDEWALK_TOP + 0.3), (8.0, 5.0, 0.6), "collision")
    chunks.add(cx, cy, (0, 1, 2), g)
    chunks.add(cx, cy, ("col",), col)


# --- Street furniture ---------------------------------------------------------------------------------

def lamp(x, y, z, g):
    g.cylinder((x, y, z), 0.14, 0.6, "iron", sides=8)
    g.cylinder((x, y, z + 0.6), 0.07, 3.6, "iron", sides=6)
    g.box((x, y, z + 4.45), (0.42, 0.42, 0.6), "lamp_glass")
    g.box((x, y, z + 4.8), (0.55, 0.55, 0.1), "iron")


def bench(x, y, z, yaw, g):
    g.box((x, y, z + 0.45), (1.8, 0.5, 0.06), "wood", yaw)
    c, s = math.cos(yaw), math.sin(yaw)
    g.box((x - s * 0.24, y + c * 0.24, z + 0.75), (1.8, 0.05, 0.4), "wood", yaw)
    for d in (-0.75, 0.75):
        g.box((x + c * d, y + s * d, z + 0.22), (0.06, 0.5, 0.44), "iron", yaw)


def palm(x, y, z, rng, g, col):
    h = rng.uniform(9.0, 13.0)
    lean = (rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4))
    segs = 6
    for i in range(segs):
        t0, t1 = i / segs, (i + 1) / segs
        p0 = (x + lean[0] * t0 * t0, y + lean[1] * t0 * t0, z + h * t0)
        g.cylinder(p0, 0.4 - 0.12 * t0, h / segs, "palm_trunk", sides=8, top=False, radius_top=0.4 - 0.12 * t1)
    top = (x + lean[0], y + lean[1], z + h)
    g.sphere(top, 0.55, "palm_trunk", rings=4, sides=6)
    fronds = 14
    for k in range(fronds):
        ang = 2 * math.pi * k / fronds + rng.uniform(-0.15, 0.15)
        droop = rng.uniform(0.3, 0.9)
        length = rng.uniform(3.6, 4.6)
        dx, dy = math.cos(ang), math.sin(ang)
        px, py = -dy, dx
        prev = None
        steps = 5
        for sidx in range(steps + 1):
            t = sidx / steps
            cxp = top[0] + dx * length * t
            cyp = top[1] + dy * length * t
            czp = top[2] + 0.6 * t - droop * length * t * t
            w = 0.75 * math.sin(math.pi * min(t * 1.1, 1.0)) + 0.05
            left = (cxp + px * w, cyp + py * w, czp - 0.1 * w)
            right = (cxp - px * w, cyp - py * w, czp - 0.1 * w)
            mid = (cxp, cyp, czp + 0.05)
            if prev:
                # Two-sided: each half-blade drawn in both windings.
                g.quad(prev[0], prev[1], mid, left, "palm_leaf")
                g.quad(prev[1], prev[2], right, mid, "palm_leaf")
                g.quad(left, mid, prev[1], prev[0], "palm_leaf")
                g.quad(mid, right, prev[2], prev[1], "palm_leaf")
            prev = (left, mid, right)
    col.cylinder((x, y, z), 0.4, h * 0.6, "collision", sides=6)


def car(x, y, yaw, color_index, g, col):
    mat = f"car_{color_index}"
    c, s = math.cos(yaw), math.sin(yaw)

    def p(dx, dy):
        return (x + dx * c - dy * s, y + dx * s + dy * c)

    g.box((*p(0, 0), 0.65), (4.3, 1.75, 0.7), mat, yaw)
    g.box((*p(-0.25, 0), 1.25), (2.3, 1.6, 0.55), mat, yaw)
    g.box((*p(-0.25, 0), 1.25), (2.32, 1.5, 0.45), "glass", yaw)
    for dx in (-1.35, 1.35):
        for dy in (-0.82, 0.82):
            q = p(dx, dy)
            g.box((q[0], q[1], 0.33), (0.66, 0.24, 0.66), "tyre", yaw)
    col.box((*p(0, 0), 0.8), (4.3, 1.8, 1.6), "collision", yaw)


def jersey(x, y, yaw, g, col):
    g.box((x, y, 0.2), (2.0, 0.6, 0.4), "jersey", yaw)
    g.box((x, y, 0.6), (2.0, 0.3, 0.5), "jersey", yaw)
    col.box((x, y, 0.45), (2.0, 0.6, 0.9), "collision", yaw)


def fence_run(a, b, g, col, height=2.2):
    """Police barrier line (the plaza's famous fences), blue-grey panels."""
    length = math.dist(a, b)
    if length < 0.5:
        return
    yaw = math.atan2(b[1] - a[1], b[0] - a[0])
    ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    panels = max(1, int(length / 2.5))
    pl = length / panels
    for i in range(panels):
        cx, cy = a[0] + ux * (i + 0.5) * pl, a[1] + uy * (i + 0.5) * pl
        g.box((cx, cy, height - 0.05), (pl - 0.1, 0.06, 0.08), "fence_blue", yaw)
        g.box((cx, cy, 0.35), (pl - 0.1, 0.06, 0.08), "fence_blue", yaw)
        for k in range(int(pl / 0.15)):
            t = -pl / 2 + 0.1 + k * 0.15
            g.box((cx + ux * t, cy + uy * t, height / 2 + 0.15), (0.025, 0.025, height - 0.3), "fence_blue", yaw)
        g.box((cx - ux * pl / 2, cy - uy * pl / 2, height / 2), (0.08, 0.5, height), "fence_blue", yaw)
    col.box(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, 1.6), (length, 0.3, 3.2), "collision", yaw)


def flagpole(x, y, g, col):
    g.box((x, y, 0.6), (2.2, 2.2, 1.2), "pbr_marble_01@trim")
    g.cylinder((x, y, 1.2), 0.18, 26.0, "paint_white", sides=10, radius_top=0.08)
    g.sphere((x, y, 27.3), 0.25, "bronze", rings=4, sides=8)
    for k, mat in enumerate(("flag_blue", "flag_white", "flag_blue")):
        z = 26.5 - k * 1.1
        g.quad((x, y, z), (x + 5.0, y + 0.3, z - 0.2), (x + 5.0, y + 0.3, z - 1.3), (x, y, z - 1.1), mat)
        g.quad((x, y, z - 1.1), (x + 5.0, y + 0.3, z - 1.3), (x + 5.0, y + 0.3, z - 0.2), (x, y, z), mat)
    col.cylinder((x, y, 0), 1.2, 1.2, "collision", sides=6)
    col.cylinder((x, y, 1.2), 0.2, 20.0, "collision", sides=6)


# --- Blender output ------------------------------------------------------------------------------------

_materials = {}


def material(name):
    if name in _materials:
        return _materials[name]
    spec = MATERIALS.get(name)
    if spec is None and name.startswith("car_"):
        rgb = CAR_COLORS[int(name[4:]) % len(CAR_COLORS)]
        spec = (None, rgb, 0.3, 0.55, 1.0)
    if spec is None:
        spec = (None, (0.5, 0.5, 0.5), 0.9, 0.0, 1.0)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*spec[1], 1.0)
    bsdf.inputs["Roughness"].default_value = spec[2]
    bsdf.inputs["Metallic"].default_value = spec[3]
    _materials[name] = mat
    return mat


def tile_of(name):
    spec = MATERIALS.get(name)
    return spec[4] if spec else 1.0


def to_object(name, geo):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(geo.verts, [], [f for f, _ in geo.faces])
    mesh.update()
    names = sorted({m for _, m in geo.faces})
    for n in names:
        mesh.materials.append(material(n))
    index = {n: i for i, n in enumerate(names)}
    uv = mesh.uv_layers.new(name="UVMap")
    for poly, (_, mname) in zip(mesh.polygons, geo.faces):
        poly.material_index = index[mname]
        tile = tile_of(mname)
        nx, ny, nz = poly.normal
        if abs(nz) > 0.7:
            for li in poly.loop_indices:
                co = mesh.vertices[mesh.loops[li].vertex_index].co
                uv.data[li].uv = (co.x / tile, co.y / tile)
        else:
            tx, ty = -ny, nx
            lt = math.hypot(tx, ty) or 1.0
            tx, ty = tx / lt, ty / lt
            for li in poly.loop_indices:
                co = mesh.vertices[mesh.loops[li].vertex_index].co
                uv.data[li].uv = ((co.x * tx + co.y * ty) / tile, co.z / tile)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def export(name, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    OUTPUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT / f"{name}.glb"), export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_normals=True, export_texcoords=True,
                              export_materials="EXPORT", export_image_format="NONE")


def main():
    layout = json.loads(LAYOUT.read_text())
    chunks = Chunks(layout["bounds"])
    rng = random.Random(1810)
    build_ground(layout, chunks)

    polys = [simplify(b["polygon"]) for b in layout["buildings"]]
    landmarks = {"casa_rosada": casa_rosada, "cabildo": cabildo, "catedral": catedral, "piramide": piramide,
                 "belgrano": belgrano}
    for i, b in enumerate(layout["buildings"]):
        others = [p for j, p in enumerate(polys) if j != i]
        role = b.get("role", "")
        if role in landmarks:
            landmarks[role](b, chunks)
        else:
            building(b, others, chunks, random.Random(hash(b["id"]) & 0xFFFF))

    # Street furniture from OSM points; trees become scenery (the vegetation models), palms are built here.
    trees = []
    plaza = layout["plaza"]
    for pt in layout["points"]:
        x, y = pt["position"]
        in_plaza = in_poly((x, y), plaza)
        on_building = any(in_poly((x, y), p) for p in polys)
        if on_building:
            continue
        z = SIDEWALK_TOP
        g, col = Geo(), Geo()
        kind = pt["kind"]
        if kind == "lamp":
            lamp(x, y, z, g)
            col.box((x, y, z + 2.0), (0.3, 0.3, 4.0), "collision")
        elif kind == "bench":
            bench(x, y, z, 0.0 if rng.random() < 0.5 else math.pi / 2, g)
            col.box((x, y, z + 0.45), (1.8, 0.6, 0.9), "collision")
        elif kind in ("tree", "palm"):
            if in_plaza and kind == "tree" and rng.random() < 0.6 or kind == "palm":
                palm(x, y, z, rng, g, col)
            else:
                trees.append((x, y))
                continue
        elif kind == "subway":
            for d, (sx, sy) in enumerate(((0, 1.6), (0, -1.6), (-2.6, 0))):
                g.box((x + sx, y + sy, z + 0.55), (5.2 if d < 2 else 0.06, 0.06 if d < 2 else 3.2, 1.1), "iron")
            g.quad((x - 2.6, y - 1.55, z + 0.01), (x + 2.6, y - 1.55, z + 0.01), (x + 2.6, y + 1.55, z + 0.01), (x - 2.6, y + 1.55, z + 0.01), "dark")
            col.box((x, y, z + 0.6), (5.2, 3.2, 1.2), "collision")
        elif kind == "bus_stop":
            g.box((x, y, z + 2.4), (3.6, 1.6, 0.1), "bus_roof")
            g.box((x, y - 0.75, z + 1.2), (3.6, 0.04, 2.3), "glass")
            for dx in (-1.75, 1.75):
                g.box((x + dx, y - 0.75, z + 1.2), (0.08, 0.08, 2.4), "iron")
            col.box((x, y - 0.75, z + 1.2), (3.6, 0.2, 2.4), "collision")
        elif kind == "fountain":
            g.cylinder((x, y, z), 1.6, 0.6, "pbr_marble_01@trim", sides=14)
            col.cylinder((x, y, z), 1.6, 0.6, "collision", sides=8)
        else:
            continue
        chunks.add(x, y, (0, 1), g)
        chunks.add(x, y, ("col",), col)

    # The big flag of the plaza, near the Casa Rosada side.
    g, col = Geo(), Geo()
    flagpole(layout["plaza_half"][0] - 30.0, 0.0, g, col)
    chunks.add(70.0, 0.0, (0, 1, 2), g)
    chunks.add(70.0, 0.0, ("col",), col)

    # Cover in the streets around the plaza: parked and abandoned cars, concrete barriers, and the
    # police fence line across the plaza (as during demonstrations).
    hx, hy = layout["plaza_half"]
    street_y = (hy + 9.0, -hy - 9.0)
    for side in street_y:
        x = -hx + 6.0
        while x < hx - 6.0:
            x += rng.uniform(7.0, 16.0)
            g, col = Geo(), Geo()
            if rng.random() < 0.65:
                car(x, side + rng.uniform(-3.0, 3.0), rng.uniform(-0.25, 0.25) + (0 if rng.random() < 0.5 else math.pi), rng.randrange(6), g, col)
            else:
                jersey(x, side + rng.uniform(-3.0, 3.0), rng.uniform(-0.5, 0.5), g, col)
            chunks.add(x, side, (0, 1, 2), g)
            chunks.add(x, side, ("col",), col)
    for sx in (-hx - 9.0, hx + 9.0):
        y = -hy
        while y < hy:
            y += rng.uniform(9.0, 18.0)
            g, col = Geo(), Geo()
            car(sx + rng.uniform(-2.5, 2.5), y, math.pi / 2 + rng.uniform(-0.3, 0.3), rng.randrange(6), g, col)
            chunks.add(sx, y, (0, 1, 2), g)
            chunks.add(sx, y, ("col",), col)
    g, col = Geo(), Geo()
    for y0, y1 in ((-hy + 4.0, -14.0), (14.0, hy - 4.0)):
        fence_run((hx * 0.45, y0), (hx * 0.45, y1), g, col)
    chunks.add(hx * 0.45, 0.0, (0, 1, 2), g)
    chunks.add(hx * 0.45, 0.0, ("col",), col)

    # Map boundary: police barriers all around (streets leaving the map are closed off).
    b = layout["bounds"]
    corners = [(b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3])]
    for i in range(4):
        a, c = corners[i], corners[(i + 1) % 4]
        steps = max(1, int(math.dist(a, c) / 20.0))
        for k in range(steps):
            p0 = (a[0] + (c[0] - a[0]) * k / steps, a[1] + (c[1] - a[1]) * k / steps)
            p1 = (a[0] + (c[0] - a[0]) * (k + 1) / steps, a[1] + (c[1] - a[1]) * (k + 1) / steps)
            g, col = Geo(), Geo()
            fence_run(p0, p1, g, col, height=2.4)
            col.box(((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, 6.0), (math.dist(p0, p1), 0.4, 12.0), "collision",
                    math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
            chunks.add((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (0, 1, 2), g)
            chunks.add((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, ("col",), col)

    # Export every chunk.
    for old in OUTPUT.glob("chunk_*.glb"):
        old.unlink()
    scenery = []
    for (kx, ky), levels in sorted(chunks.data.items()):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        _materials.clear()
        name = f"chunk_{kx:02d}_{ky:02d}"
        objects = []
        for level in (0, 1, 2):
            geo = levels.get(level)
            if geo and geo.faces:
                objects.append(to_object(f"{name}_LOD{level}", geo))
        if levels.get("col") and levels["col"].faces:
            objects.append(to_object(f"{name}_COL", levels["col"]))
        if not objects:
            continue
        export(name, objects)
        tris = sum(len(f) - 2 for f, _ in levels.get(0, Geo()).faces)
        print(f"[origon] {name}: {tris} triangles (LOD0)")
        scenery.append({"model": f"maps/plaza_de_mayo/{name}", "pos": [0.0, 0.0, 0.0]})

    for x, y in trees:
        scenery.append({"model": "vegetation/maple", "pos": [round(x, 2), SIDEWALK_TOP, round(-y, 2)],
                        "yaw": rng.uniform(0, 360), "scale": round(rng.uniform(0.8, 1.05), 2), "trunk": 0.35, "trunk_height": 4.0})

    # Spawns: open ground in the plaza and on the surrounding sidewalks, facing the plaza center.
    spawns = []
    candidates = []
    for gx in range(-int(hx) + 8, int(hx) - 8, 9):
        for gy in (-int(hy) + 6, -12, 12, int(hy) - 6):
            candidates.append((gx, gy, SIDEWALK_TOP))
    for gx in range(int(b[0]) + 20, int(b[2]) - 20, 22):
        for gy in (hy + 9.0, -hy - 9.0):
            candidates.append((gx, gy, 0.0))
    for gy in range(int(b[1]) + 20, int(b[3]) - 20, 22):
        for gx in (-hx - 9.0, hx + 9.0):
            candidates.append((gx, gy, 0.0))
    occupied = [p for p in polys]
    for x, y, z in candidates:
        if any(in_poly((x, y), p) for p in occupied):
            continue
        if any(math.dist((x, y), pt["position"]) < 2.0 for pt in layout["points"]):
            continue
        yaw = math.degrees(math.atan2(x, -y))  # Engine yaw: 0 looks toward -Z (north here).
        spawns.append({"pos": [round(x, 2), z + 0.1, round(-y, 2)], "yaw": round((yaw + 180.0) % 360.0 - 180.0, 1)})
    rng.shuffle(spawns)
    spawns = spawns[:32]

    map_json = {
        "version": 1,
        "name": "Plaza de Mayo",
        "environment": {"sun_direction": [0.55, 0.62, -0.42], "sun_color": [1.0, 0.92, 0.8], "sun_intensity": 3.4,
                        "sky_color": [0.5, 0.66, 0.86], "ground_color": [0.3, 0.29, 0.27], "ambient_intensity": 0.6,
                        "fog_color": [0.72, 0.78, 0.86], "fog_density": 0.0006},
        "credits": "Footprints and street furniture: OpenStreetMap contributors (ODbL).",
        "blocks": [],
        "scenery": scenery,
        "spawns": spawns,
        "lights": [],
    }
    MAP_FILE.write_text(json.dumps(map_json, indent=1, ensure_ascii=False) + "\n")
    print(f"[origon] {len(scenery)} scenery entries, {len(spawns)} spawns -> {MAP_FILE}")


main()
