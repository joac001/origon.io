"""Procedural forest assets for Origon, generated headless in Blender.

Run:
    blender --background --factory-startup --python tools/blender/gen_vegetation.py -- [asset ...]

Writes assets/models/vegetation/<name>.glb. Each file holds the nodes
<name>_LOD0, <name>_LOD1, <name>_LOD2 and optionally <name>_COL (collision mesh).
Every generator is deterministic (fixed seeds), so re-running gives the same files.
Textures are not embedded: the engine resolves them from the material name
(bark_<id> / rock_<id> -> assets/textures/<id>/, foliage_<name> -> assets/textures/foliage/<name>.png).
Bark and rock materials use the CC0 textures fetched by tools/fetch_assets.py;
foliage cards use the textures from tools/textures/gen_foliage.py.
"""
import math
import pathlib
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector, noise

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEXTURES = ROOT / "assets" / "textures"
OUTPUT = ROOT / "assets" / "models" / "vegetation"


# --- Scene and materials ---------------------------------------------------------------------

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def image(path, color=True):
    img = bpy.data.images.load(str(path), check_existing=True)
    if not color:
        img.colorspace_settings.name = "Non-Color"
    return img


_materials = {}


def material(name, texture_id=None, foliage=None, tint=(1, 1, 1)):
    """PBR material. texture_id: folder in assets/textures with albedo/normal/arm jpgs.
    foliage: name of an RGBA card texture in assets/textures/foliage."""
    if name in _materials:
        return _materials[name]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Specular IOR Level"].default_value = 0.3
    if foliage:
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = image(TEXTURES / "foliage" / f"{foliage}.png")
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        bsdf.inputs["Roughness"].default_value = 0.7
        mat.use_backface_culling = False
        mat.blend_method = "CLIP"
    elif texture_id:
        folder = TEXTURES / texture_id
        albedo = nodes.new("ShaderNodeTexImage")
        albedo.image = image(folder / "albedo.jpg")
        if tint != (1, 1, 1):
            mix = nodes.new("ShaderNodeMix")
            mix.data_type = "RGBA"
            mix.blend_type = "MULTIPLY"
            mix.inputs["Factor"].default_value = 1.0
            mix.inputs[7].default_value = (*tint, 1.0)
            links.new(albedo.outputs["Color"], mix.inputs[6])
            links.new(mix.outputs[2], bsdf.inputs["Base Color"])
        else:
            links.new(albedo.outputs["Color"], bsdf.inputs["Base Color"])
        normal_tex = nodes.new("ShaderNodeTexImage")
        normal_tex.image = image(folder / "normal.jpg", color=False)
        normal_map = nodes.new("ShaderNodeNormalMap")
        links.new(normal_tex.outputs["Color"], normal_map.inputs["Color"])
        links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
        arm = nodes.new("ShaderNodeTexImage")
        arm.image = image(folder / "arm.jpg", color=False)
        separate = nodes.new("ShaderNodeSeparateColor")
        links.new(arm.outputs["Color"], separate.inputs["Color"])
        links.new(separate.outputs["Green"], bsdf.inputs["Roughness"])
        links.new(separate.outputs["Blue"], bsdf.inputs["Metallic"])
    _materials[name] = mat
    return mat


# --- Mesh building helpers ---------------------------------------------------------------------

