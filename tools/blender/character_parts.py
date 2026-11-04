"""Headgear for gen_human.py: helmets, caps, hats, eyewear and facial hair, modeled procedurally.

Every piece is a mesh object named "x_<slot>_<variant>" (slot: headwear, eyewear, face). The engine
shows one variant per slot from the player's look (game/fx/SoldierLook.*).

Soft shapes come from Blender's Skin modifier (chains of spheres with per-node radii) smoothed by
subdivision; hard items (helmets, hats, glasses) are lathed profiles.

Coordinates: Blender, Z up, the character faces +Y, its right side is +X, feet at the origin.
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector

# Default material colors (linear-ish sRGB) and roughness. The engine recolors skin, hair, top,
# top_detail, bottom and tint from the player's look.
MATERIALS = {
    "skin": ((0.62, 0.43, 0.32), 0.55, 0.0),
    "lips": ((0.5, 0.28, 0.24), 0.45, 0.0),
    "eye": ((0.04, 0.03, 0.025), 0.15, 0.0),
    "eye_white": ((0.75, 0.72, 0.68), 0.25, 0.0),
    "hair": ((0.16, 0.1, 0.06), 0.7, 0.0),
    "top": ((0.3, 0.33, 0.2), 0.9, 0.0),
    "top_detail": ((0.22, 0.24, 0.15), 0.9, 0.0),
    "bottom": ((0.24, 0.24, 0.17), 0.9, 0.0),
    "boots": ((0.12, 0.08, 0.05), 0.55, 0.0),
    "sole": ((0.05, 0.045, 0.04), 0.8, 0.0),
    "gear": ((0.36, 0.3, 0.2), 0.8, 0.0),
    "leather": ((0.25, 0.15, 0.08), 0.5, 0.0),
    "metal": ((0.45, 0.45, 0.46), 0.35, 1.0),
    "helmet": ((0.22, 0.25, 0.15), 0.6, 0.0),
    "helmet_grey": ((0.25, 0.27, 0.26), 0.6, 0.0),
    "felt": ((0.24, 0.17, 0.11), 0.85, 0.0),
    "wool": ((0.18, 0.2, 0.15), 0.95, 0.0),
    "beret": ((0.08, 0.08, 0.1), 0.9, 0.0),
    "lens": ((0.02, 0.02, 0.025), 0.05, 0.0),
    "cloth_face": ((0.3, 0.12, 0.1), 0.9, 0.0),
    "tint": ((0.7, 0.2, 0.15), 0.8, 0.0),
}


# --- Geometry helpers ----------------------------------------------------------------------------

def chain(nodes, levels=2):
    """Skin-modifier tube through nodes [(x, y, z, rx, ry)]. Returns (verts, polys)."""
    mesh = bpy.data.meshes.new("tmp_chain")
    mesh.from_pydata([n[:3] for n in nodes], [(i, i + 1) for i in range(len(nodes) - 1)], [])
    obj = bpy.data.objects.new("tmp_chain", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.modifiers.new("skin", "SKIN")
    for i, n in enumerate(nodes):
        mesh.skin_vertices[0].data[i].radius = (n[3], n[4])
    mesh.skin_vertices[0].data[0].use_root = True
    obj.modifiers.new("sub", "SUBSURF").levels = levels
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph))
    verts = [v.co.copy() for v in evaluated.vertices]
    polys = [tuple(p.vertices) for p in evaluated.polygons]
    bpy.data.objects.remove(obj)
    bpy.data.meshes.remove(mesh)
    bpy.data.meshes.remove(evaluated)
    return verts, polys


def ellipsoid(center, radii, segments=16, rings=10, keep=None, deform=None):
    """UV ellipsoid. keep(local unit-sphere point) filters faces; deform(world point) -> point."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=1.0)
    if keep:
        doomed = [f for f in bm.faces if not keep(f.calc_center_median())]
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
    verts, index = [], {}
    for v in bm.verts:
        p = Vector((center[0] + v.co.x * radii[0], center[1] + v.co.y * radii[1], center[2] + v.co.z * radii[2]))
        index[v] = len(verts)
        verts.append(deform(p) if deform else p)
    polys = [tuple(index[v] for v in f.verts) for f in bm.faces]
    bm.free()
    return verts, polys


