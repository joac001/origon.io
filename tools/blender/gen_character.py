"""Soldier skeleton and animations for Origon (used by gen_human.py), generated headless in Blender.

Run (builds both soldiers through gen_human.py):
    blender --background --factory-startup --python tools/blender/gen_character.py

BONES is the 17-bone skeleton in the rifle-holding rest pose. add_animations() writes NLA tracks
exported as animations: idle, run, crouch, reload, knife,
death (falls backward) and death_forward. Reload and knife only key the upper body; the engine
blends them over the locomotion pose.
The character faces Blender +Y (engine -Z), feet at the origin.
"""
import math
import pathlib

import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "assets" / "models" / "characters"
FPS = 30

# name: (head, tail, parent)
BONES = {
    "hips": ((0, 0, 0.92), (0, 0, 1.05), None),
    "spine": ((0, 0, 1.05), (0, 0, 1.28), "hips"),
    "chest": ((0, 0, 1.28), (0, 0, 1.48), "spine"),
    "neck": ((0, 0, 1.48), (0, 0, 1.58), "chest"),
    "head": ((0, 0, 1.58), (0, 0, 1.82), "neck"),
    "upperarm.R": ((0.21, 0, 1.44), (0.22, 0.16, 1.22), "chest"),
    "forearm.R": ((0.22, 0.16, 1.22), (0.1, 0.36, 1.27), "upperarm.R"),
    "hand.R": ((0.1, 0.36, 1.27), (0.07, 0.44, 1.27), "forearm.R"),
    "upperarm.L": ((-0.21, 0, 1.44), (-0.2, 0.2, 1.25), "chest"),
    "forearm.L": ((-0.2, 0.2, 1.25), (-0.02, 0.52, 1.3), "upperarm.L"),
    "hand.L": ((-0.02, 0.52, 1.3), (0.0, 0.6, 1.3), "forearm.L"),
    "thigh.R": ((0.11, 0, 0.92), (0.11, 0.02, 0.5), "hips"),
    "shin.R": ((0.11, 0.02, 0.5), (0.11, 0, 0.09), "thigh.R"),
    "foot.R": ((0.11, 0, 0.09), (0.11, 0.17, 0.03), "shin.R"),
    "thigh.L": ((-0.11, 0, 0.92), (-0.11, 0.02, 0.5), "hips"),
    "shin.L": ((-0.11, 0.02, 0.5), (-0.11, 0, 0.09), "thigh.L"),
    "foot.L": ((-0.11, 0, 0.09), (-0.11, 0.17, 0.03), "shin.L"),
}

def make_armature(bones=None):
    bones = bones or BONES
    data = bpy.data.armatures.new("soldier_rig")
    rig = bpy.data.objects.new("soldier_rig", data)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    for name, (head, tail, parent) in bones.items():
        bone = data.edit_bones.new(name)
        bone.head = Vector(head)
        bone.tail = Vector(tail)
        bone.roll = 0.0
        if parent:
            bone.parent = data.edit_bones[parent]
            bone.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    return rig


UPPER_BODY = ("spine", "chest", "neck", "head", "upperarm.R", "forearm.R", "hand.R", "upperarm.L", "forearm.L",
              "hand.L")


def key(rig, frame, rotations, hips_drop=0.0, bones=None, hips_back=0.0):
    """rotations: bone -> (x, y, z) degrees (pose space, XYZ euler)."""
    for name in bones or BONES:
        pb = rig.pose.bones[name]
        pb.rotation_mode = "XYZ"
        r = rotations.get(name, (0, 0, 0))
        pb.rotation_euler = tuple(math.radians(a) for a in r)
        pb.keyframe_insert("rotation_euler", frame=frame)
    if bones:
        return
    hips = rig.pose.bones["hips"]
    # Hips bone points up: local Y is world up, local Z is world back.
    hips.location = (0.0, hips_drop, hips_back)
    hips.keyframe_insert("location", frame=frame)


def make_action(rig, name, frames, bones=None):
    action = bpy.data.actions.new(name)
    rig.animation_data_create()
    rig.animation_data.action = action
    for frame, rotations, drop, *extra in frames:
        key(rig, frame, rotations, drop, bones, extra[0] if extra else 0.0)
    track = rig.animation_data.nla_tracks.new()
    track.name = name
    track.strips.new(name, int(frames[0][0]), action)
    rig.animation_data.action = None