class Builder:
    """Accumulates geometry into one bmesh with per-face material slots."""

    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.materials = []
        self.normal_overrides = {}  # vertex -> custom normal (foliage)

    def slot(self, mat):
        if mat not in self.materials:
            self.materials.append(mat)
        return self.materials.index(mat)

    def face(self, verts, uvs, mat, normal_hint=None):
        bm_verts = [self.bm.verts.new(v) for v in verts]
        f = self.bm.faces.new(bm_verts)
        f.material_index = self.slot(mat)
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
        if normal_hint is not None:
            for v in bm_verts:
                self.normal_overrides[v] = normal_hint(v.co)
        return f

    def tube(self, path, radii, mat, sides=8, uv_repeat=2.0, uv_height=2.0, cap_top=False):
        """Generalized cylinder along a polyline (list of Vectors) with per-point radius."""
        rings = []
        length = 0.0
        prev = None
        for i, (p, r) in enumerate(zip(path, radii)):
            if prev is not None:
                length += (p - prev).length
            prev = p
            tangent = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
            helper = Vector((0, 0, 1)) if abs(tangent.z) < 0.9 else Vector((1, 0, 0))
            side = tangent.cross(helper).normalized()
            up = side.cross(tangent).normalized()
            ring = []
            for s in range(sides + 1):
                a = 2 * math.pi * s / sides
                ring.append((p + (side * math.cos(a) + up * math.sin(a)) * r, s / sides * uv_repeat, length / uv_height))
            rings.append(ring)
        for a, b in zip(rings, rings[1:]):
            for s in range(sides):
                verts = [a[s][0], a[s + 1][0], b[s + 1][0], b[s][0]]
                uvs = [(a[s][1], a[s][2]), (a[s + 1][1], a[s][2]), (b[s + 1][1], b[s][2]), (b[s][1], b[s][2])]
                f = self.face(verts, uvs, mat)
                f.smooth = True
        if cap_top:
            top = rings[-1]
            center = path[-1]
            for s in range(sides):
                self.face([top[s][0], top[s + 1][0], center], [(0.5, 0.5), (0.6, 0.5), (0.5, 0.6)], mat)

    def card(self, origin, axis_u, axis_v, mat, segments=1, bend=0.0, normal_hint=None, uv_rect=(0, 0, 1, 1)):
        """Quad strip from origin along axis_v (length = |axis_v|), width axis_u, bending down along v."""
        u0, v0, u1, v1 = uv_rect
        down = Vector((0, 0, -1))
        rows = []
        for i in range(segments + 1):
            t = i / segments
            center = origin + axis_v * t + down * bend * t * t
            rows.append((center - axis_u * 0.5, center + axis_u * 0.5, v0 + (v1 - v0) * t))
        for a, b in zip(rows, rows[1:]):
            self.face([a[0], a[1], b[1], b[0]], [(u0, a[2]), (u1, a[2]), (u1, b[2]), (u0, b[2])], mat, normal_hint)

    def to_object(self, name, collection):
        mesh = bpy.data.meshes.new(name)
        overrides = self.normal_overrides
        # Store override normals by vertex index before the bmesh is freed.
        self.bm.verts.index_update()
        override_by_index = {v.index: n for v, n in overrides.items()}
        self.bm.to_mesh(mesh)
        self.bm.free()
        for mat in self.materials:
            mesh.materials.append(mat)
        if override_by_index:
            normals = []
            for v in mesh.vertices:
                normals.append(override_by_index.get(v.index, v.normal))
            mesh.normals_split_custom_set_from_vertices(normals)
        obj = bpy.data.objects.new(name, mesh)
        collection.objects.link(obj)
        return obj


def crown_normal(center, up_bias=0.6):
    def hint(co):
        n = (co - center)
        if n.length < 1e-4:
            n = Vector((0, 0, 1))
        n = n.normalized() + Vector((0, 0, up_bias))
        return n.normalized()
    return hint


# --- Generators -----------------------------------------------------------------------------------

