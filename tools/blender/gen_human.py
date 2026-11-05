"""Realistic soldiers from MakeHuman CC0 data, generated headless in Blender.

Run (after tools/fetch_assets.py has cached the MakeHuman data):
    blender --background --factory-startup --python tools/blender/gen_human.py

For each body (male, female) this:
  1. shapes the MakeHuman base mesh with macro targets (young adult, athletic),
  2. fits eyes, eyebrows, eyelashes, hair styles, outfits and shoes (MHCLO proxies),
  3. poses everything into the rifle-holding rest pose with the MakeHuman game_engine rig weights
     (fingers curled around the grip) and bakes it,
  4. builds our 17-bone skeleton at the posed joints, remaps the weights to it, adds the shared
     animations from gen_character.py and the helmets/hats/eyewear from character_parts.py,
  5. writes assets/models/characters/soldier_<m|f>.glb and the textures into
     assets/textures/characters/ (not committed, like the other fetched textures).

Material names tell the engine what to load: "char_<texture>" (opaque) or "charalpha_<texture>"
(alpha-tested) map to assets/textures/characters/<texture>.{jpg,png} plus an optional
<texture>_normal file. The body uses "char_skin" and gets its texture from the player's look.
Both bodies are scaled to the same height: hit capsules are identical for everyone.
"""
import json
import math
import pathlib
import sys

import bmesh
import bpy
import mathutils.kdtree
from mathutils import Matrix, Vector

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import character_parts  # noqa: E402
import gen_character  # noqa: E402
import makehuman as mh  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEXTURES = ROOT / "assets" / "textures" / "characters"
HEIGHT = 1.78  # Top of the head, both bodies.

SHAPES = {
    "m": dict(muscle=0.7, weight=0.5, height=0.6, proportions=0.9),
    "f": dict(muscle=0.55, weight=0.45, height=0.6, proportions=0.9),
}
SKINS = ["young_caucasian", "young_asian", "young_african", "middleage_caucasian", "middleage_asian", "middleage_african"]
HAIR = ["short01", "short02", "short03", "short04", "afro01", "bob01", "bob02", "braid01", "long01", "ponytail01"]
OUTFITS = {
    "m": ["male_worksuit01", "male_casualsuit01", "male_casualsuit02", "male_casualsuit03", "male_casualsuit04",
          "male_casualsuit05", "male_casualsuit06"],
    "f": ["male_worksuit01", "female_casualsuit01", "female_casualsuit02", "female_sportsuit01"],
}
SHOES = ["shoes01", "shoes02", "shoes03", "shoes04", "shoes05", "shoes06"]
EYEBROWS = {"m": "eyebrow001", "f": "eyebrow010"}

# Rifle rest pose. Legs: bone directions of gen_character.BONES (Blender space). Arms: IK to a rifle
# held at the right shoulder (GUN_REST is the weapon origin, at the trigger; weapon axes = body axes).
GUN_REST = Vector((0.10, 0.40, 1.40))
# Per hand, in weapon space (Blender axes: +X right, +Y forward, +Z up): wrist position, direction of the
# fingers (wrist to knuckles), index-minus-pinky knuckle line, and where the elbow points.
HOLD = {
    "r": dict(wrist=(0.04, -0.075, -0.005), fingers=(-0.25, 1.0, -0.35), knuckles=(0.0, 0.35, 1.0),
              pole=(0.8, -0.2, -1.0), curl=(50, 65, 40), thumb=25),
    "l": dict(wrist=(-0.035, 0.16, -0.03), fingers=(0.55, 0.75, 0.1), knuckles=(-0.75, 0.55, 0.0),
              pole=(-0.4, -0.1, -1.0), curl=(30, 45, 25), thumb=10),
}
# Bladed stance: chest turned (degrees about the vertical, negative brings the left shoulder forward).
TWIST = (("spine_02", -12.0), ("spine_03", -13.0), ("neck_01", 12.0), ("head", 13.0))
AIM = {
    "upperarm_r": "upperarm.R", "lowerarm_r": "forearm.R", "hand_r": "hand.R",
    "upperarm_l": "upperarm.L", "lowerarm_l": "forearm.L", "hand_l": "hand.L",
    "thigh_r": "thigh.R", "calf_r": "shin.R", "thigh_l": "thigh.L", "calf_l": "shin.L",
}


def bone_dir(name):
    head, tail, _ = gen_character.BONES[name]
    return (Vector(tail) - Vector(head)).normalized()


# --- Textures ---------------------------------------------------------------------------------------

_written = set()


