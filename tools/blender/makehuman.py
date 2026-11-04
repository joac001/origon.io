"""Reader for MakeHuman CC0 data (base mesh, morph targets, rig joints, weights, MHCLO proxies).

Only data files are read: the MakeHuman base mesh, targets and rig/weight JSON shipped in the MPFB2
repository (data released as CC0) and the "makehuman_system_assets" pack (CC0). tools/fetch_assets.py
downloads them into .cache/makehuman/. No MakeHuman or MPFB code is used.

All positions here are in MakeHuman space: decimeters, Y up, the character faces +Z, its left is +X.
to_blender() converts to the engine/Blender convention used by gen_character.py: meters, Z up,
facing +Y, right side +X.

Formats: see the MPFB docs (docs/fileformats/target.md, mhclo.md, rig.md, weights.md).
"""
import gzip
import json
import math
import pathlib

from mathutils import Matrix, Quaternion, Vector

ROOT = pathlib.Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache" / "makehuman"
MH_DATA = CACHE / "mpfb_data"
ASSETS = CACHE / "system_assets"


def to_blender(p):
    return Vector((-p[0] * 0.1, p[2] * 0.1, p[1] * 0.1))


def from_blender_dir(d):
    return Vector((-d[0], d[2], d[1]))


# --- OBJ --------------------------------------------------------------------------------------------

class Obj:
    def __init__(self, path):
        self.verts, self.uvs, self.faces, self.face_groups = [], [], [], []
        group = ""
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("v "):
                    x, y, z = line.split()[1:4]
                    self.verts.append(Vector((float(x), float(y), float(z))))
                elif line.startswith("vt "):
                    u, v = line.split()[1:3]
                    self.uvs.append((float(u), float(v)))
                elif line.startswith("g "):
                    group = line.split(maxsplit=1)[1].strip()
                elif line.startswith("f "):
                    face = []
                    for token in line.split()[1:]:
                        parts = token.split("/")
                        vi = int(parts[0]) - 1
                        ti = int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1
                        face.append((vi, ti))
                    self.faces.append(face)
                    self.face_groups.append(group)

    def group_vertices(self, name):
        out = set()
        for face, group in zip(self.faces, self.face_groups):
            if group == name:
                out.update(v for v, _ in face)
        return out


# --- Targets ---------------------------------------------------------------------------------------

def load_target(path):
    opener = gzip.open if str(path).endswith(".gz") else open
    offsets = {}
    with opener(path, "rt") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 4 or parts[0].startswith("#"):
                continue
            offsets[int(parts[0])] = Vector((float(parts[1]), float(parts[2]), float(parts[3])))
    return offsets


def apply_target(verts, name, weight):
    if weight <= 0.0:
        return
    path = MH_DATA / "targets" / f"{name}.target.gz"
    for index, offset in load_target(path).items():
        verts[index] += offset * weight


def shaped_base(gender, muscle=0.5, weight=0.5, height=0.5, proportions=0.5):
    """Base mesh vertices with macro targets for a young adult (MakeHuman macro rules, simplified:
    equal race mix, cup size and firmness at their defaults)."""
    base = Obj(MH_DATA / "3dobjs" / "base.obj")
    verts = [v.copy() for v in base.verts]
    g = "male" if gender == "m" else "female"
    for race in ("caucasian", "african", "asian"):
        apply_target(verts, f"macrodetails/{race}-{g}-young", 1.0 / 3.0)

    def split(value, low, mid, high):
        """Weights of the two neighbouring grid points (0..0.5 low-mid, 0.5..1 mid-high)."""
        if value < 0.5:
            return {low: 1.0 - value * 2.0, mid: value * 2.0}
        return {mid: 2.0 - value * 2.0, high: value * 2.0 - 1.0}

    muscles = split(muscle, "minmuscle", "averagemuscle", "maxmuscle")
    weights = split(weight, "minweight", "averageweight", "maxweight")
    for m, wm in muscles.items():
        for w, ww in weights.items():
            apply_target(verts, f"macrodetails/universal-{g}-young-{m}-{w}", wm * ww)
            if height != 0.5:
                key = "maxheight" if height > 0.5 else "minheight"
                apply_target(verts, f"macrodetails/height/{g}-young-{m}-{w}-{key}", wm * ww * abs(height - 0.5) * 2.0)
            if proportions > 0.5:
                apply_target(verts, f"macrodetails/proportions/{g}-young-{m}-{w}-idealproportions",
                             wm * ww * (proportions - 0.5) * 2.0)
    return base, verts


# --- Rig and weights -----------------------------------------------------------------------------------

def joints(base, verts):
    """Joint positions: mean of each joint cube's vertices."""
    out = {}
    for name in set(base.face_groups):
        if name.startswith("joint-"):
            members = base.group_vertices(name)
            out[name] = sum((verts[i] for i in members), Vector()) / len(members)
    return out