def conifer(name, seed, height, trunk_radius, crown_start, branch_length, whorl_spacing, droop, bark, lod):
    """Spruce/pine/fir: whorls of drooping branches. Each branch is a woody twig with a main
    needle card and smaller side cards; an inner shell of cards around the trunk fills the crown
    so it never looks hollow; a few bare dead branches below the crown."""
    rng = random.Random(seed)
    b = Builder()
    bark_mat = material(f"bark_{bark}", bark)
    needles = material("foliage_needles", foliage="needles")
    sides = (12, 8, 5)[lod]
    steps = 10
    trunk_points = [Vector((0, 0, height * t)) + Vector((rng.uniform(-0.08, 0.08) * t, rng.uniform(-0.08, 0.08) * t, 0))
                    for t in [i / steps for i in range(steps + 1)]]
    radii = [trunk_radius * (1.25 - 1.15 * t) for t in [i / steps for i in range(steps + 1)]]
    radii[0] *= 1.35  # Root flare.
    radii[1] *= 1.08
    b.tube(trunk_points, radii, bark_mat, sides=sides, uv_repeat=2, uv_height=2.5)

    def trunk_at(z):
        t = max(0.0, min(1.0, z / height))
        i = min(int(t * steps), steps - 1)
        f = t * steps - i
        return trunk_points[i].lerp(trunk_points[i + 1], f)

    center = Vector((0, 0, height * (crown_start + 1) * 0.5))
    hint = crown_normal(center, 0.8)
    density = (1.0, 0.5, 0.25)[lod]
    spacing = whorl_spacing * (0.55, 0.9, 1.6)[lod]

    # Dead branches under the crown (bare twigs), only up close.
    if lod == 0:
        z = height * crown_start * 0.45
        while z < height * crown_start:
            for _ in range(rng.randint(1, 3)):
                a = rng.uniform(0, math.tau)
                d = Vector((math.cos(a), math.sin(a), rng.uniform(-0.4, 0.1))).normalized()
                start = trunk_at(z)
                length = rng.uniform(0.6, 1.6)
                b.tube([start, start + d * length], [0.03, 0.008], bark_mat, sides=4)
            z += rng.uniform(0.5, 1.0)

    z = height * crown_start
    while z < height * 0.97:
        t = (z - height * crown_start) / (height * (1 - crown_start))
        length = (branch_length * (1.0 - t) ** 0.85 + 0.45) * rng.uniform(0.85, 1.1)
        count = max(3, int(round(rng.randint(7, 9) * (0.5 + 0.5 * density))))
        phase = rng.uniform(0, math.tau)
        for k in range(count):
            if rng.random() < 0.12:
                continue  # Natural gaps.
            a = phase + math.tau * k / count + rng.uniform(-0.25, 0.25)
            sag = droop * rng.uniform(0.6, 1.3) * (0.6 + 0.6 * (1.0 - t))
            direction = Vector((math.cos(a), math.sin(a), -sag)).normalized()
            side = direction.cross(Vector((0, 0, 1))).normalized()
            start = trunk_at(z)
            l = length * rng.uniform(0.8, 1.15)
            width = l * 0.6
            if lod == 0:
                b.tube([start, start + direction * l * 0.85], [0.035 * (1.0 - t * 0.6), 0.01], bark_mat, sides=4)
            b.card(start, side * width, direction * l, needles, segments=(3, 2, 1)[lod], bend=l * 0.16, normal_hint=hint)
            if lod < 2:
                # Side sprays along the branch, tilted up and down for volume from every angle.
                for f, tilt in ((0.35, 0.9), (0.6, -0.7), (0.8, 0.5))[: (3 if lod == 0 else 1)]:
                    origin = start + direction * l * f + Vector((0, 0, -l * 0.16 * f * f))
                    up = (side * math.cos(tilt) + Vector((0, 0, 1)) * math.sin(tilt)).normalized()
                    sub_dir = (direction + side * rng.uniform(-0.5, 0.5)).normalized()
                    sub = l * rng.uniform(0.35, 0.5)
                    b.card(origin - sub_dir * sub * 0.15, up * sub * 0.8, sub_dir * sub, needles, bend=sub * 0.15,
                           normal_hint=hint)
        # Inner shell: vertical sprays around the trunk fill the crown interior.
        if lod < 2:
            shell = max(2, int(4 * density))
            for k in range(shell):
                a = rng.uniform(0, math.tau)
                out = Vector((math.cos(a), math.sin(a), 0))
                r = length * rng.uniform(0.2, 0.45)
                h = spacing * 2.2
                origin = trunk_at(z) + out * r + Vector((0, 0, -h * 0.3))
                b.card(origin, out.cross(Vector((0, 0, 1))) * h * 0.9, Vector((0, 0, h)) + out * 0.2, needles,
                       normal_hint=hint)
        z += spacing * rng.uniform(0.8, 1.2)
    # Leader and top tuft.
    top = trunk_at(height * 0.96)
    for k in range(3 if lod < 2 else 2):
        a = k * math.pi / 3 + rng.uniform(-0.2, 0.2)
        b.card(top - Vector((0, 0, 0.6)), Vector((math.cos(a), math.sin(a), 0)) * 1.1, Vector((0, 0, 2.0)), needles,
               normal_hint=hint)
    return b


