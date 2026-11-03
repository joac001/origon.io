"""Low-poly WWII weapon models for Origon, generated headless in Blender.

Run:
    blender --background --factory-startup --python tools/blender/gen_weapons.py -- [weapon_id ...]

Writes assets/models/weapons/<id>.glb with a single <id>_LOD0 node.
Conventions: meters, the muzzle points along Blender +Y (engine -Z), the origin sits at the
trigger/grip, so the engine can hold every weapon the same way.
Materials are plain PBR factors: gun metal, oiled wood, bakelite and blued steel.
"""
import math
import pathlib
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "assets" / "models" / "weapons"

MATERIALS = {
    "metal": ((0.045, 0.045, 0.05), 0.38, 0.9),
    "steel": ((0.11, 0.11, 0.12), 0.3, 1.0),
    "wood": ((0.24, 0.11, 0.045), 0.55, 0.0),
    "darkwood": ((0.13, 0.06, 0.03), 0.5, 0.0),
    "bakelite": ((0.06, 0.035, 0.02), 0.45, 0.0),
    "blade": ((0.55, 0.55, 0.56), 0.22, 1.0),
}


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


class Part:
    def __init__(self):
        self.bm = bmesh.new()
        self.materials = []

    def _slot(self, name):
        mat = material(name)
        if mat not in self.materials:
            self.materials.append(mat)
        return self.materials.index(mat)

    def box(self, center, size, mat, taper=1.0, rot_x=0.0):
        """Box from center/size (x width, y length, z height). taper scales the +Y end."""
        slot = self._slot(mat)
        hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
        corners = []
        for y in (-hy, hy):
            s = taper if y > 0 else 1.0
            for x, z in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz)):
                corners.append(Vector((x * s, y, z * s)))
        rot = Matrix.Rotation(rot_x, 3, "X")
        verts = [self.bm.verts.new(Vector(center) + rot @ c) for c in corners]
        faces = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        for f in faces:
            face = self.bm.faces.new([verts[i] for i in f])
            face.material_index = slot

    def cylinder(self, start, length, radius, mat, sides=10, axis="y", radius_end=None):
        slot = self._slot(mat)
        radius_end = radius if radius_end is None else radius_end
        start = Vector(start)
        direction = {"y": Vector((0, 1, 0)), "z": Vector((0, 0, 1)), "x": Vector((1, 0, 0))}[axis]
        u = Vector((1, 0, 0)) if axis != "x" else Vector((0, 1, 0))
        v = direction.cross(u).normalized()
        rings = []
        for t, r in ((0.0, radius), (1.0, radius_end)):
            center = start + direction * length * t
            ring = [self.bm.verts.new(center + (u * math.cos(a) + v * math.sin(a)) * r)
                    for a in [2 * math.pi * i / sides for i in range(sides)]]
            rings.append(ring)
        for i in range(sides):
            j = (i + 1) % sides
            f = self.bm.faces.new([rings[0][i], rings[0][j], rings[1][j], rings[1][i]])
            f.material_index = slot
            f.smooth = True
        for ring, rev in ((rings[0], True), (rings[1], False)):
            f = self.bm.faces.new(list(reversed(ring)) if rev else ring)
            f.material_index = slot

    def finish(self, name):
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        mesh = bpy.data.meshes.new(name)
        self.bm.to_mesh(mesh)
        self.bm.free()
        for mat in self.materials:
            mesh.materials.append(mat)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