def rig_bones(base, verts):
    """MakeHuman game_engine rig: name -> (head, tail, parent) in MakeHuman space."""
    rig = json.loads((MH_DATA / "rigs" / "standard" / "rig.game_engine.json").read_text())
    cubes = joints(base, verts)

    def point(spec):
        if spec["strategy"] == "CUBE":
            return cubes[spec["cube_name"]].copy()
        return sum((verts[i] for i in spec["vertex_indices"]), Vector()) / len(spec["vertex_indices"])

    return {name: (point(b["head"]), point(b["tail"]), b.get("parent") or None) for name, b in rig.items()}


def base_weights():
    """Vertex index -> {MakeHuman bone: weight} for the game_engine rig."""
    data = json.loads((MH_DATA / "rigs" / "standard" / "weights.game_engine.json").read_text())
    out = {}
    for bone, entries in data["weights"].items():
        for index, w in entries:
            out.setdefault(index, {})[bone] = out.get(index, {}).get(bone, 0.0) + w
    return out


# --- MHCLO proxies (clothes, hair, eyes, eyebrows) ----------------------------------------------------

class Mhclo:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.refs = []  # (indices, weights, offset)
        self.scale = {}
        self.obj_file = None
        self.material = None
        self.delete = set()
        section = None
        with open(self.path, encoding="utf-8", errors="replace") as f:
            for raw in f:
                words = raw.split()
                if not words:
                    section = None
                    continue
                key = words[0]
                if key.startswith("#"):
                    continue
                if key == "material":
                    self.material = self.path.parent / words[1]
                    continue
                if key.startswith("vertexboneweights"):
                    continue
                if section == "verts":
                    if key.lstrip("-").isdigit():
                        if len(words) == 1:
                            self.refs.append(((int(words[0]),) * 3, (1.0, 0.0, 0.0), Vector()))
                        else:
                            idx = tuple(int(w) for w in words[0:3])
                            wts = tuple(float(w) for w in words[3:6])
                            off = Vector(tuple(float(w) for w in words[6:9]))
                            self.refs.append((idx, wts, off))
                        continue
                    section = None
                    continue
                if section == "delete":
                    if key.isdigit():
                        i = 0
                        while i < len(words):
                            if i + 2 < len(words) and words[i + 1] == "-":
                                self.delete.update(range(int(words[i]), int(words[i + 2]) + 1))
                                i += 3
                            else:
                                if words[i].isdigit():
                                    self.delete.add(int(words[i]))
                                i += 1
                        continue
                    section = None
                    continue
                if key == "obj_file":
                    self.obj_file = self.path.parent / words[1]
                elif key in ("x_scale", "y_scale", "z_scale"):
                    self.scale[key[0]] = (int(words[1]), int(words[2]), float(words[3]))
                elif key == "verts" and len(words) > 1:
                    section = "verts"
                elif key == "delete_verts":
                    section = "delete"

    def fit(self, verts):
        """Proxy vertex positions on the (shaped) base mesh."""
        scale = []
        for axis, comp in (("x", 0), ("y", 1), ("z", 2)):
            if axis in self.scale:
                a, b, ref = self.scale[axis]
                scale.append(abs(verts[a][comp] - verts[b][comp]) / ref)
            else:
                scale.append(1.0)
        out = []
        for idx, wts, off in self.refs:
            p = verts[idx[0]] * wts[0] + verts[idx[1]] * wts[1] + verts[idx[2]] * wts[2]
            out.append(p + Vector((off.x * scale[0], off.y * scale[1], off.z * scale[2])))
        return out

    def weights(self, base_w):
        out = []
        for idx, wts, _ in self.refs:
            acc = {}
            for i, w in zip(idx, wts):
                if w == 0.0:
                    continue
                for bone, bw in base_w.get(i, {}).items():
                    acc[bone] = acc.get(bone, 0.0) + bw * w
            out.append(acc)
        return out


def read_mhmat(path):
    """Subset of MHMAT: texture file names and the transparency flag."""
    out = {"diffuse": None, "normal": None, "transparent": False, "diffuse_color": (1.0, 1.0, 1.0)}
    if not path or not pathlib.Path(path).exists():
        return out
    folder = pathlib.Path(path).parent
    for line in open(path, encoding="utf-8", errors="replace"):
        words = line.split()
        if len(words) < 2:
            continue
        if words[0] == "diffuseTexture":
            out["diffuse"] = folder / words[1]
        elif words[0] == "normalmapTexture":
            out["normal"] = folder / words[1]
        elif words[0] == "transparent":
            out["transparent"] = words[1].lower() == "true"
        elif words[0] == "diffuseColor" and len(words) >= 4:
            out["diffuse_color"] = tuple(float(w) for w in words[1:4])
    return out