def deciduous(name, seed, height, trunk_radius, lod):
    rng = random.Random(seed)
    b = Builder()
    bark_mat = material("bark_bark_willow_02", "bark_willow_02")
    leaves = material("foliage_leaves", foliage="leaves")
    sides = (8, 6, 4)[lod]
    trunk_top = height * 0.45
    trunk = [Vector((rng.uniform(-0.15, 0.15) * t, rng.uniform(-0.15, 0.15) * t, trunk_top * t)) for t in [i / 5 for i in range(6)]]
    b.tube(trunk, [trunk_radius * (1.2 - 0.5 * t) for t in [i / 5 for i in range(6)]], bark_mat, sides=sides)
    crown_center = Vector((0, 0, height * 0.68))
    hint = crown_normal(crown_center, 0.5)
    tips = []

    def branch(start, direction, length, radius, depth):
        end = start + direction * length
        mid = start + direction * length * 0.5 + Vector((rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 0.2)) * length * 0.2
        if lod < 2 or depth == 0:
            b.tube([start, mid, end], [radius, radius * 0.75, radius * 0.5], bark_mat, sides=max(3, sides - 2 - depth))
        if depth >= (3 if lod == 0 else 2):
            tips.append(end)
            return
        for _ in range(3 if depth == 0 else 2):
            d = (direction + Vector((rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), rng.uniform(0.0, 0.6)))).normalized()
            branch(end, d, length * rng.uniform(0.55, 0.75), radius * 0.6, depth + 1)

    for k in range(4):
        a = math.tau * k / 4 + rng.uniform(-0.3, 0.3)
        d = Vector((math.cos(a), math.sin(a), 1.1)).normalized()
        branch(trunk[-1], d, height * 0.28, trunk_radius * 0.55, 0)

    cards_per_tip = (6, 4, 2)[lod]
    for tip in tips:
        for _ in range(cards_per_tip):
            u = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.3))).normalized()
            v = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.5, 0.8))).normalized()
            size = rng.uniform(1.4, 2.2) if lod == 0 else rng.uniform(1.8, 2.6)
            origin = tip + Vector((rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), rng.uniform(-0.6, 0.6))) - v * size * 0.5
            b.card(origin, u * size, v * size, leaves, normal_hint=hint)
    # Inner fill: leaf clusters inside the crown so it reads as a volume, not a shell.
    if lod < 2:
        for _ in range((24, 10)[lod]):
            p = crown_center + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.6, 0.8))) * height * 0.18
            u = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.3))).normalized()
            v = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.5, 0.8))).normalized()
            size = rng.uniform(1.6, 2.4)
            b.card(p - v * size * 0.5, u * size, v * size, leaves, normal_hint=hint)
    return b