def lathe(profile, center, scale=(1.0, 1.0, 1.0), segments=20, closed=False, rotation=None):
    """Surface of revolution around local Z from a (radius, height) profile.
    Points with radius 0 collapse into a pole. closed=True joins the last ring to the first."""
    verts, rings = [], []
    for r, z in profile:
        if r <= 1e-6:
            rings.append([len(verts)])
            verts.append(Vector((0.0, 0.0, z)))
            continue
        ring = []
        for s in range(segments):
            a = 2.0 * math.pi * s / segments
            ring.append(len(verts))
            verts.append(Vector((math.cos(a) * r, math.sin(a) * r, z)))
        rings.append(ring)
    polys = []
    pairs = list(zip(rings[:-1], rings[1:]))
    if closed:
        pairs.append((rings[-1], rings[0]))
    for a, b in pairs:
        if len(a) == 1 and len(b) == 1:
            continue
        for s in range(segments):
            t = (s + 1) % segments
            if len(a) == 1:
                polys.append((a[0], b[t], b[s]))
            elif len(b) == 1:
                polys.append((a[s], a[t], b[0]))
            else:
                polys.append((a[s], a[t], b[t], b[s]))
    m = rotation or Matrix.Identity(3)
    out = []
    for v in verts:
        p = Vector((v.x * scale[0], v.y * scale[1], v.z * scale[2]))
        p = m @ p
        out.append(Vector(center) + p)
    return out, polys


def rounded_box(center, size, levels=1):
    """A soft box (pouches, pockets, backpacks): a two-node skin chain along X."""
    cx, cy, cz = center
    sx, sy, sz = size
    return chain([(cx - sx * 0.25, cy, cz, sy * 0.5, sz * 0.5), (cx + sx * 0.25, cy, cz, sy * 0.5, sz * 0.5)], levels)


def transformed(geometry, matrix):
    verts, polys = geometry
    return [matrix @ v for v in verts], polys


# --- Skin weights ---------------------------------------------------------------------------------

