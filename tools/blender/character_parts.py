"""Character meshes for gen_character.py: male and female bodies, heads, hair, clothing and accessories.

Every piece is a separate skinned mesh object named "<g>_<slot>_<variant>", where <g> is "m" (male),
"f" (female) or "x" (shared). The engine shows one variant per slot from the player's look
(game/fx/CharacterLook.*). Bodies use the same skeleton, so all animations are shared; both bodies are
the same height, so what players see matches the identical hit capsules.

Organic shapes come from Blender's Skin modifier (chains of spheres with per-node radii) smoothed by
subdivision; hard items (helmets, hats, glasses) are lathed profiles. Skin weights are computed from
the distance to each candidate bone segment, so every chain only follows the bones it belongs to.

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


# --- Body proportions ----------------------------------------------------------------------------

HEAD_CENTER = Vector((0.0, 0.014, 1.685))
HEAD_R = (0.093, 0.109, 0.123)  # Male skull radii; shared pieces (hair, hats) fit this size.


class Body:
    """Per-gender radii. Joint positions come from the shared skeleton."""

    def __init__(self, gender, rig_bones):
        self.g = gender
        self.bones = rig_bones
        female = gender == "f"
        # Torso nodes: (y, z, rx, ry) from the crotch up to the base of the neck.
        if female:
            self.torso = [(-0.012, 0.85, 0.135, 0.095), (-0.015, 0.93, 0.172, 0.118), (-0.008, 1.03, 0.142, 0.1),
                          (0.0, 1.1, 0.122, 0.088), (0.004, 1.2, 0.13, 0.092), (0.006, 1.3, 0.148, 0.1),
                          (0.0, 1.39, 0.155, 0.092), (-0.01, 1.46, 0.125, 0.075), (-0.012, 1.51, 0.08, 0.06)]
            self.limb = 0.86
            self.head_radii = (0.088, 0.104, 0.116)
        else:
            self.torso = [(-0.01, 0.85, 0.13, 0.092), (-0.01, 0.93, 0.158, 0.11), (0.0, 1.04, 0.145, 0.1),
                          (0.004, 1.15, 0.155, 0.105), (0.01, 1.26, 0.175, 0.118), (0.008, 1.35, 0.188, 0.115),
                          (0.0, 1.42, 0.198, 0.108), (-0.01, 1.48, 0.155, 0.088), (-0.012, 1.52, 0.095, 0.07)]
            self.limb = 1.0
            self.head_radii = HEAD_R
        self.female = female

    def bone(self, name):
        head, tail = self.bones[name]
        return Vector(head), Vector(tail)

    def torso_nodes(self, inflate=0.0, z_min=0.0, z_max=9.0):
        return [(0.0, y, z, rx + inflate, ry + inflate) for y, z, rx, ry in self.torso if z_min <= z <= z_max]

    def breasts(self, inflate=0.0):
        out = []
        for side in (-1, 1):
            out.append(ellipsoid((side * 0.058, 0.07 + inflate * 0.5, 1.285),
                                 (0.062 + inflate, 0.058 + inflate, 0.058 + inflate), 12, 8))
        return out

    def arm_nodes(self, side, inflate=0.0, until=1.0, start=0.0):
        """Shoulder to wrist; until/start in 0..1 along the arm (0.5 = elbow)."""
        s = "R" if side > 0 else "L"
        sh, el = self.bone(f"upperarm.{s}")
        _, wr = self.bone(f"forearm.{s}")
        k = self.limb
        cap = sh + Vector((-0.035 * side, 0.0, 0.015))
        points = [
            (0.0, cap, 0.074 * k), (0.06, sh, 0.068 * k), (0.25, sh.lerp(el, 0.45), 0.058 * k),
            (0.5, el, 0.045 * k), (0.65, el.lerp(wr, 0.35), 0.048 * k), (1.0, wr, 0.032 * k),
        ]
        nodes = []
        for i, (t, p, r) in enumerate(points):
            if t < start - 1e-6 or t > until + 1e-6:
                continue
            nodes.append((p.x, p.y, p.z, r + inflate, r + inflate))
        # Partial sleeves: end exactly at `until` between two nodes.
        if until < 1.0 and nodes:
            for (t0, p0, r0), (t1, p1, r1) in zip(points, points[1:]):
                if t0 < until < t1:
                    u = (until - t0) / (t1 - t0)
                    p = p0.lerp(p1, u)
                    r = r0 + (r1 - r0) * u
                    nodes.append((p.x, p.y, p.z, r + inflate, r + inflate))
        return nodes

    def leg_nodes(self, side, inflate=0.0, until=1.0):
        s = "R" if side > 0 else "L"
        hip, knee = self.bone(f"thigh.{s}")
        _, ankle = self.bone(f"shin.{s}")
        f = self.female
        points = [
            (0.0, hip + Vector((-0.01 * side, -0.005, 0.05)), 0.1 if f else 0.098, 0.104 if f else 0.102),
            (0.2, hip.lerp(knee, 0.3), 0.09 if f else 0.09, 0.096),
            (0.4, hip.lerp(knee, 0.72), 0.07 if f else 0.072, 0.075),
            (0.5, knee, 0.054 if f else 0.058, 0.06),
            (0.62, knee.lerp(ankle, 0.28), 0.057 if f else 0.062, 0.068),
            (0.8, knee.lerp(ankle, 0.68), 0.04 if f else 0.044, 0.046),
            (1.0, ankle + Vector((0.0, 0.0, 0.03)), 0.034, 0.037),
        ]
        nodes = [(p.x, p.y, p.z, rx + inflate, ry + inflate) for t, p, rx, ry in points if t <= until + 1e-6]
        return nodes


# --- Pieces ---------------------------------------------------------------------------------------

TORSO_BONES = ["hips", "spine", "chest"]
PELVIS_BONES = ["hips", "spine", "thigh.R", "thigh.L"]
HEAD_BONES = ["head"]


def arm_bones(side):
    s = "R" if side > 0 else "L"
    return ["chest", f"upperarm.{s}", f"forearm.{s}", f"hand.{s}"]


def leg_bones(side):
    s = "R" if side > 0 else "L"
    return ["hips", f"thigh.{s}", f"shin.{s}", f"foot.{s}"]


def head_shape(local, radii):
    """Skull deformation (local offset from HEAD_CENTER): narrower jaw, flatter back and face."""
    rx, ry, rz = radii
    local = local.copy()
    below = max(0.0, min(1.0, -local.z / (rz * 0.9)))
    local.x *= 1.0 - 0.28 * below * below
    if local.y < 0:
        local.y *= 1.0 - 0.25 * below
    else:
        local.y *= 1.0 - 0.1 * below
        if local.z < -rz * 0.6:
            local.y += 0.012
    if local.y > ry * 0.7:
        local.y = ry * 0.7 + (local.y - ry * 0.7) * 0.6
    return local


def face_y(x, z, radii):
    """Y of the face surface at local (x, z) (front of the deformed skull)."""
    rx, ry, rz = radii
    inside = max(0.0, 1.0 - (x / rx) ** 2 - (z / rz) ** 2)
    return head_shape(Vector((x, ry * math.sqrt(inside), z)), radii).y


def head_geometry(body):
    """Skull with jaw, nose, ears, eyes, brows, cheekbones and lips."""
    c = HEAD_CENTER
    radii = body.head_radii
    rx, ry, rz = radii
    parts = [(ellipsoid(c, radii, 20, 14, deform=lambda p: c + head_shape(p - c, radii)), "skin")]

    def on_face(x, z, inset):
        return (x, c.y + face_y(x, z, radii) - inset, c.z + z)

    parts.append((ellipsoid(on_face(0.0, -0.016, 0.012), (0.015, 0.026, 0.032), 10, 8), "skin"))  # Nose.
    parts.append((ellipsoid(on_face(0.0, -0.036, 0.016), (0.012, 0.018, 0.01), 8, 6), "skin"))   # Nose tip.
    for side in (-1, 1):
        parts.append((ellipsoid((side * (rx + 0.001), c.y - 0.005, c.z - 0.005), (0.012, 0.024, 0.035), 10, 8), "skin"))
        ex, ez = side * 0.035, 0.012
        eye = on_face(ex, ez, 0.006)
        parts.append((ellipsoid(eye, (0.016, 0.008, 0.0095), 12, 8), "eye_white"))
        parts.append((ellipsoid((eye[0], eye[1] + 0.006, eye[2]), (0.0075, 0.004, 0.0078), 10, 6), "eye"))
        # Upper eyelid shadow line, brow ridge and eyebrow.
        parts.append((ellipsoid(on_face(ex, ez + 0.012, 0.004), (0.019, 0.007, 0.005), 10, 6), "skin"))
        parts.append((ellipsoid(on_face(side * 0.036, 0.034, 0.002), (0.025, 0.007, 0.005 if body.female else 0.0075), 10, 6),
                      "hair"))
        parts.append((ellipsoid(on_face(side * 0.05, -0.024, 0.014), (0.026, 0.024, 0.02), 10, 6), "skin"))  # Cheek.
    parts.append((ellipsoid(on_face(0.0, -0.056, 0.004), (0.02, 0.007, 0.0065), 10, 6), "lips"))
    parts.append((ellipsoid(on_face(0.0, -0.068, 0.004), (0.017, 0.006, 0.006), 10, 6), "lips"))
    return parts


def hair_cap(scale=1.06, front=0.045, back=-0.075):
    """Hair on the skull: an enlarged head shell trimmed at the hairline."""
    c = HEAD_CENTER

    def keep(n):  # Unit-sphere point.
        if n.y > 0.35:
            return n.z > front / 0.11 + (n.y - 0.35) * 0.4
        if abs(n.x) > 0.8 and n.y > -0.2:
            return n.z > 0.05  # Above the ears.
        return n.z > back / 0.11
    return ellipsoid(c, (HEAD_R[0] * scale, HEAD_R[1] * scale, HEAD_R[2] * scale), 20, 14, keep=keep)


def build_pieces(rig_bones):
    pieces = []

    def piece(name):
        p = Piece(name)
        pieces.append(p)
        return p

    for g in ("m", "f"):
        body = Body(g, rig_bones)
        # --- Skin, split by what clothing can cover.
        p = piece(f"{g}_skin_torso")
        p.add(chain(body.torso_nodes()), "skin", TORSO_BONES, rig_bones)
        if body.female:
            for b in body.breasts():
                p.add(b, "skin", ["chest"], rig_bones)
        p = piece(f"{g}_skin_neck")
        p.add(chain([(0, -0.008, 1.45, 0.07, 0.07), (0, 0.0, 1.53, 0.064 if g == "m" else 0.054, 0.062),
                     (0, 0.01, 1.6, 0.058 if g == "m" else 0.05, 0.058)]), "skin", ["chest", "neck", "head"], rig_bones)
        p = piece(f"{g}_skin_upperarms")
        for side in (-1, 1):
            p.add(chain(body.arm_nodes(side, until=0.55)), "skin", arm_bones(side), rig_bones)
        p = piece(f"{g}_skin_forearms")
        for side in (-1, 1):
            p.add(chain(body.arm_nodes(side, start=0.5)), "skin", arm_bones(side), rig_bones)
        p = piece(f"{g}_skin_legs")
        for side in (-1, 1):
            p.add(chain(body.leg_nodes(side)), "skin", leg_bones(side), rig_bones)
        p = piece(f"{g}_head")
        for geometry, mat in head_geometry(body):
            p.add(geometry, mat, HEAD_BONES, rig_bones)

        # --- Tops (cover the torso; sleeves decide what arm skin is hidden).
        def top(name, sleeve_until, inflate, collar=True, pockets=True, cuff=False, hem=False):
            p = piece(f"{g}_top_{name}")
            p.add(chain(body.torso_nodes(inflate, z_min=0.9)), "top", TORSO_BONES, rig_bones)
            if body.female:
                for b in body.breasts(inflate):
                    p.add(b, "top", ["chest"], rig_bones)
            for side in (-1, 1):
                p.add(chain(body.arm_nodes(side, inflate * 0.9, until=sleeve_until)), "top", arm_bones(side), rig_bones)
                if cuff:
                    nodes = body.arm_nodes(side, inflate * 0.9 + 0.012, until=sleeve_until)
                    last = nodes[-1]
                    prev = nodes[-2]
                    a = Vector(prev[:3]).lerp(Vector(last[:3]), 0.55)
                    p.add(chain([(a.x, a.y, a.z, last[3], last[4]), (*last[:3], last[3], last[4])], 1),
                          "top_detail", arm_bones(side), rig_bones)
                if pockets:
                    y = body.torso[5][3] + inflate + (0.035 if body.female else 0.004)
                    p.add(rounded_box((side * 0.075, y, 1.31), (0.085, 0.02, 0.085)), "top_detail", ["chest"], rig_bones)
            if collar:
                p.add(lathe([(0.074, 1.48), (0.094, 1.48), (0.098, 1.55), (0.08, 1.56)], (0, 0.0, 0.0),
                            scale=(1.0, 1.05, 1.0), segments=16, closed=True), "top_detail", ["chest", "neck"], rig_bones)
            if hem:
                nodes = body.torso_nodes(inflate + 0.008)
                ring = [n for n in nodes if 0.9 <= n[2] <= 1.05]
                p.add(chain(ring[:2], 1), "top_detail", TORSO_BONES, rig_bones)
            # Player-color armband on the left sleeve (identification in free-for-all).
            nodes = body.arm_nodes(-1, inflate + 0.008, until=0.22)
            a, b = Vector(nodes[-2][:3]), Vector(nodes[-1][:3])
            p.add(chain([(*a.lerp(b, 0.3), nodes[-1][3], nodes[-1][4]), (*b, nodes[-1][3], nodes[-1][4])], 1),
                  "tint", arm_bones(-1), rig_bones)

        top("jacket", 1.0, 0.017, cuff=False)
        top("shirt", 0.62, 0.012, pockets=True, cuff=True)
        top("tshirt", 0.3, 0.009, collar=False, pockets=False)
        top("sweater", 1.0, 0.022, collar=False, pockets=False, hem=True)

        # --- Bottoms.
        def bottom(name, until, inflate, gaiters=False):
            p = piece(f"{g}_bottom_{name}")
            pelvis = [n for n in body.torso_nodes(inflate + 0.004) if n[2] <= 1.04]
            p.add(chain(pelvis), "bottom", PELVIS_BONES, rig_bones)
            for side in (-1, 1):
                nodes = body.leg_nodes(side, inflate, until=until)
                p.add(chain(nodes), "bottom", leg_bones(side), rig_bones)
                if gaiters:
                    gaiter = [n for n in body.leg_nodes(side, inflate + 0.012) if n[2] < 0.36]
                    p.add(chain(gaiter, 1), "gear", leg_bones(side), rig_bones)
            return p

        bottom("trousers", 1.0, 0.015)
        bottom("gaiters", 1.0, 0.012, gaiters=True)
        bottom("shorts", 0.36, 0.016)

        # --- Gear (depends on the torso shape).
        waist = body.torso[1]
        belt_r = (waist[2] + 0.026, waist[3] + 0.026)
        p = piece(f"{g}_gear_belt")
        p.add(chain([(0, waist[0], 0.95, *belt_r), (0, waist[0], 0.99, *belt_r)], 1), "leather", PELVIS_BONES, rig_bones)
        for side in (-1, 1):
            for k in range(2):
                x = side * (0.06 + 0.055 * k)
                p.add(rounded_box((x, waist[3] + 0.035, 0.97), (0.05, 0.035, 0.065)), "gear", ["hips"], rig_bones)
        p.add(rounded_box((0.0, waist[3] + 0.03, 0.97), (0.04, 0.012, 0.035)), "metal", ["hips"], rig_bones)

        p = piece(f"{g}_gear_suspenders")
        chest_y = body.torso[5][3] + 0.03 + (0.03 if body.female else 0.0)
        back_y = -body.torso[5][3] - 0.03
        for side in (-1, 1):
            front = [(side * 0.075, waist[3] + 0.03, 0.98, 0.014, 0.006), (side * 0.085, chest_y, 1.25, 0.014, 0.006),
                     (side * 0.1, chest_y - 0.04, 1.43, 0.014, 0.006), (side * 0.1, 0.0, 1.5, 0.014, 0.006),
                     (side * 0.07, back_y + 0.02, 1.4, 0.014, 0.006), (0.0, back_y, 1.2, 0.016, 0.006)]
            p.add(chain(front, 1), "gear", TORSO_BONES, rig_bones)
        p.add(chain([(0, waist[0], 0.95, *belt_r), (0, waist[0], 0.99, *belt_r)], 1), "leather", PELVIS_BONES, rig_bones)

        p = piece(f"{g}_gear_backpack")
        back = -body.torso[5][3] - 0.07
        p.add(chain([(0, back, 1.12, 0.13, 0.065), (0, back - 0.01, 1.4, 0.12, 0.06)], 1), "gear", ["chest"], rig_bones)
        p.add(rounded_box((0.0, back - 0.06, 1.22), (0.16, 0.035, 0.1)), "gear", ["chest"], rig_bones)
        p.add(chain([(0.0, back - 0.02, 1.45, 0.05, 0.03), (0.0, back - 0.02, 1.47, 0.05, 0.03)], 1), "leather", ["chest"],
              rig_bones)
        for side in (-1, 1):
            strap = [(side * 0.09, back + 0.06, 1.42, 0.02, 0.008), (side * 0.1, 0.0, 1.5, 0.02, 0.008),
                     (side * 0.1, chest_y - 0.02, 1.38, 0.02, 0.008), (side * 0.11, chest_y, 1.15, 0.02, 0.008)]
            p.add(chain(strap, 1), "leather", TORSO_BONES, rig_bones)

    # --- Shared pieces: hands, boots, hair, facial hair, headwear, eyewear.
    p = piece("x_hands")
    for side in (-1, 1):
        s = "R" if side > 0 else "L"
        _, wrist = Body("m", rig_bones).bone(f"forearm.{s}")
        head, tail = Body("m", rig_bones).bone(f"hand.{s}")
        d = (tail - head).normalized()
        knuckles = tail + d * 0.01
        fingers = knuckles + d * 0.018 + Vector((0, 0, -0.03))
        nodes = [(*wrist, 0.028, 0.028), (*head.lerp(tail, 0.5), 0.034, 0.03), (*knuckles, 0.03, 0.026),
                 (*fingers, 0.022, 0.022)]
        p.add(chain(nodes, 1), "skin", ["forearm." + s, "hand." + s], rig_bones)
        thumb_base = head.lerp(tail, 0.4) + Vector((-0.02 * side, 0.0, 0.02))
        p.add(chain([(*thumb_base, 0.013, 0.013), (*(thumb_base + d * 0.04 + Vector((0, 0, 0.005))), 0.011, 0.011)], 1),
              "skin", ["hand." + s], rig_bones)

    def boots(name, top_z):
        p = piece(f"x_boots_{name}")
        for side in (-1, 1):
            s = "R" if side > 0 else "L"
            x = 0.11 * side
            nodes = [(x, 0.0, top_z, 0.058, 0.064), (x, 0.0, 0.14, 0.052, 0.058), (x, 0.01, 0.07, 0.052, 0.062),
                     (x, 0.1, 0.05, 0.048, 0.042), (x, 0.175, 0.045, 0.043, 0.034)]
            p.add(chain(nodes, 1), "boots", [f"shin.{s}", f"foot.{s}"], rig_bones)
            p.add(chain([(x, -0.035, 0.016, 0.05, 0.016), (x, 0.19, 0.016, 0.045, 0.016)], 1), "sole", [f"foot.{s}"],
                  rig_bones)
            if top_z > 0.25:
                p.add(chain([(x, 0.0, top_z - 0.01, 0.064, 0.07), (x, 0.0, top_z + 0.01, 0.064, 0.07)], 1), "leather",
                      [f"shin.{s}"], rig_bones)

    boots("short", 0.2)
    boots("tall", 0.34)

    c = HEAD_CENTER
    piece("x_hair_buzz").add(hair_cap(1.025, front=0.05), "hair", HEAD_BONES, rig_bones)
    piece("x_hair_short").add(hair_cap(1.07), "hair", HEAD_BONES, rig_bones)
    p = piece("x_hair_slick")
    p.add(hair_cap(1.08), "hair", HEAD_BONES, rig_bones)
    p.add(ellipsoid((0.0, c.y + 0.035, c.z + 0.1), (0.075, 0.07, 0.04), 14, 8), "hair", HEAD_BONES, rig_bones)
    p = piece("x_hair_ponytail")
    p.add(hair_cap(1.08, back=-0.06), "hair", HEAD_BONES, rig_bones)
    p.add(chain([(0.0, c.y - 0.1, c.z + 0.03, 0.03, 0.03), (0.0, c.y - 0.125, c.z - 0.06, 0.028, 0.026),
                 (0.0, c.y - 0.12, c.z - 0.2, 0.018, 0.016)], 1), "hair", ["head", "neck"], rig_bones)
    p = piece("x_hair_bun")
    p.add(hair_cap(1.08, back=-0.06), "hair", HEAD_BONES, rig_bones)
    p.add(ellipsoid((0.0, c.y - 0.11, c.z + 0.04), (0.048, 0.04, 0.045), 12, 8), "hair", HEAD_BONES, rig_bones)
    p = piece("x_hair_long")
    p.add(hair_cap(1.09, back=-0.06), "hair", HEAD_BONES, rig_bones)
    p.add(ellipsoid((0.0, c.y - 0.035, c.z - 0.06), (0.1, 0.085, 0.13), 16, 10, keep=lambda n: n.y < 0.25),
          "hair", ["head", "neck"], rig_bones)

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
        return piece(name).add(lathe(outer + inner, (0.0, c.y - 0.005, c.z + 0.025), scale=(1.07, 1.07 * stretch, 1.07),
                                     segments=24),
                               mat, HEAD_BONES, rig_bones)

    helmet("x_headwear_helmet", [(0.0, 0.13), (0.06, 0.125), (0.105, 0.1), (0.13, 0.06), (0.142, 0.015), (0.146, -0.012),
                                 (0.16, -0.02), (0.163, -0.03)], "helmet")
    helmet("x_headwear_stahlhelm", [(0.0, 0.125), (0.065, 0.12), (0.11, 0.095), (0.132, 0.05), (0.137, 0.0),
                                    (0.14, -0.035), (0.158, -0.06), (0.17, -0.075)], "helmet_grey", stretch=1.08)
    p = piece("x_headwear_cap")
    p.add(lathe([(0.0, 0.1), (0.098, 0.1), (0.1, 0.03), (0.095, -0.01), (0.0, -0.01)], (0.0, c.y - 0.008, c.z + 0.035),
                scale=(1.1, 1.2, 1.08)), "top", HEAD_BONES, rig_bones)
    p.add(ellipsoid((0.0, c.y + 0.12, c.z + 0.03), (0.07, 0.05, 0.007), 14, 6), "top_detail", HEAD_BONES, rig_bones)
    tilt = Matrix.Rotation(math.radians(-14), 3, "Y")
    piece("x_headwear_beret").add(transformed(ellipsoid((0.0, 0.0, 0.0), (0.126, 0.137, 0.04), 18, 10),
                                              Matrix.Translation(c + Vector((-0.015, -0.01, 0.092))) @ tilt.to_4x4()),
                                  "beret", HEAD_BONES, rig_bones)
    p = piece("x_headwear_hat")
    p.add(lathe([(0.0, 0.115), (0.075, 0.12), (0.092, 0.1), (0.098, 0.03), (0.0, 0.03)], (0.0, c.y - 0.005, c.z + 0.02),
                scale=(1.1, 1.2, 1.08)), "felt", HEAD_BONES, rig_bones)
    p.add(lathe([(0.0, 0.034), (0.185, 0.03), (0.2, 0.02), (0.185, 0.022), (0.0, 0.026)], (0.0, c.y - 0.005, c.z + 0.02),
                scale=(1.1, 1.19, 1.08), segments=24), "felt", HEAD_BONES, rig_bones)
    p.add(lathe([(0.097, 0.032), (0.1, 0.032), (0.1, 0.05), (0.096, 0.05)], (0.0, c.y - 0.005, c.z + 0.02),
                scale=(1.1, 1.2, 1.08), closed=True), "leather", HEAD_BONES, rig_bones)
    p = piece("x_headwear_beanie")
    p.add(lathe([(0.0, 0.135), (0.06, 0.13), (0.1, 0.1), (0.098, 0.03), (0.096, -0.0), (0.0, -0.0)],
                (0.0, c.y - 0.008, c.z + 0.02), scale=(1.1, 1.19, 1.08)), "wool", HEAD_BONES, rig_bones)
    p.add(lathe([(0.097, -0.002), (0.104, -0.002), (0.104, 0.035), (0.097, 0.035)], (0.0, c.y - 0.008, c.z + 0.02),
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