def rock(name, seed, scale, flatness, lod):
    rng = random.Random(seed)
    b = Builder()
    mat = material("rock_mossy_rock", "mossy_rock")
    subdivisions = (4, 3, 2)[lod]
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdivisions, radius=1.0)
    offset = Vector((rng.uniform(0, 100), rng.uniform(0, 100), rng.uniform(0, 100)))
    for v in bm.verts:
        p = v.co.copy()
        d = noise.fractal(p * 1.3 + offset, 0.5, 2.0, 4) * 0.35 + noise.noise(p * 3.5 + offset) * 0.08
        v.co = p * (1.0 + d)
        v.co.z *= flatness
        v.co.z = max(v.co.z, -0.25 * flatness)  # Flat bottom sits on the ground.
        v.co *= scale
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        f.smooth = True
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for loop in f.loops:
            co = loop.vert.co
            if ax == 0:
                loop[uv].uv = (co.y * 0.35, co.z * 0.35)
            elif ax == 1:
                loop[uv].uv = (co.x * 0.35, co.z * 0.35)
            else:
                loop[uv].uv = (co.x * 0.35, co.y * 0.35)
        f.material_index = 0
    b.bm.free()
    b.bm = bm
    b.uv = uv
    b.materials = [mat]
    return b


def fern(name, seed, lod):
    rng = random.Random(seed)
    b = Builder()
    mat = material("foliage_fern", foliage="fern")
    fronds = (11, 7, 4)[lod]
    hint = crown_normal(Vector((0, 0, -0.3)), 1.2)
    for k in range(fronds):
        a = math.tau * k / fronds + rng.uniform(-0.25, 0.25)
        length = rng.uniform(0.9, 1.3)
        rise = rng.uniform(0.5, 0.9)
        direction = Vector((math.cos(a), math.sin(a), rise)).normalized()
        side = Vector((-math.sin(a), math.cos(a), 0))
        b.card(Vector((0, 0, 0.02)), side * length * 0.32, direction * length, mat, segments=(4, 3, 1)[lod], bend=length * 0.55,
               normal_hint=hint)
    return b


def grass(name, seed, lod):
    rng = random.Random(seed)
    b = Builder()
    mat = material("foliage_grass", foliage="grass")
    cards = (5, 3, 2)[lod]
    hint = lambda co: Vector((0, 0, 1))
    for k in range(cards):
        a = math.pi * k / cards + rng.uniform(-0.2, 0.2)
        width = rng.uniform(0.35, 0.55)
        h = rng.uniform(0.35, 0.6)
        u = Vector((math.cos(a), math.sin(a), 0)) * width
        origin = Vector((rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), 0))
        b.card(origin, u, Vector((0, 0, h)), mat, segments=1, normal_hint=hint)
    return b


def bush(name, seed, lod):
    rng = random.Random(seed)
    b = Builder()
    mat = material("foliage_leaves", foliage="leaves")
    bark_mat = material("bark_bark_willow_02", "bark_willow_02")
    center = Vector((0, 0, 0.7))
    hint = crown_normal(Vector((0, 0, 0.2)), 0.4)
    for k in range((5, 3, 0)[lod]):
        a = math.tau * k / 5
        b.tube([Vector((0, 0, 0)), Vector((math.cos(a) * 0.4, math.sin(a) * 0.4, 0.8))], [0.04, 0.02], bark_mat, sides=4)
    for _ in range((26, 14, 7)[lod]):
        p = center + Vector((rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), rng.uniform(-0.4, 0.5)))
        u = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.3))).normalized()
        v = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(0.0, 1.0))).normalized()
        size = rng.uniform(0.8, 1.2)
        b.card(p - v * size * 0.5, u * size, v * size, mat, normal_hint=hint)
    return b