def segment_distance(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
    return (p - (a + ab * t)).length


def compute_weights(point, bones, rig_bones):
    """Smooth weights from distances to the candidate bone segments (side-filtered)."""
    scored = []
    for name in bones:
        if name.endswith(".R") and point.x < -0.03:
            continue
        if name.endswith(".L") and point.x > 0.03:
            continue
        head, tail = rig_bones[name]
        d = max(segment_distance(point, Vector(head), Vector(tail)), 0.004)
        scored.append((1.0 / d ** 6, name))
    if not scored:
        return {bones[0]: 1.0}
    scored.sort(reverse=True)
    scored = scored[:3]
    total = sum(w for w, _ in scored)
    weights = {n: w / total for w, n in scored if w / total > 0.02}
    total = sum(weights.values())
    return {n: w / total for n, w in weights.items()}


class Piece:
    """Accumulates geometry for one exported mesh object."""

    def __init__(self, name):
        self.name = name
        self.verts, self.polys, self.poly_mats, self.weights = [], [], [], []
        self.materials = []

    def add(self, geometry, material, bones, rig_bones):
        verts, polys = geometry
        if material not in self.materials:
            self.materials.append(material)
        slot = self.materials.index(material)
        base = len(self.verts)
        for v in verts:
            self.verts.append(Vector(v))
            self.weights.append(compute_weights(Vector(v), bones, rig_bones))
        for p in polys:
            self.polys.append(tuple(base + i for i in p))
            self.poly_mats.append(slot)
        return self

    def build(self, rig):
        mesh = bpy.data.meshes.new(self.name)
        mesh.from_pydata([tuple(v) for v in self.verts], [], self.polys)
        for name in self.materials:
            mesh.materials.append(material(name))
        for poly, slot in zip(mesh.polygons, self.poly_mats):
            poly.material_index = slot
            poly.use_smooth = True
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        groups = {}
        for i, weights in enumerate(self.weights):
            for bone, w in weights.items():
                if bone not in groups:
                    groups[bone] = obj.vertex_groups.new(name=bone)
                groups[bone].add([i], w, "REPLACE")
        obj.parent = rig
        obj.modifiers.new("rig", "ARMATURE").object = rig
        return obj


def material(name):
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    color, roughness, metallic = MATERIALS[name]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    return mat


# --- Head reference -------------------------------------------------------------------------------

# Nominal head the headgear is modeled for; gen_human.py scales the result to each real head.
HEAD_CENTER = Vector((0.0, 0.014, 1.685))
HEAD_R = (0.093, 0.109, 0.123)
HEAD_BONES = ["head"]


def build_headgear(rig_bones, pieces=None):
    """Facial hair, headwear and eyewear fitted to HEAD_CENTER / HEAD_R."""
    pieces = [] if pieces is None else pieces

    def piece(name):
        p = Piece(name)
        pieces.append(p)
        return p

    c = HEAD_CENTER
    piece("x_face_mustache").add(chain([(-0.032, c.y + 0.096, c.z - 0.042, 0.009, 0.007),
                                        (0.0, c.y + 0.105, c.z - 0.036, 0.011, 0.008),
                                        (0.032, c.y + 0.096, c.z - 0.042, 0.009, 0.007)], 1), "hair", HEAD_BONES, rig_bones)

    def beard_keep(n):
        return n.z < -0.25 and n.y > -0.15 and not (n.y > 0.85 and -0.62 < n.z < -0.38 and abs(n.x) < 0.25)

    piece("x_face_beard").add(ellipsoid(c + Vector((0, 0.004, 0.0)), (HEAD_R[0] * 1.07, HEAD_R[1] * 1.06, HEAD_R[2] * 1.07), 20, 14,
                                        keep=beard_keep,
                                        deform=lambda p: c + Vector(((p - c).x * (1 - 0.25 * max(0, -(p - c).z) / 0.12),
                                                                     (p - c).y, (p - c).z))),
                              "hair", HEAD_BONES, rig_bones)
    p = piece("x_face_goatee")
    p.add(ellipsoid((0.0, c.y + 0.094, c.z - 0.09), (0.024, 0.02, 0.03), 10, 8), "hair", HEAD_BONES, rig_bones)
    p.add(chain([(-0.026, c.y + 0.098, c.z - 0.042, 0.008, 0.006), (0.0, c.y + 0.106, c.z - 0.036, 0.009, 0.007),
                 (0.026, c.y + 0.098, c.z - 0.042, 0.008, 0.006)], 1), "hair", HEAD_BONES, rig_bones)

    # Headwear. Profiles are (radius, height) around the head's vertical axis.
    top_of_head = c.z + 0.115
    shell = Matrix.Identity(3)

    def helmet(name, outer, mat, stretch=1.1):
        inner = [(r * 0.93, z - 0.008) for r, z in reversed(outer)]
        return piece(name).add(lathe(outer + inner, (0.0, c.y - 0.008, c.z + 0.058), scale=(1.03, 1.03 * stretch, 1.03),
                                     segments=24),
                               mat, HEAD_BONES, rig_bones)

    helmet("x_headwear_helmet", [(0.0, 0.13), (0.06, 0.125), (0.105, 0.1), (0.13, 0.06), (0.142, 0.015), (0.146, -0.012),
                                 (0.16, -0.02), (0.163, -0.03)], "helmet")
    helmet("x_headwear_stahlhelm", [(0.0, 0.125), (0.065, 0.12), (0.11, 0.095), (0.132, 0.05), (0.137, 0.0),
                                    (0.14, -0.035), (0.158, -0.06), (0.17, -0.075)], "helmet_grey", stretch=1.08)
    p = piece("x_headwear_cap")
    p.add(lathe([(0.0, 0.1), (0.098, 0.1), (0.1, 0.03), (0.095, -0.01), (0.0, -0.01)], (0.0, c.y - 0.008, c.z + 0.05),
                scale=(1.1, 1.2, 1.08)), "top", HEAD_BONES, rig_bones)
    p.add(ellipsoid((0.0, c.y + 0.12, c.z + 0.045), (0.07, 0.05, 0.007), 14, 6), "top_detail", HEAD_BONES, rig_bones)
    tilt = Matrix.Rotation(math.radians(-14), 3, "Y")
    piece("x_headwear_beret").add(transformed(ellipsoid((0.0, 0.0, 0.0), (0.126, 0.137, 0.04), 18, 10),
                                              Matrix.Translation(c + Vector((-0.015, -0.01, 0.092))) @ tilt.to_4x4()),
                                  "beret", HEAD_BONES, rig_bones)
    p = piece("x_headwear_hat")
    p.add(lathe([(0.0, 0.115), (0.075, 0.12), (0.092, 0.1), (0.098, 0.03), (0.0, 0.03)], (0.0, c.y - 0.005, c.z + 0.045),
                scale=(1.1, 1.2, 1.08)), "felt", HEAD_BONES, rig_bones)
    p.add(lathe([(0.0, 0.034), (0.185, 0.03), (0.2, 0.02), (0.185, 0.022), (0.0, 0.026)], (0.0, c.y - 0.005, c.z + 0.045),
                scale=(1.1, 1.19, 1.08), segments=24), "felt", HEAD_BONES, rig_bones)
    p.add(lathe([(0.097, 0.032), (0.1, 0.032), (0.1, 0.05), (0.096, 0.05)], (0.0, c.y - 0.005, c.z + 0.045),
                scale=(1.1, 1.2, 1.08), closed=True), "leather", HEAD_BONES, rig_bones)
    p = piece("x_headwear_beanie")
    p.add(lathe([(0.0, 0.135), (0.06, 0.13), (0.1, 0.1), (0.098, 0.03), (0.096, -0.0), (0.0, -0.0)],
                (0.0, c.y - 0.008, c.z + 0.04), scale=(1.1, 1.19, 1.08)), "wool", HEAD_BONES, rig_bones)
    p.add(lathe([(0.097, -0.002), (0.104, -0.002), (0.104, 0.035), (0.097, 0.035)], (0.0, c.y - 0.008, c.z + 0.04),
                scale=(1.1, 1.19, 1.08), closed=True), "wool", HEAD_BONES, rig_bones)

    # Eyewear: rings face forward (lathe around Z, rotated so Z points to +Y).
    forward = Matrix.Rotation(math.radians(-90), 3, "X")
    eye_y = c.y + HEAD_R[1] * 0.7 + 0.02
    ring = [(0.019, -0.0025), (0.023, -0.0025), (0.023, 0.0025), (0.019, 0.0025)]
    disc = [(0.0, 0.001), (0.0195, 0.001), (0.0195, -0.001), (0.0, -0.001)]

    def glasses(name, lenses):
        p = piece(name)
        for side in (-1, 1):
            center = (side * 0.036, eye_y, c.z + 0.012)
            p.add(lathe(ring, center, segments=16, closed=True, rotation=forward), "metal", HEAD_BONES, rig_bones)
            if lenses:
                p.add(lathe(disc, center, segments=16, rotation=forward), "lens", HEAD_BONES, rig_bones)
            temple = [(side * 0.059, eye_y, c.z + 0.015, 0.0025, 0.0025), (side * 0.097, c.y + 0.0, c.z + 0.012, 0.0025, 0.0025)]
            p.add(chain(temple, 0), "metal", HEAD_BONES, rig_bones)
        p.add(chain([(-0.012, eye_y + 0.002, c.z + 0.016, 0.002, 0.002), (0.012, eye_y + 0.002, c.z + 0.016, 0.002, 0.002)], 0),
              "metal", HEAD_BONES, rig_bones)

    glasses("x_eyewear_glasses", False)
    glasses("x_eyewear_sunglasses", True)
    p = piece("x_eyewear_goggles")
    for side in (-1, 1):
        center = (side * 0.038, c.y + 0.11, c.z + 0.09)
        lens_rot = Matrix.Rotation(math.radians(-60), 3, "X")
        p.add(lathe([(0.0, 0.012), (0.022, 0.012), (0.024, -0.008), (0.0, -0.008)], center, segments=14, rotation=lens_rot),
              "leather", HEAD_BONES, rig_bones)
        p.add(lathe([(0.0, 0.0135), (0.017, 0.0135), (0.017, 0.012), (0.0, 0.012)], center, segments=14, rotation=lens_rot),
              "lens", HEAD_BONES, rig_bones)
    p.add(lathe([(0.088, 0.0), (0.094, 0.0), (0.094, 0.018), (0.088, 0.018)], (0.0, c.y - 0.004, c.z + 0.065),
                scale=(1.1, 1.25, 1.0), segments=24, closed=True), "leather", HEAD_BONES, rig_bones)
    p = piece("x_eyewear_bandana")
    p.add(ellipsoid(c, (HEAD_R[0] * 1.09, HEAD_R[1] * 1.09, HEAD_R[2] * 1.07), 20, 14,
                    keep=lambda n: n.z < -0.05 and n.y > -0.5,
                    deform=lambda p: c + Vector(((p - c).x * (1 - 0.2 * max(0, -(p - c).z) / 0.12), (p - c).y, (p - c).z))),
          "cloth_face", HEAD_BONES, rig_bones)
    p.add(chain([(0.0, -0.002, 1.5, 0.068, 0.072), (0.0, 0.004, 1.6, 0.064, 0.07)], 1), "cloth_face", ["neck", "head"],
          rig_bones)
    return pieces
