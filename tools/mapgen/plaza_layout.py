#!/usr/bin/env python3
"""Plaza de Mayo layout from OpenStreetMap data (ODbL, (c) OpenStreetMap contributors).

    python3 tools/mapgen/plaza_layout.py [--fetch] [--preview out.png]

Reads the Overpass extracts cached in .cache/osm/ (--fetch downloads them) and writes
.cache/osm/plaza_layout.json in local meters: origin at the plaza center, X along the plaza's long
axis (toward the Casa Rosada, east), Y toward the north (Rivadavia side). Contents:
  - plaza: outer polygon of the Plaza de Mayo, grass beds and walkways inside it,
  - buildings: footprint polygons with height, name and role (landmark ids for the special ones),
  - blocks: convex hulls of the building clusters (city blocks) that sidewalks wrap,
  - streets: centerlines with a width class,
  - points: trees, lamps, benches, bus stops, subway entrances inside the map,
  - bounds: the playable rectangle (the plaza plus one block on every side).
tools/blender/gen_plaza.py turns it into geometry.
"""
import argparse
import json
import math
import pathlib
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache" / "osm"
BBOX = (-34.6125, -58.3780, -34.6040, -58.3660)  # south, west, north, east
LAT0, LON0 = -34.6083, -58.3722
PLAZA_RELATION = 17076039
# One block beyond the plaza edge on every side (street + block depth), meters.
RING = (150.0, 150.0)

LANDMARKS = {
    ("relation", 2337969): "casa_rosada",
    ("way", 293947112): "cabildo",
    ("way", 265344159): "catedral",
    ("way", 720034699): "piramide",
    ("way", 720034722): "belgrano",
    ("relation", 3527701): "banco_nacion",
}
STREET_WIDTH = {"motorway": 22.0, "primary": 18.0, "primary_link": 10.0, "secondary": 14.0, "tertiary": 12.0,
                "residential": 10.0, "living_street": 9.0, "pedestrian": 9.0, "service": 7.0, "unclassified": 10.0}


def overpass(query, path):
    data = urllib.parse.urlencode({"data": query}).encode()
    request = urllib.request.Request("https://overpass-api.de/api/interpreter", data=data,
                                     headers={"User-Agent": "origon-asset-fetch"})
    with urllib.request.urlopen(request, timeout=180) as response:
        path.write_bytes(response.read())


def fetch():
    CACHE.mkdir(parents=True, exist_ok=True)
    s, w, n, e = BBOX
    box = f"{s},{w},{n},{e}"
    overpass(f'[out:json][timeout:90];(way({box});relation["building"]({box});relation["type"="multipolygon"]({box}););'
             '(._;>;);out body;', CACHE / "plaza.json")
    overpass(f'[out:json][timeout:90];node({box})[~"."~"."];out body;', CACHE / "plaza_nodes.json")


def project(lat, lon):
    return ((lon - LON0) * 111320.0 * math.cos(math.radians(LAT0)), (lat - LAT0) * 110574.0)


def area(poly):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1]))


def centroid(poly):
    return (sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))