def log(name, seed, length, radius, lod):
    rng = random.Random(seed)
    b = Builder()
    bark_mat = material("bark_bark_brown_02", "bark_brown_02")
    points = [Vector((length * (t - 0.5), rng.uniform(-0.05, 0.05), radius + rng.uniform(-0.03, 0.03))) for t in
              [i / 6 for i in range(7)]]
    radii = [radius * (1.0 - 0.15 * t) for t in [i / 6 for i in range(7)]]
    b.tube(points, radii, bark_mat, sides=(10, 7, 5)[lod], uv_height=2.0, cap_top=True)
    # Broken branch stubs.
    if lod == 0:
        for _ in range(4):
            x = rng.uniform(-length * 0.4, length * 0.4)
            a = rng.uniform(0, math.tau)
            start = Vector((x, 0, radius))
            end = start + Vector((rng.uniform(-0.2, 0.2), math.cos(a), math.sin(a))) * rng.uniform(0.4, 0.8)
            b.tube([start, end], [radius * 0.22, radius * 0.1], bark_mat, sides=5)
    return b


def stump(name, seed, radius, height, lod):
    rng = random.Random(seed)
    b = Builder()
    bark_mat = material("bark_bark_brown_01", "bark_brown_01")
    points = [Vector((0, 0, height * t)) for t in (0.0, 0.3, 1.0)]
    b.tube(points, [radius * 1.5, radius * 1.1, radius], bark_mat, sides=(12, 8, 6)[lod], cap_top=True)
    return b


# --- Asset definitions --------------------------------------------------------------------------

def make_lods(build, collision=None):
    """build(lod) -> Builder. collision(Builder) optional -> Builder for the _COL node."""
    return [build(0), build(1), build(2)], collision


ASSETS = {
    "pine_a": (lambda lod: conifer("pine_a", 11, 22.0, 0.32, 0.38, 4.2, 1.0, 0.35, "bark_brown_02", lod), "trunk:0.45"),
    "pine_b": (lambda lod: conifer("pine_b", 12, 17.0, 0.26, 0.3, 3.6, 0.85, 0.45, "bark_brown_02", lod), "trunk:0.38"),
    "fir_a": (lambda lod: conifer("fir_a", 13, 14.0, 0.22, 0.12, 3.4, 0.6, 0.55, "bark_brown_02", lod), "trunk:0.32"),
    "redwood": (lambda lod: conifer("redwood", 14, 48.0, 1.15, 0.55, 5.5, 1.3, 0.25, "bark_brown_01", lod), "trunk:1.5"),
    "maple": (lambda lod: deciduous("maple", 15, 12.0, 0.28, lod), "trunk:0.35"),
    "rock_a": (lambda lod: rock("rock_a", 21, 1.2, 0.7, lod), "mesh"),
    "rock_b": (lambda lod: rock("rock_b", 22, 0.7, 0.8, lod), "mesh"),
    "boulder": (lambda lod: rock("boulder", 23, 2.8, 0.65, lod), "mesh"),
    "fern": (lambda lod: fern("fern", 31, lod), None),
    "grass": (lambda lod: grass("grass", 32, lod), None),
    "bush": (lambda lod: bush("bush", 33, lod), None),
    "log": (lambda lod: log("log", 41, 7.0, 0.38, lod), "mesh"),
    "stump": (lambda lod: stump("stump", 42, 0.55, 0.9, lod), "mesh"),
}


IMPOSTORS = ("pine_a", "pine_b", "fir_a", "redwood", "maple")