# Spec fields (meters): receiver (length, height, width), barrel (length, radius), stock,
# handguard length, magazine type, scope, pistol grip.
WEAPONS = {
    "mp40": dict(receiver=(0.32, 0.055, 0.04), barrel=(0.2, 0.009), stock="folded", mag="stick", mag_len=0.25, grip="bakelite"),
    "thompson": dict(receiver=(0.3, 0.07, 0.045), barrel=(0.26, 0.012), stock="wood", mag="stick", mag_len=0.2, grip="wood",
                     foregrip=True, fins=True),
    "ppsh41": dict(receiver=(0.3, 0.065, 0.05), barrel=(0.27, 0.017), stock="wood_full", mag="drum", grip=None, shroud=True),
    "stg44": dict(receiver=(0.34, 0.07, 0.045), barrel=(0.25, 0.01), stock="wood_straight", mag="curved", mag_len=0.22,
                  grip="bakelite", handguard=0.14),
    "bar": dict(receiver=(0.42, 0.075, 0.05), barrel=(0.45, 0.012), stock="wood", mag="box", mag_len=0.12, grip=None,
                handguard=0.22, bipod=True),
    "bren": dict(receiver=(0.4, 0.075, 0.05), barrel=(0.48, 0.013), stock="bren", mag="top", grip="wood", bipod=True,
                 carry=True),
    "sten": dict(receiver=(0.3, 0.035, 0.035), barrel=(0.12, 0.012), stock="frame", mag="side", mag_len=0.22, grip=None,
                 tube=True),
    "m3": dict(receiver=(0.24, 0.06, 0.06), barrel=(0.14, 0.01), stock="wire", mag="stick", mag_len=0.2, grip="metal",
               tube=True),
    "pps42": dict(receiver=(0.3, 0.06, 0.04), barrel=(0.2, 0.014), stock="folded", mag="curved", mag_len=0.2, grip="wood",
                  shroud=True),
    "trenchgun": dict(receiver=(0.2, 0.06, 0.045), barrel=(0.5, 0.012), stock="wood", mag=None, grip=None, handguard=0.16,
                      tube_mag=True, heatshield=True),
    "m1carbine": dict(receiver=(0.2, 0.05, 0.04), barrel=(0.45, 0.009), stock="rifle", mag="box", mag_len=0.08, grip=None,
                      handguard=0.3),
    "garand": dict(receiver=(0.24, 0.055, 0.042), barrel=(0.6, 0.009), stock="rifle", mag=None, grip=None, handguard=0.4),
    "g43": dict(receiver=(0.24, 0.055, 0.042), barrel=(0.55, 0.009), stock="rifle", mag="box", mag_len=0.09, grip=None,
                handguard=0.35),
    "svt40": dict(receiver=(0.24, 0.055, 0.042), barrel=(0.6, 0.009), stock="rifle", mag="box", mag_len=0.1, grip=None,
                  handguard=0.3, muzzle_brake=True),
    "enfield": dict(receiver=(0.22, 0.05, 0.04), barrel=(0.62, 0.009), stock="rifle", mag="box", mag_len=0.07, grip=None,
                    handguard=0.5, bolt=True),
    "mosin": dict(receiver=(0.22, 0.05, 0.04), barrel=(0.72, 0.009), stock="rifle", mag=None, grip=None, handguard=0.55,
                  bolt=True),
    "kar98k_scoped": dict(receiver=(0.22, 0.05, 0.04), barrel=(0.62, 0.009), stock="rifle", mag=None, grip=None,
                          handguard=0.45, bolt=True, scope=True),
    "webley": dict(pistol="revolver"),
    "m1911": dict(pistol="auto"),
    "knife": dict(knife=True),
}


def build_pistol(p, kind):
    if kind == "revolver":
        p.cylinder((0, 0.03, 0.03), 0.16, 0.009, "steel")
        p.cylinder((0, 0.0, 0.025), 0.045, 0.022, "metal")
        p.box((0, -0.01, 0.035), (0.028, 0.08, 0.04), "metal")
        p.box((0, -0.05, -0.02), (0.03, 0.04, 0.09), "bakelite", rot_x=0.35)
    else:
        p.box((0, 0.03, 0.035), (0.028, 0.2, 0.035), "metal")
        p.box((0, -0.04, -0.02), (0.03, 0.05, 0.09), "wood", rot_x=0.25)
        p.box((0, 0.005, 0.005), (0.006, 0.03, 0.02), "metal")


def build_knife(p):
    p.box((0, 0.13, 0.0), (0.004, 0.17, 0.03), "blade", taper=0.3)
    p.box((0, 0.04, 0.0), (0.03, 0.012, 0.045), "steel")
    p.box((0, -0.02, 0.0), (0.025, 0.11, 0.03), "darkwood")