def export_texture(source, name, max_size=1024, alpha=False, grayscale=False):
    """Copies a MakeHuman texture into assets/textures/characters (resized, jpg or png)."""
    if source is None or not pathlib.Path(source).exists() or name in _written:
        return name
    _written.add(name)
    TEXTURES.mkdir(parents=True, exist_ok=True)
    image = bpy.data.images.load(str(source))
    w, h = image.size
    if max(w, h) > max_size:
        image.scale(max_size * w // max(w, h), max_size * h // max(w, h))
    if grayscale:
        # Tintable hair: luminance normalized so the brightest strands are near white.
        px = list(image.pixels)
        lum = [0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2] for i in range(0, len(px), 4)]
        visible = sorted(l for i, l in enumerate(lum) if px[i * 4 + 3] > 0.5) or [1.0]
        top = max(visible[int(len(visible) * 0.95)], 1e-3)
        for i, l in enumerate(lum):
            v = min(1.0, l / top)
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v
        image.pixels[:] = px
    image.filepath_raw = str(TEXTURES / f"{name}.{'png' if alpha else 'jpg'}")
    image.file_format = "PNG" if alpha else "JPEG"
    if not alpha:
        bpy.context.scene.render.image_settings.quality = 90
    image.save()
    bpy.data.images.remove(image)
    return name


def material(name):
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (1, 1, 1, 1)
    mat.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.6
    return mat


def proxy_material(mhclo, fallback, alpha=False, max_size=1024, prefix="", grayscale=False):
    info = mh.read_mhmat(mhclo.material)
    name = prefix + (pathlib.Path(info["diffuse"]).stem.lower() if info["diffuse"] else fallback)
    alpha = alpha or info["transparent"]
    export_texture(info["diffuse"], name, max_size, alpha, grayscale)
    if info["normal"]:
        export_texture(info["normal"], f"{name}_normal", max_size)
    return ("charalpha_" if alpha else "char_") + name


# --- Mesh building ----------------------------------------------------------------------------------

class Part:
    def __init__(self, name):
        self.name = name
        self.verts, self.weights, self.faces, self.face_uvs, self.face_mats = [], [], [], [], []
        self.materials = []

    def add(self, positions, weights, faces, uvs, mat):
        """faces: lists of (vertex index, uv index) in the given positions/uvs."""
        if mat not in self.materials:
            self.materials.append(mat)
        slot = self.materials.index(mat)
        base = len(self.verts)
        self.verts.extend(positions)
        self.weights.extend(weights)
        for face in faces:
            self.faces.append([base + v for v, _ in face])
            self.face_uvs.append([uvs[t] if 0 <= t < len(uvs) else (0.0, 0.0) for _, t in face])
            self.face_mats.append(slot)

    def compact(self):
        """Drops vertices no face uses (after splitting a shared vertex list)."""
        used = sorted({v for f in self.faces for v in f})
        remap = {v: i for i, v in enumerate(used)}
        self.verts = [self.verts[v] for v in used]
        self.weights = [self.weights[v] for v in used]
        self.faces = [[remap[v] for v in f] for f in self.faces]

    def build(self, rig):
        mesh = bpy.data.meshes.new(self.name)
        mesh.from_pydata([tuple(v) for v in self.verts], [], self.faces)
        uv = mesh.uv_layers.new(name="UVMap")
        for poly, corners in zip(mesh.polygons, self.face_uvs):
            for loop_index, coord in zip(poly.loop_indices, corners):
                uv.data[loop_index].uv = coord
        for name in self.materials:
            mesh.materials.append(material(name))
        for poly, slot in zip(mesh.polygons, self.face_mats):
            poly.material_index = slot
            poly.use_smooth = True
        mesh.validate(clean_customdata=False)
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


def proxy_part(name, mhclo_path, base_verts, base_w, pose, place, mat, faces_filter=None):
    clo = mh.Mhclo(mhclo_path)
    obj = mh.Obj(clo.obj_file)
    fitted = clo.fit(base_verts)
    weights = clo.weights(base_w)
    posed = pose.skin(fitted, weights)
    part = Part(name)
    faces = obj.faces if faces_filter is None else [f for f in obj.faces if faces_filter(f)]
    part.add([place(p) for p in posed], [mh.map_weights(w) for w in weights], faces, obj.uvs, mat)
    return part, clo