# --- Posing (linear blend skinning in MakeHuman space) ----------------------------------------------

def rotation_between(a, b):
    a = a.normalized()
    b = b.normalized()
    return a.rotation_difference(b)


class Pose:
    """Forward kinematics on the MakeHuman rig. Rotations are world-space deltas at each bone head."""

    def __init__(self, bones):
        self.bones = bones
        self.local = {name: Quaternion() for name in bones}
        self.order = []
        seen = set()
        while len(self.order) < len(bones):
            for name, (_, _, parent) in bones.items():
                if name not in seen and (parent is None or parent in seen or parent not in bones):
                    self.order.append(name)
                    seen.add(name)

    def matrices(self):
        out = {}
        for name in self.order:
            head, _, parent = self.bones[name]
            parent_m = out.get(parent, Matrix.Identity(4))
            rot = Matrix.Translation(head) @ self.local[name].to_matrix().to_4x4() @ Matrix.Translation(-head)
            out[name] = parent_m @ rot
        return out

    def aim(self, name, target_dir):
        """Rotate a bone so its direction (after its parents' pose) points along target_dir."""
        mats = self.matrices()
        head, tail, parent = self.bones[name]
        parent_m = mats.get(parent, Matrix.Identity(4))
        wanted = parent_m.to_3x3().inverted() @ target_dir
        self.local[name] = rotation_between(tail - head, wanted)

    def rotate(self, name, axis_world_rest, angle):
        """Additional rotation about an axis given in the bone's rest frame."""
        self.local[name] = Quaternion(axis_world_rest.normalized(), angle) @ self.local[name]

    def posed_bones(self):
        mats = self.matrices()
        return {name: (mats[name] @ head, mats[name] @ tail, parent) for name, (head, tail, parent) in self.bones.items()}

    def skin(self, points, weights):
        mats = self.matrices()
        out = []
        for p, w in zip(points, weights):
            total = sum(w.values())
            if total <= 0.0:
                out.append(p.copy())
                continue
            acc = Vector()
            for bone, bw in w.items():
                m = mats.get(bone)
                if m is not None:
                    acc += (m @ p) * (bw / total)
            out.append(acc)
        return out


# Our 17-bone skeleton from MakeHuman bones.
BONE_MAP = {
    "Root": "hips", "pelvis": "hips", "spine_01": "spine", "spine_02": "spine", "spine_03": "chest",
    "clavicle_l": "chest", "clavicle_r": "chest", "neck_01": "neck", "head": "head",
    "upperarm_l": "upperarm.L", "lowerarm_l": "forearm.L", "hand_l": "hand.L",
    "upperarm_r": "upperarm.R", "lowerarm_r": "forearm.R", "hand_r": "hand.R",
    "thigh_l": "thigh.L", "calf_l": "shin.L", "foot_l": "foot.L", "ball_l": "foot.L",
    "thigh_r": "thigh.R", "calf_r": "shin.R", "foot_r": "foot.R", "ball_r": "foot.R",
}


def map_weights(w):
    out = {}
    for bone, value in w.items():
        target = BONE_MAP.get(bone)
        if target is None:
            side = "L" if bone.endswith("_l") else "R"
            target = f"hand.{side}"  # Fingers and thumbs.
        out[target] = out.get(target, 0.0) + value
    total = sum(out.values())
    if total <= 0.0:
        return {"hips": 1.0}
    top = sorted(out.items(), key=lambda kv: -kv[1])[:4]
    total = sum(v for _, v in top)
    return {k: v / total for k, v in top if v / total > 0.01}


def our_skeleton(posed):
    """Head/tail of our bones (MakeHuman space) from the posed MakeHuman rig."""
    def head(n):
        return posed[n][0]

    def tail(n):
        return posed[n][1]

    out = {
        "hips": (head("pelvis"), head("spine_01")),
        "spine": (head("spine_01"), head("spine_03")),
        "chest": (head("spine_03"), head("neck_01")),
        "neck": (head("neck_01"), head("head")),
        "head": (head("head"), tail("head")),
    }
    for mh, ours in (("l", "L"), ("r", "R")):
        out[f"upperarm.{ours}"] = (head(f"upperarm_{mh}"), head(f"lowerarm_{mh}"))
        out[f"forearm.{ours}"] = (head(f"lowerarm_{mh}"), head(f"hand_{mh}"))
        out[f"hand.{ours}"] = (head(f"hand_{mh}"), tail(f"middle_01_{mh}"))
        out[f"thigh.{ours}"] = (head(f"thigh_{mh}"), head(f"calf_{mh}"))
        out[f"shin.{ours}"] = (head(f"calf_{mh}"), head(f"foot_{mh}"))
        out[f"foot.{ours}"] = (head(f"foot_{mh}"), tail(f"ball_{mh}"))
    return out