def add_animations(rig):
    # Idle: breathing.
    make_action(rig, "idle", [
        (1, {"chest": (2, 0, 0)}, 0.0),
        (31, {"chest": (-1, 0, 0)}, -0.005),
        (61, {"chest": (2, 0, 0)}, 0.0),
    ])
    # Locomotion cycles from gait curves. Joint conventions on this rig: thigh +X flexes the hip
    # (knee forward), shin -X bends the knee, foot +X lifts the toes, spine -X leans forward,
    # hips Y twists the pelvis. Phase 0 = right heel strike, 0.5 = left heel strike.
    def bump(p, center, width):
        d = (p - center + 0.5) % 1.0 - 0.5
        return math.exp(-(d / width) ** 2)

    def curve(p, keys):
        """Periodic piecewise-linear curve through (phase, value) keys."""
        keys = sorted(keys)
        for (p0, v0), (p1, v1) in zip(keys, keys[1:] + [(keys[0][0] + 1.0, keys[0][1])]):
            q = p if p >= p0 else p + 1.0
            if p0 <= q <= p1:
                t = (q - p0) / (p1 - p0)
                t = t * t * (3 - 2 * t)  # Smooth between keys.
                return v0 + (v1 - v0) * t
        return keys[0][1]

    def gait(p, hip_amp, hip_bias, knee_stance, knee_swing, swing_center, swing_width, ankle_keys):
        pose = {}
        for side, offset in (("R", 0.0), ("L", 0.5)):
            q = (p + offset) % 1.0
            hip = hip_amp * math.cos(2 * math.pi * q) + hip_bias
            knee = 6 + knee_stance * bump(q, 0.12, 0.08) + knee_swing * bump(q, swing_center, swing_width)
            ankle = curve(q, ankle_keys)
            pose[f"thigh.{side}"] = (hip, 0, 0)
            pose[f"shin.{side}"] = (-knee, 0, 0)
            pose[f"foot.{side}"] = (ankle, 0, 0)
        return pose

    def cycle(name, frames_per_cycle, pose_at, keys=16):
        out = []
        for i in range(keys + 1):
            p = (i % keys) / keys
            rotations, drop, back = pose_at(p)
            out.append((1 + frames_per_cycle * i / keys, rotations, drop, back))
        make_action(rig, name, out)

    walk_ankle = [(0.0, 4), (0.08, -6), (0.4, 8), (0.6, -16), (0.75, 6)]
    run_ankle = [(0.0, 6), (0.12, 12), (0.35, -24), (0.55, 4), (0.85, 10)]

    def walk(p):
        pose = gait(p, 21, 3, 12, 56, 0.72, 0.11, walk_ankle)
        c = math.cos(2 * math.pi * p)
        pose["hips"] = (0, 5 * c, 0)
        pose["spine"] = (-3, -3 * c, 0)
        pose["chest"] = (1.0 * math.cos(4 * math.pi * p), -2 * c, 0)
        return pose, -0.025 * (0.5 + 0.5 * math.cos(4 * math.pi * p)), 0.0

    def run(p):
        pose = gait(p, 36, 8, 30, 95, 0.68, 0.14, run_ankle)
        c = math.cos(2 * math.pi * p)
        pose["hips"] = (0, 7 * c, 0)
        pose["spine"] = (-12, -5 * c, 0)
        pose["chest"] = (-2 + 2.0 * math.cos(4 * math.pi * p), -4 * c, 0)
        pose["neck"] = (6, 0, 0)  # Keep the eyes level while leaning.
        return pose, -0.035 - 0.035 * (0.5 + 0.5 * math.cos(4 * math.pi * p)), 0.02

    crouch = {"thigh.R": (75, 0, 0), "thigh.L": (65, 0, 6), "shin.R": (-115, 0, 0), "shin.L": (-110, 0, 0),
              "foot.R": (35, 0, 0), "foot.L": (35, 0, 0), "spine": (12, 0, 0)}

    def crouch_walk(p):
        pose = {}
        for side, offset in (("R", 0.0), ("L", 0.5)):
            q = (p + offset) % 1.0
            swing = bump(q, 0.7, 0.13)
            hip = 68 + 16 * math.cos(2 * math.pi * q) + 10 * swing
            knee = 108 + 18 * swing - 6 * bump(q, 0.15, 0.1)
            pose[f"thigh.{side}"] = (hip, 0, 0)
            pose[f"shin.{side}"] = (-knee, 0, 0)
            pose[f"foot.{side}"] = (36 + curve(q, [(0.0, 2), (0.45, 8), (0.62, -10), (0.8, 4)]), 0, 0)
        c = math.cos(2 * math.pi * p)
        pose["hips"] = (0, 4 * c, 0)
        pose["spine"] = (8, -3 * c, 0)
        return pose, -0.4 - 0.015 * (0.5 + 0.5 * math.cos(4 * math.pi * p)), 0.0

    cycle("walk", 30, walk)        # 1.0 s per cycle (two steps).
    cycle("run", 21, run)          # 0.7 s.
    cycle("crouch_walk", 36, crouch_walk)
    # Airborne: knees tucked, one leg ahead.
    air = {"thigh.R": (48, 0, 0), "thigh.L": (22, 0, 0), "shin.R": (-75, 0, 0), "shin.L": (-58, 0, 0),
           "foot.R": (12, 0, 0), "foot.L": (-6, 0, 0), "spine": (-6, 0, 0)}
    make_action(rig, "jump", [(1, air, -0.04), (11, air, -0.05)])
    # Crouch: hips down, knees bent.
    make_action(rig, "crouch", [(1, crouch, -0.4), (31, crouch, -0.41)])

    # Reload (1 s, the engine scales it to the weapon's reload time): left hand drops to the belt
    # pouch, brings a magazine back to the gun, then the right hand works the action.
    hold = {}
    pouch = {"upperarm.L": (-45, 0, 8), "forearm.L": (-35, 0, 0), "chest": (6, -8, 0), "head": (12, 0, 0)}
    insert = {"upperarm.L": (-12, 0, 0), "forearm.L": (-10, 0, 0), "chest": (4, -5, 0), "head": (14, 0, 0)}
    charge = {"forearm.R": (8, 0, -25), "upperarm.R": (0, 0, -12), "chest": (2, 6, 0), "head": (8, 0, 0)}
    make_action(rig, "reload", [
        (1, hold, 0.0), (6, insert, 0.0), (11, pouch, 0.0), (17, pouch, 0.0), (22, insert, 0.0), (25, charge, 0.0),
        (31, hold, 0.0),
    ], bones=UPPER_BODY)
    # Knife / butt stroke (0.3 s): wind up high on the right, slash across to the left.
    windup = {"upperarm.R": (55, 0, -35), "forearm.R": (35, 0, 0), "chest": (0, -14, 0), "spine": (-4, -6, 0)}
    slash = {"upperarm.R": (-15, 0, 45), "forearm.R": (-10, 0, 25), "chest": (-8, 18, 0), "spine": (-8, 8, 0),
             "upperarm.L": (-25, 0, 20)}
    make_action(rig, "knife", [(1, hold, 0.0), (3, windup, 0.0), (6, slash, 0.0), (10, hold, 0.0)],
                bones=UPPER_BODY)

    # Deaths (1 s, the last frame holds): knees buckle, the body hits the ground and settles.
    buckle = {"thigh.R": (40, 0, 0), "thigh.L": (25, 0, 4), "shin.R": (-60, 0, 0), "shin.L": (-45, 0, 0),
              "head": (-15, 0, 0), "upperarm.R": (30, 0, -20), "upperarm.L": (30, 0, 20)}
    fall_back = dict(buckle, hips=(25, 0, 0), spine=(10, 0, 0))
    ground_back = {"hips": (88, 0, 4), "spine": (-6, 0, 0), "chest": (-4, 0, 0), "head": (-20, 0, 25),
                   "thigh.R": (-8, 0, -6), "thigh.L": (12, 0, 8), "shin.R": (-12, 0, 0), "shin.L": (-35, 0, 0),
                   "foot.R": (-50, 0, 0), "foot.L": (-40, 0, 0),
                   "upperarm.R": (70, 0, -70), "forearm.R": (-20, 0, -30),
                   "upperarm.L": (40, 0, 60), "forearm.L": (-50, 0, 20)}
    settle_back = dict(ground_back, hips=(91, 0, 4), head=(-12, 0, 30))
    make_action(rig, "death", [
        (1, hold, 0.0), (7, buckle, -0.25), (13, fall_back, -0.5, 0.1),
        (19, ground_back, -0.74, 0.25), (23, dict(ground_back, hips=(84, 0, 4)), -0.71, 0.25),
        (31, settle_back, -0.74, 0.25),
    ])
    fall_forward = dict(buckle, hips=(-30, 0, 0), spine=(-15, 0, 0))
    ground_forward = {"hips": (-88, 0, -5), "spine": (4, 0, 0), "head": (20, 0, -30),
                      "thigh.R": (6, 0, 4), "thigh.L": (-10, 0, -6), "shin.R": (-10, 0, 0), "shin.L": (-40, 0, 0),
                      "foot.R": (30, 0, 0), "foot.L": (40, 0, 0),
                      "upperarm.R": (-60, 0, -30), "forearm.R": (-30, 0, 0),
                      "upperarm.L": (70, 0, 40), "forearm.L": (-40, 0, 30)}
    settle_forward = dict(ground_forward, hips=(-90, 0, -5))
    make_action(rig, "death_forward", [
        (1, hold, 0.0), (7, buckle, -0.25), (13, fall_forward, -0.5, -0.1),
        (19, ground_forward, -0.76, -0.25), (23, dict(ground_forward, hips=(-84, 0, -5)), -0.73, -0.25),
        (31, settle_forward, -0.76, -0.25),
    ])



def export(rig, objects, filename):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT / filename), export_format="GLB", use_selection=True,
                              export_yup=True, export_skins=True, export_animations=True,
                              export_animation_mode="NLA_TRACKS", export_materials="EXPORT",
                              export_image_format="NONE", export_apply=False)
    for obj in objects:
        print(f"[origon]   {obj.name}: {len(obj.data.vertices)} verts, {len(obj.data.polygons)} faces")
    print(f"[origon] {filename}: {len(objects)} pieces, {len(rig.data.bones)} bones")




def main():
    # The soldiers are built from MakeHuman data on this skeleton: see gen_human.py.
    import gen_human
    gen_human.main()


if __name__ == "__main__":
    main()