def build_body(gender):
    base, verts = mh.shaped_base(gender, **SHAPES[gender])
    bones = mh.rig_bones(base, verts)
    base_w = mh.base_weights()

    # Rifle pose, stage 1: legs along the old skeleton, a bladed stance (chest turned so the left
    # shoulder leads, head turned back to the front).
    pose = mh.Pose(bones)
    for mh_bone, ours in AIM.items():
        pose.aim(mh_bone, mh.from_blender_dir(bone_dir(ours)))
    up = mh.from_blender_dir(Vector((0.0, 0.0, 1.0)))
    for name, degrees in TWIST:
        pose.rotate(name, up, math.radians(degrees))

    # Body: the "body" group of the base mesh only (helpers and joint cubes dropped).
    body_faces = [f for f, g in zip(base.faces, base.face_groups) if g == "body"]
    used = sorted({v for f in body_faces for v, _ in f})
    remap = {v: i for i, v in enumerate(used)}
    body_w = [base_w.get(v, {}) for v in used]

    # Placement (MakeHuman space -> Blender, scaled to HEIGHT with the soles at the origin). The arms
    # never reach the soles or the top of the head, so it is measured before posing them.
    standing = [mh.to_blender(p) for p in pose.skin([verts[v] for v in used], body_w)]
    top = max(p.z for p in standing)
    bottom = min(p.z for p in standing)
    scale = (HEIGHT - 0.03) / (top - bottom)

    def place(p):
        b = mh.to_blender(p)
        return Vector((b.x * scale, b.y * scale, (b.z - bottom) * scale + 0.03))

    def unplace(v):
        return Vector((-v.x / scale * 10.0, ((v.z - 0.03) / scale + bottom) * 10.0, v.y / scale * 10.0))

    # Stage 2: both hands on the rifle (IK for the wrists, explicit hand frames: the palm of the right
    # hand wraps the grip from the right side, the left palm supports the forend from below).
    def frame(direction, knuckles):
        f = direction.normalized()
        k = (knuckles - f * knuckles.dot(f)).normalized()
        return Matrix((f, k, f.cross(k))).transposed()

    for side, hold in HOLD.items():
        target = GUN_REST + Vector(hold["wrist"])
        miss = pose.solve_arm(f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}", unplace(target),
                              mh.from_blender_dir(Vector(hold["pole"])))
        mats = pose.matrices()
        m = mats[f"hand_{side}"]
        now = frame(m @ bones[f"middle_01_{side}"][0] - m @ bones[f"hand_{side}"][0],
                    m @ bones[f"index_01_{side}"][0] - m @ bones[f"pinky_01_{side}"][0])
        wanted = frame(mh.from_blender_dir(Vector(hold["fingers"])), mh.from_blender_dir(Vector(hold["knuckles"])))
        pose.orient(f"hand_{side}", now, wanted)
        print(f"[origon] {gender} arm {side}: wrist misses its target by {miss * 0.1 * scale * 100:.1f} cm")
    # Fingers curled around the grip (the support hand cups the forend: a looser wrap).
    for side, hold in HOLD.items():
        knuckles = bones[f"index_01_{side}"][0] - bones[f"pinky_01_{side}"][0]
        axis = knuckles.normalized() * (1.0 if side == "l" else -1.0)
        for finger in ("index", "middle", "ring", "pinky"):
            for segment, angle in zip(("01", "02", "03"), hold["curl"]):
                pose.rotate(f"{finger}_{segment}_{side}", axis, math.radians(angle) * CURL_SIGN)
        pose.rotate(f"thumb_02_{side}", axis, math.radians(hold["thumb"]) * CURL_SIGN)

    posed_bones = pose.posed_bones()
    skeleton = mh.our_skeleton(posed_bones)
    posed = pose.skin([verts[v] for v in used], body_w)

    parts = []
    # Body split by the garments that hide each face (MHCLO delete_verts), so skin never pokes
    # through clothes: the engine hides a part when the worn outfit or shoes are in its list.
    hiders = {}
    for item in OUTFITS[gender]:
        hiders[item] = mh.Mhclo(mh.ASSETS / "clothes" / item / f"{item}.mhclo").delete
    for item in SHOES:
        hiders[item] = mh.Mhclo(mh.ASSETS / "clothes" / item / f"{item}.mhclo").delete
    groups = {}
    for face in body_faces:
        key = tuple(sorted(item for item, deleted in hiders.items() if any(v in deleted for v, _ in face)))
        groups.setdefault(key, []).append(face)
    body_positions = [place(p) for p in posed]
    body_weights = [mh.map_weights(w) for w in body_w]
    hidden_by = {}
    head_points = []
    for index, (key, faces) in enumerate(sorted(groups.items(), key=lambda kv: -len(kv[1]))):
        name = f"x_skin_{index:02d}"
        part = Part(name)
        part.add(body_positions, body_weights, [[(remap[v], t) for v, t in f] for f in faces], base.uvs, "char_skin")
        part.compact()
        parts.append(part)
        hidden_by[name] = list(key)
    head_points = [p for p, w in zip(body_positions, body_weights) if w.get("head", 0.0) > 0.95]

    for skin in SKINS:
        name = f"{skin}_{'male' if gender == 'm' else 'female'}"
        info = mh.read_mhmat(next((mh.ASSETS / "skins" / name).glob("*.mhmat"), None))
        export_texture(info["diffuse"], f"skin_{name}", 2048)

    def add_proxy(name, path, mat=None, alpha=False, max_size=1024, prefix="", grayscale=False):
        clo = mh.Mhclo(path)
        material_name = mat or proxy_material(clo, name, alpha, max_size, prefix, grayscale)
        part, _ = proxy_part(name, path, verts, base_w, pose, place, material_name)
        parts.append(part)
        return part

    add_proxy("x_eyes", mh.ASSETS / "eyes" / "low-poly" / "low-poly.mhclo", mat="char_" + export_texture(
        mh.ASSETS / "eyes" / "materials" / "brown_eye.png", "eye_brown", 512))
    add_proxy("x_eyebrows", mh.ASSETS / "eyebrows" / EYEBROWS[gender] / f"{EYEBROWS[gender]}.mhclo", alpha=True, max_size=512,
              prefix="hair_", grayscale=True)
    add_proxy("x_eyelashes", mh.ASSETS / "eyelashes" / "eyelashes01" / "eyelashes01.mhclo", alpha=True, max_size=512)
    for hair in HAIR:
        add_proxy(f"x_hair_{hair}", mh.ASSETS / "hair" / hair / f"{hair}.mhclo", alpha=True, prefix="hair_", grayscale=True)
    for outfit in OUTFITS[gender]:
        add_proxy(f"x_outfit_{outfit}", mh.ASSETS / "clothes" / outfit / f"{outfit}.mhclo")
    for shoes in SHOES:
        add_proxy(f"x_shoes_{shoes}", mh.ASSETS / "clothes" / shoes / f"{shoes}.mhclo")
    add_proxy("x_headwear_fedora", mh.ASSETS / "clothes" / "fedora01" / "fedora01.mhclo")

    rig_bones = {name: (place(h), place(t)) for name, (h, t) in skeleton.items()}
    # Weapon joint (child of the right hand): where the rifle's origin (trigger) sits in the rest pose,
    # axes as the weapon models (muzzle along +Y). The engine attaches every weapon to it.
    rig_bones["weapon"] = (GUN_REST.copy(), GUN_REST + Vector((0.0, 0.1, 0.0)))
    return parts, rig_bones, head_points, hidden_by