def convex_hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def ring_from_ways(ways, nodes):
    """Joins way segments (node id lists) into closed rings."""
    segments = [list(w) for w in ways if len(w) >= 2]
    rings = []
    while segments:
        ring = segments.pop(0)
        changed = True
        while ring[0] != ring[-1] and changed:
            changed = False
            for i, seg in enumerate(segments):
                if seg[0] == ring[-1]:
                    ring += seg[1:]
                elif seg[-1] == ring[-1]:
                    ring += seg[-2::-1]
                elif seg[-1] == ring[0]:
                    ring = seg[:-1] + ring
                elif seg[0] == ring[0]:
                    ring = seg[:0:-1] + ring
                else:
                    continue
                segments.pop(i)
                changed = True
                break
        rings.append([nodes[n] for n in ring if n in nodes])
    return rings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--preview")
    args = parser.parse_args()
    if args.fetch or not (CACHE / "plaza.json").exists():
        fetch()
    data = json.loads((CACHE / "plaza.json").read_text())["elements"]
    node_data = json.loads((CACHE / "plaza_nodes.json").read_text())["elements"]
    raw_nodes = {e["id"]: project(e["lat"], e["lon"]) for e in data if e["type"] == "node"}
    ways = {e["id"]: e for e in data if e["type"] == "way"}
    relations = {e["id"]: e for e in data if e["type"] == "relation"}

    # Frame: plaza principal axis along X, plaza bounding-box center at the origin.
    plaza_rel = relations[PLAZA_RELATION]
    plaza_rings = ring_from_ways([ways[m["ref"]]["nodes"] for m in plaza_rel["members"] if m["type"] == "way" and m["role"] == "outer"],
                                 raw_nodes)
    plaza_raw = max(plaza_rings, key=lambda r: abs(area(r)))
    cx, cy = centroid(plaza_raw)
    sxx = sum((p[0] - cx) ** 2 for p in plaza_raw)
    syy = sum((p[1] - cy) ** 2 for p in plaza_raw)
    sxy = sum((p[0] - cx) * (p[1] - cy) for p in plaza_raw)
    angle = 0.5 * math.atan2(2 * sxy, sxx - syy)
    ca, sa = math.cos(-angle), math.sin(-angle)

    def local(p):
        x, y = p[0] - cx, p[1] - cy
        return (x * ca - y * sa, x * sa + y * ca)

    plaza = [local(p) for p in plaza_raw]
    minx, maxx = min(p[0] for p in plaza), max(p[0] for p in plaza)
    miny, maxy = min(p[1] for p in plaza), max(p[1] for p in plaza)
    ox, oy = (minx + maxx) / 2, (miny + maxy) / 2

    def to_map(p):
        q = local(p)
        return (round(q[0] - ox, 2), round(q[1] - oy, 2))

    nodes = {i: to_map(p) for i, p in raw_nodes.items()}
    plaza = [to_map(p) for p in plaza_raw]
    half = ((maxx - minx) / 2, (maxy - miny) / 2)
    bounds = [-half[0] - RING[0], -half[1] - RING[1], half[0] + RING[0], half[1] + RING[1]]

    def inside(p, margin=0.0):
        return bounds[0] - margin <= p[0] <= bounds[2] + margin and bounds[1] - margin <= p[1] <= bounds[3] + margin

    # Buildings (ways and multipolygon relations), kept when their centroid is in the map.
    buildings = []

    def add_building(kind, ident, tags, rings):
        outer = max(rings, key=lambda r: abs(area(r))) if rings else None
        if not outer or len(outer) < 4:
            return
        if outer[0] == outer[-1]:
            outer = outer[:-1]
        if area(outer) < 0:
            outer = outer[::-1]
        if not inside(centroid(outer)):
            return
        height = None
        try:
            height = float(str(tags.get("height", "")).replace("m", "").strip())
        except ValueError:
            pass
        levels = tags.get("building:levels")
        if height is None and levels:
            try:
                height = float(levels) * 3.6 + 1.0
            except ValueError:
                pass
        buildings.append({"id": f"{kind}/{ident}", "name": tags.get("name", ""), "polygon": outer,
                          "height": height, "levels": levels, "role": LANDMARKS.get((kind, ident), ""),
                          "colour": tags.get("building:colour", ""), "type": tags.get("building", "yes"),
                          "historic": tags.get("historic", "")})

    member_ways = set()
    for ident, rel in relations.items():
        tags = rel.get("tags", {})
        if "building" in tags or (("relation", ident) in LANDMARKS):
            outers = [ways[m["ref"]]["nodes"] for m in rel["members"] if m["type"] == "way" and m["role"] == "outer" and m["ref"] in ways]
            member_ways.update(m["ref"] for m in rel["members"] if m["type"] == "way")
            add_building("relation", ident, tags, ring_from_ways(outers, nodes))
    for ident, way in ways.items():
        tags = way.get("tags", {})
        if ("building" in tags or ("way", ident) in LANDMARKS) and tags.get("building") != "ship" and ident not in member_ways:
            if "building:part" in tags:
                continue
            add_building("way", ident, tags, [[nodes[n] for n in way["nodes"] if n in nodes]])

    # Blocks: clusters of buildings closer than 4 m (bounding boxes), wrapped by their convex hull.
    def bbox(poly):
        return (min(p[0] for p in poly), min(p[1] for p in poly), max(p[0] for p in poly), max(p[1] for p in poly))

    parent = list(range(len(buildings)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    boxes = [bbox(b["polygon"]) for b in buildings]
    for i in range(len(buildings)):
        for j in range(i + 1, len(buildings)):
            a, b = boxes[i], boxes[j]
            if a[0] - 4 <= b[2] and b[0] - 4 <= a[2] and a[1] - 4 <= b[3] and b[1] - 4 <= a[3]:
                # Boxes overlap: check real vertex proximity (diagonal streets make boxes overlap).
                near = any(math.dist(p, q) < 6.0 for p in buildings[i]["polygon"] for q in buildings[j]["polygon"])
                if near or all(b[k] >= a[k] for k in (0, 1)) and all(b[k] <= a[k] for k in (2, 3)):
                    parent[find(i)] = find(j)
    clusters = {}
    for i in range(len(buildings)):
        clusters.setdefault(find(i), []).append(i)
    blocks = []
    for members in clusters.values():
        hull = convex_hull([p for i in members for p in buildings[i]["polygon"]])
        for i in members:
            buildings[i]["block"] = len(blocks)
        blocks.append({"hull": [(round(x, 2), round(y, 2)) for x, y in hull], "buildings": members})

    # Plaza interior: grass beds and walkways inside the plaza polygon.
    def in_poly(p, poly):
        x, y = p
        c = False
        for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
            if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
                c = not c
        return c

    grass, walkways, streets = [], [], []
    for ident, way in ways.items():
        tags = way.get("tags", {})
        pts = [nodes[n] for n in way["nodes"] if n in nodes]
        if len(pts) < 2:
            continue
        mid = centroid(pts)
        if tags.get("landuse") in ("grass", "greenery") or tags.get("leisure") == "garden":
            if in_poly(mid, plaza) and len(pts) >= 4:
                grass.append(pts[:-1] if pts[0] == pts[-1] else pts)
        hw = tags.get("highway")
        if hw in ("footway", "pedestrian", "path") and in_poly(mid, plaza):
            walkways.append(pts)
        elif hw in STREET_WIDTH and any(inside(p, 30.0) for p in pts) and tags.get("tunnel") != "yes" and tags.get("layer", "0") in ("0", ""):
            streets.append({"name": tags.get("name", ""), "kind": hw, "width": STREET_WIDTH[hw], "points": pts,
                            "oneway": tags.get("oneway", "no")})

    points = []
    for e in node_data:
        tags = e.get("tags", {})
        p = to_map(project(e["lat"], e["lon"]))
        if not inside(p):
            continue
        kind = None
        if tags.get("natural") == "tree":
            kind = "palm" if "palm" in (tags.get("species", "") + tags.get("genus", "") + tags.get("leaf_type", "")).lower() or \
                tags.get("genus") in ("Phoenix", "Washingtonia", "Syagrus") else "tree"
        elif tags.get("highway") == "street_lamp":
            kind = "lamp"
        elif tags.get("amenity") == "bench":
            kind = "bench"
        elif tags.get("highway") == "bus_stop":
            kind = "bus_stop"
        elif tags.get("railway") == "subway_entrance":
            kind = "subway"
        elif tags.get("amenity") == "fountain":
            kind = "fountain"
        elif tags.get("amenity") == "waste_basket":
            kind = "bin"
        if kind:
            points.append({"kind": kind, "position": p, "name": tags.get("name", ""), "genus": tags.get("genus", "")})

    layout = {"source": "OpenStreetMap contributors (ODbL), extracted " + json.loads((CACHE / "plaza.json").read_text())["osm3s"]["timestamp_osm_base"],
              "rotation_degrees": round(math.degrees(angle), 3), "plaza": plaza, "plaza_half": [round(h, 2) for h in half],
              "bounds": [round(b, 2) for b in bounds], "buildings": buildings, "blocks": blocks, "grass": grass,
              "walkways": walkways, "streets": streets, "points": points}
    (CACHE / "plaza_layout.json").write_text(json.dumps(layout))
    print(f"rotation {math.degrees(angle):.2f} deg, plaza {2 * half[0]:.0f} x {2 * half[1]:.0f} m, bounds {bounds}")
    print(f"{len(buildings)} buildings in {len(blocks)} blocks, {len(streets)} streets, {len(grass)} grass beds, "
          f"{len(walkways)} walkways, {len(points)} points")
    for b in buildings:
        if b["role"]:
            print(f"  landmark {b['role']}: {b['name']} h={b['height']}")

    if args.preview:
        from PIL import Image, ImageDraw
        scale = 2.0
        w = int((bounds[2] - bounds[0] + 80) * scale)
        h = int((bounds[3] - bounds[1] + 80) * scale)
        img = Image.new("RGB", (w, h), (40, 40, 44))
        draw = ImageDraw.Draw(img)

        def px(p):
            return ((p[0] - bounds[0] + 40) * scale, (bounds[3] - p[1] + 40) * scale)

        for s in streets:
            draw.line([px(p) for p in s["points"]], fill=(90, 90, 96), width=int(s["width"] * scale))
        for blk in blocks:
            if len(blk["hull"]) >= 3:
                draw.polygon([px(p) for p in blk["hull"]], fill=(150, 145, 135))
        draw.polygon([px(p) for p in plaza], fill=(170, 165, 150))
        for g in grass:
            draw.polygon([px(p) for p in g], fill=(80, 130, 70))
        for wk in walkways:
            draw.line([px(p) for p in wk], fill=(200, 195, 180), width=3)
        for b in buildings:
            col = (220, 120, 110) if b["role"] else (110, 105, 100)
            draw.polygon([px(p) for p in b["polygon"]], fill=col, outline=(30, 30, 30))
        colors = {"tree": (40, 160, 40), "palm": (200, 220, 60), "lamp": (255, 230, 120), "bench": (160, 100, 60),
                  "bus_stop": (60, 120, 255), "subway": (255, 60, 200), "fountain": (60, 200, 255), "bin": (120, 120, 120)}
        for pt in points:
            x, y = px(pt["position"])
            draw.ellipse([x - 3, y - 3, x + 3, y + 3], fill=colors[pt["kind"]])
        draw.rectangle([px((bounds[0], bounds[3])), px((bounds[2], bounds[1]))], outline=(255, 200, 60), width=2)
        img.save(args.preview)


if __name__ == "__main__":
    main()