def render_impostor(name, obj):
    """Renders the LOD0 tree from the front (orthographic, unlit albedo, transparent background) into
    assets/textures/foliage/impostor_<name>.png. Returns (width, height) of the covered area."""
    import numpy as np  # Bundled with Blender.

    xs = [v.co.x for v in obj.data.vertices]
    ys = [v.co.y for v in obj.data.vertices]
    zs = [v.co.z for v in obj.data.vertices]
    width = max(max(xs) - min(xs), max(ys) - min(ys)) * 1.02
    height = max(zs) * 1.01
    scene = bpy.context.scene
    camera = bpy.data.objects.new("impostor_cam", bpy.data.cameras.new("impostor_cam"))
    scene.collection.objects.link(camera)
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = max(width, height)
    camera.location = (0.0, -100.0, height * 0.5)
    camera.rotation_euler = (math.pi / 2, 0.0, 0.0)
    camera.data.clip_end = 300.0
    scene.camera = camera
    # Workbench, flat light, texture colors: the unlit albedo the engine lights again.
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.render_aa = "32"
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    size = 1024
    aspect = width / height
    scene.render.resolution_x = max(64, int(round(size * min(aspect, 1.0) / 32)) * 32)
    scene.render.resolution_y = max(64, int(round(size * min(1.0 / aspect, 1.0) / 32)) * 32)
    # Camera frame matches the larger side; crop the other to the tree.
    camera.data.sensor_fit = "VERTICAL" if height >= width else "HORIZONTAL"
    path = TEXTURES / "foliage" / f"impostor_{name}.png"
    scene.render.filepath = str(path)
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(camera)

    # Bleed colors into transparent texels so mipmaps do not darken the silhouette.
    img = bpy.data.images.load(str(path))
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    alpha = px[:, :, 3] > 0.5
    color = px[:, :, :3] * alpha[:, :, None]
    weight = alpha.astype(np.float32)
    filled = color.copy()
    known = alpha.copy()
    for _ in range(24):
        acc = np.zeros_like(filled)
        cnt = np.zeros(known.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            acc += np.roll(np.roll(filled * known[:, :, None], dy, 0), dx, 1)
            cnt += np.roll(np.roll(known.astype(np.float32), dy, 0), dx, 1)
        grow = (~known) & (cnt > 0)
        filled[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    avg = color.sum(axis=(0, 1)) / max(weight.sum(), 1.0)
    filled[~known] = avg
    px[:, :, :3] = filled
    px[:, :, 3] = alpha.astype(np.float32)
    img.pixels[:] = px.ravel()
    img.save()
    bpy.data.images.remove(img)
    print(f"[origon] {name}: impostor {w}x{h}")
    return width, height


def impostor_lod(name, width, height):
    """LOD3: two crossed quads carrying the impostor texture."""
    b = Builder()
    mat = material(f"foliage_impostor_{name}", foliage=f"impostor_{name}")
    hint = crown_normal(Vector((0, 0, height * 0.55)), 0.7)
    b.card(Vector((0, 0, 0)), Vector((width, 0, 0)), Vector((0, 0, height)), mat, normal_hint=hint)
    b.card(Vector((0, 0, 0)), Vector((0, width, 0)), Vector((0, 0, height)), mat, normal_hint=hint)
    return b


def trunk_collision(radius, height):
    b = Builder()
    mat = material("collision", None)
    b.tube([Vector((0, 0, 0)), Vector((0, 0, height))], [radius, radius], mat, sides=8, cap_top=True)
    return b


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
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    names = args or list(ASSETS)
    for name in names:
        reset_scene()
        _materials.clear()
        collection = bpy.context.scene.collection
        build, collision = ASSETS[name]
        objects = []
        lods = [build(lod) for lod in range(3)]
        for lod, builder in enumerate(lods):
            objects.append(builder.to_object(f"{name}_LOD{lod}", collection))
        if name in IMPOSTORS:
            width, height = render_impostor(name, objects[0])
            objects.append(impostor_lod(name, width, height).to_object(f"{name}_LOD3", collection))
        if collision == "mesh":
            col = build(2).to_object(f"{name}_COL", collection)
            objects.append(col)
        elif collision and collision.startswith("trunk:"):
            radius = float(collision.split(":")[1])
            objects.append(trunk_collision(radius, 6.0).to_object(f"{name}_COL", collection))
        export(name, objects)
        tris = sum(len(p.vertices) - 2 for p in objects[0].data.polygons)
        print(f"[origon] {name}: LOD0 {tris} triangles")


if __name__ == "__main__":
    main()