def build_long(p, s):
    rl, rh, rw = s["receiver"]
    # Receiver centered slightly forward of the grip.
    rec_center = Vector((0, rl * 0.25, 0.03))
    p.box(rec_center, (rw, rl, rh), "metal")
    front = rec_center.y + rl / 2
    bl, br = s["barrel"]
    if s.get("shroud"):
        p.cylinder((0, front, 0.035), bl * 0.85, br * 1.6, "metal", sides=8)
    if s.get("fins"):
        p.cylinder((0, front, 0.035), bl * 0.45, br * 1.5, "metal", sides=12)
    p.cylinder((0, front, 0.035), bl, br, "steel")
    muzzle = front + bl
    if s.get("muzzle_brake"):
        p.cylinder((0, muzzle - 0.02, 0.035), 0.05, br * 1.7, "steel")
    if s.get("tube"):
        p.cylinder((0, rec_center.y - rl / 2, 0.03), rl, rh * 0.55, "metal", sides=10)

    hg = s.get("handguard")
    if hg:
        p.box((0, front + hg / 2 - 0.02, 0.02), (rw * 1.2, hg, rh * 0.9), "wood")
    if s.get("tube_mag"):
        p.cylinder((0, front, 0.012), bl * 0.75, br * 1.1, "steel")
        p.box((0, front + 0.1, 0.008), (rw * 1.25, 0.14, 0.03), "wood")
    if s.get("heatshield"):
        p.box((0, front + bl * 0.6, 0.048), (0.03, bl * 0.4, 0.012), "metal")

    stock = s.get("stock")
    back = rec_center.y - rl / 2
    if stock in ("wood", "wood_full", "wood_straight", "rifle"):
        drop = {"wood": -0.03, "wood_full": -0.04, "wood_straight": 0.0, "rifle": -0.035}[stock]
        length = 0.3 if stock != "rifle" else 0.34
        p.box((0, back - length / 2, 0.02 + drop / 2), (rw * 1.1, length, rh * 1.6), "wood", taper=1.0, rot_x=-0.06)
        if stock == "rifle":
            # Full-length rifle stock under the receiver, wrist and butt.
            p.box((0, rec_center.y, -0.005), (rw * 1.15, rl * 1.05, rh * 0.8), "wood")
        if stock == "wood_full":
            p.box((0, rec_center.y + 0.05, 0.0), (rw * 1.2, rl * 0.9, rh * 0.9), "wood")
    elif stock == "folded":
        p.box((0, back - 0.12, -0.005), (0.012, 0.24, 0.012), "metal")
        p.box((0, back - 0.24, -0.03), (0.02, 0.012, 0.07), "metal")
    elif stock in ("frame", "wire"):
        p.box((0, back - 0.13, 0.02), (0.01, 0.26, 0.01), "metal")
        p.box((0, back - 0.13, -0.03), (0.01, 0.26, 0.01), "metal")
        p.box((0, back - 0.26, -0.005), (0.015, 0.012, 0.07), "metal")
    elif stock == "bren":
        p.box((0, back - 0.17, 0.0), (rw, 0.34, rh * 1.3), "wood", taper=1.1)

    grip = s.get("grip")
    if grip:
        p.box((0, -0.01, -0.035), (0.03, 0.04, 0.09), {"bakelite": "bakelite", "wood": "wood", "metal": "metal"}[grip], rot_x=0.25)
    if s.get("foregrip"):
        p.box((0, front - 0.02, -0.02), (0.03, 0.04, 0.08), "wood", rot_x=-0.15)

    mag = s.get("mag")
    mag_len = s.get("mag_len", 0.15)
    mag_y = rec_center.y + rl * 0.15
    if mag == "stick":
        p.box((0, mag_y, -mag_len / 2), (0.025, 0.035, mag_len), "metal")
    elif mag == "curved":
        p.box((0, mag_y + 0.02, -mag_len / 2), (0.025, 0.04, mag_len), "metal", rot_x=0.25)
    elif mag == "box":
        p.box((0, mag_y, -mag_len / 2), (0.03, 0.05, mag_len), "metal")
    elif mag == "drum":
        p.cylinder((-0.045, mag_y, -0.04), 0.09, 0.07, "metal", sides=16, axis="x")
    elif mag == "side":
        p.box((-0.12, rec_center.y, 0.03), (mag_len, 0.03, 0.022), "metal")
    elif mag == "top":
        p.box((0, mag_y, 0.11), (0.025, 0.05, 0.16), "metal", rot_x=0.3)

    if s.get("bolt"):
        p.cylinder((0, rec_center.y - 0.03, 0.045), 0.06, 0.006, "steel", sides=6, axis="x")
    if s.get("scope"):
        p.cylinder((0, rec_center.y - 0.08, 0.1), 0.3, 0.018, "metal", sides=12)
        p.cylinder((0, rec_center.y - 0.1, 0.1), 0.05, 0.024, "metal", sides=12)
        p.cylinder((0, rec_center.y + 0.2, 0.1), 0.05, 0.026, "metal", sides=12)
        p.box((0, rec_center.y, 0.075), (0.012, 0.15, 0.03), "metal")
    else:
        # Front sight post.
        p.box((0, muzzle - 0.03, 0.035 + br + 0.01), (0.004, 0.01, 0.02), "metal")
    if s.get("bipod"):
        for side in (-1, 1):
            p.box((0.02 * side, muzzle - 0.08, 0.0), (0.008, 0.008, 0.1), "metal", rot_x=0.6)
    if s.get("carry"):
        p.box((0, front - 0.05, 0.075), (0.01, 0.1, 0.03), "metal")


def export(name, obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    OUTPUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT / f"{name}.glb"), export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_materials="EXPORT", export_image_format="NONE")


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    for name in args or list(WEAPONS):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        spec = WEAPONS[name]
        part = Part()
        if spec.get("pistol"):
            build_pistol(part, spec["pistol"])
        elif spec.get("knife"):
            build_knife(part)
        else:
            build_long(part, spec)
        obj = part.finish(f"{name}_LOD0")
        export(name, obj)
        print(f"[origon] weapon {name}: {len(obj.data.polygons)} faces")


if __name__ == "__main__":
    main()