CURL_SIGN = -1.0


LODS = ((1, 0.22), (2, 0.07))  # (level, decimation ratio): drawn beyond ~10 m and ~30 m.
TINY = ("x_eyes", "x_eyebrows", "x_eyelashes")


def decimated_copy(source, name, ratio, rig):
    """A collapsed copy of a skinned object (vertex groups and UVs are interpolated)."""
    obj = bpy.data.objects.new(name, source.data.copy())
    bpy.context.scene.collection.objects.link(obj)
    for group in source.vertex_groups:
        obj.vertex_groups.new(name=group.name)
    modifier = obj.modifiers.new("decimate", "DECIMATE")
    modifier.ratio = ratio
    modifier.use_collapse_triangulate = True
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), preserve_all_data_layers=True,
                                           depsgraph=depsgraph)
    obj.modifiers.clear()
    obj.data = mesh
    obj.parent = rig
    obj.modifiers.new("rig", "ARMATURE").object = rig
    return obj


def make_lods(objects, rig):
    """LOD1/LOD2 for every piece. The skin parts (split for clothing) merge into one body: at
    distance nothing pokes through visibly. Eyes, brows and lashes have no LOD (hidden far away)."""
    skin = [o for o in objects if o.name.startswith("x_skin_")]
    merged = None
    if skin:
        merged = bpy.data.objects.new("x_skinall", bpy.data.meshes.new("x_skinall"))
        bpy.context.scene.collection.objects.link(merged)
        for group in skin[0].vertex_groups:
            merged.vertex_groups.new(name=group.name)
        bpy.ops.object.select_all(action="DESELECT")
        copies = []
        for o in skin:
            c = bpy.data.objects.new(o.name + "_tmp", o.data.copy())
            bpy.context.scene.collection.objects.link(c)
            for group in o.vertex_groups:
                c.vertex_groups.new(name=group.name)
            copies.append(c)
        for c in copies:
            c.select_set(True)
        bpy.context.view_layer.objects.active = copies[0]
        bpy.ops.object.join()
        merged = copies[0]
        merged.name = "x_skinall"
        # Weld the seams between the split parts so decimation does not open cracks.
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.remove_doubles(threshold=0.0001)
        bpy.ops.object.mode_set(mode="OBJECT")
    out = []
    sources = [o for o in objects if not o.name.startswith("x_skin_") and o.name not in TINY]
    # Nearest original skin face -> its part, to split the decimated body like LOD0.
    tree = None
    if skin:
        centers = [(poly.center.copy(), part.name) for part in skin for poly in part.data.polygons]
        tree = mathutils.kdtree.KDTree(len(centers))
        for i, (c, _) in enumerate(centers):
            tree.insert(c, i)
        tree.balance()
    for level, ratio in LODS:
        for o in sources:
            # Small accessories are already light: keep more of them.
            r = ratio if len(o.data.polygons) > 1500 else min(1.0, ratio * 2.5)
            out.append(decimated_copy(o, f"{o.name}_LOD{level}", r, rig))
        if merged:
            body = decimated_copy(merged, "x_skin_tmp", ratio, rig)
            owner = [centers[tree.find(poly.center)[1]][1] for poly in body.data.polygons]
            for part in skin:
                keep = {i for i, name in enumerate(owner) if name == part.name}
                if not keep:
                    continue
                bm = bmesh.new()
                bm.from_mesh(body.data)
                bm.faces.ensure_lookup_table()
                bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index not in keep], context="FACES")
                bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
                mesh = bpy.data.meshes.new(f"{part.name}_LOD{level}")
                bm.to_mesh(mesh)
                bm.free()
                for material in body.data.materials:
                    mesh.materials.append(material)
                piece = bpy.data.objects.new(f"{part.name}_LOD{level}", mesh)
                bpy.context.scene.collection.objects.link(piece)
                for group in body.vertex_groups:
                    piece.vertex_groups.new(name=group.name)
                piece.parent = rig
                piece.modifiers.new("rig", "ARMATURE").object = rig
                out.append(piece)
            bpy.data.objects.remove(body)
    if merged:
        bpy.data.objects.remove(merged)
    return out


def main():
    for gender in ("m", "f"):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.render.fps = gen_character.FPS
        parts, rig_bones, head_points, hidden_by = build_body(gender)
        parents = {name: parent for name, (_, _, parent) in gen_character.BONES.items()}
        parents["weapon"] = "hand.R"
        bones = {name: (tuple(h), tuple(t), parents[name]) for name, (h, t) in rig_bones.items()}
        rig = gen_character.make_armature(bones)
        objects = [part.build(rig) for part in parts]

        # Helmets, caps and eyewear: built for the nominal head of character_parts, then scaled to
        # this head (eye height, skull width and depth measured on the mesh).
        xs = [p.x for p in head_points]
        ys = [p.y for p in head_points]
        top = max(p.z for p in head_points)
        eyes = next(o for o in objects if o.name == "x_eyes")
        eye_z = sum(v.co.z for v in eyes.data.vertices) / len(eyes.data.vertices)
        nominal = (0.093, 0.109, 0.123)
        k = Vector(((max(xs) - min(xs)) * 0.5 * 0.94 / nominal[0], (max(ys) - min(ys)) * 0.5 * 0.9 / nominal[1],
                    (top - eye_z) / (nominal[2] - 0.012)))
        center = Vector(((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5 - 0.004, eye_z - 0.012 * k.z))
        print(f"[origon] head center {tuple(round(c, 3) for c in center)} scale {tuple(round(c, 3) for c in k)}")
        character_parts.HEAD_CENTER = Vector((0.0, 0.0, 0.0))
        character_parts.HEAD_R = nominal
        simple = {name: (Vector(h), Vector(t)) for name, (h, t) in rig_bones.items()}
        for piece in character_parts.build_headgear(simple):
            piece.verts = [center + Vector((v.x * k.x, v.y * k.y, v.z * k.z)) for v in piece.verts]
            piece.weights = [{"head": 1.0} for _ in piece.verts]
            objects.append(piece.build(rig))

        objects += make_lods(objects, rig)
        (gen_character.OUTPUT / f"soldier_{gender}.pieces.json").write_text(json.dumps({"hidden_by": hidden_by}, indent=1))
        gen_character.add_animations(rig)
        gen_character.export(rig, objects, f"soldier_{gender}.glb")


if __name__ == "__main__":
    main()
