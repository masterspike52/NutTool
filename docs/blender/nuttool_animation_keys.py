# NutTool animation keys exporter for Blender: File > Export > NutTool Animation Keys (.json).
# Writes an armature's motion as the JSON timeline that NutTool's Replace motion / anim-import reads.

bl_info = {
    "name": "NutTool Animation Keys",
    "author": "NutTool",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "File > Export > NutTool Animation Keys (.json)",
    "description": "Export an armature's animation as a NutTool animation keys timeline (Banjo-Kazooie: Nuts & Bolts)",
    "category": "Import-Export",
}

import json

import bpy
from bpy.props import BoolProperty, EnumProperty, IntProperty, StringProperty
from bpy_extras.io_utils import ExportHelper
from mathutils import Matrix

# Blender's world (Z up, the character facing -Y, as Blender imports NutTool's FBX) to the game's (Y up, facing +Z).
TO_GAME_ZUP = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0)))
TO_GAME_YUP = Matrix.Identity(3)


def rotation_of(m3):
    """The rotation part of a 3x3 matrix (scale removed)."""
    return m3.to_quaternion().normalized().to_matrix()


def find_armature(context):
    obj = context.active_object
    if obj and obj.type == 'ARMATURE':
        return obj
    if obj and obj.type == 'MESH':
        for mod in obj.modifiers:
            if mod.type == 'ARMATURE' and mod.object:
                return mod.object
        if obj.parent and obj.parent.type == 'ARMATURE':
            return obj.parent
    return None


def keyed_channels(arm):
    """Bone name -> the channels the armature's action keys for it ('rot', 'loc')."""
    channels = {}
    ad = arm.animation_data
    action = ad.action if ad else None
    if not action:
        return channels
    curves = []
    if hasattr(action, "fcurves"):
        try:
            curves = list(action.fcurves)
        except Exception:
            curves = []
    if not curves and hasattr(action, "layers"):
        # Layered actions (Blender 4.4 and later): the channelbags of every strip.
        for layer in action.layers:
            for strip in layer.strips:
                for bag in getattr(strip, "channelbags", []):
                    curves.extend(bag.fcurves)
    for fc in curves:
        path = fc.data_path
        if not path.startswith('pose.bones["'):
            continue
        name, _, prop = path[len('pose.bones["'):].partition('"]')
        if prop.startswith(".rotation"):
            channels.setdefault(name, set()).add('rot')
        elif prop == ".location":
            channels.setdefault(name, set()).add('loc')
    return channels


class EXPORT_ANIM_OT_nuttool_keys(bpy.types.Operator, ExportHelper):
    """Export the active armature's animation as a NutTool animation keys timeline"""
    bl_idname = "export_anim.nuttool_keys"
    bl_label = "Export NutTool Animation Keys"
    bl_options = {'PRESET'}

    filename_ext = ".json"
    filter_glob: StringProperty(default="*.json", options={'HIDDEN'})

    frame_range: EnumProperty(
        name="Frames",
        items=(
            ('SCENE', "Scene range", "The scene's start to end frame"),
            ('ACTION', "Action range", "The frame range of the armature's action"),
        ),
        default='SCENE',
    )
    step: IntProperty(name="Every n-th frame", default=1, min=1, max=30,
                      description="Write a key every this many frames (1 keeps every frame)")
    bones: EnumProperty(
        name="Bones",
        items=(
            ('KEYED', "Keyed bones", "Bones the action animates; the rest keep the game animation's own motion"),
            ('DEFORM', "All deforming bones", "Every bone with Deform on"),
            ('SELECTED', "Selected bones", "The bones selected in pose mode"),
            ('ALL', "All bones", "Every bone of the armature"),
        ),
        default='KEYED',
    )
    constrained: BoolProperty(name="Include constrained bones", default=True,
                              description="With Keyed bones, also bones moved by constraints or IK")
    moves: EnumProperty(
        name="Bone moves",
        items=(
            ('AUTO', "Bones that move", "Write \"move\" for bones whose position changes (the hips, mostly)"),
            ('NONE', "None", "Rotations only"),
        ),
        default='AUTO',
    )
    base: EnumProperty(
        name="Other bones",
        items=(
            ('keep', "Keep the game's motion", "Bones not exported keep playing the original animation (face, fingers...)"),
            ('rest', "Rest pose", "Bones not exported hold the bind pose"),
        ),
        default='keep',
    )
    axes: EnumProperty(
        name="Axes",
        items=(
            ('ZUP', "Blender (Z up)", "The armature as Blender imports NutTool's FBX: Z up, the character facing -Y"),
            ('YUP', "Game (Y up)", "The scene already uses the game's axes: Y up, the character facing +Z"),
        ),
        default='ZUP',
    )
    skip_end: BoolProperty(name="Skip _end bones", default=True,
                           description="Leave out the <bone>_end helpers NutTool's FBX export can add")

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.prop(self, "frame_range")
        layout.prop(self, "step")
        layout.prop(self, "bones")
        if self.bones == 'KEYED':
            layout.prop(self, "constrained")
        layout.prop(self, "moves")
        layout.prop(self, "base")
        layout.prop(self, "axes")
        layout.prop(self, "skip_end")

    def execute(self, context):
        arm = find_armature(context)
        if arm is None:
            self.report({'ERROR'}, "Select an armature (or a mesh deformed by one)")
            return {'CANCELLED'}
        scene = context.scene
        fps = scene.render.fps / scene.render.fps_base
        if self.frame_range == 'ACTION' and arm.animation_data and arm.animation_data.action:
            start, end = (int(round(f)) for f in arm.animation_data.action.frame_range)
        else:
            start, end = scene.frame_start, scene.frame_end
        if end < start:
            self.report({'ERROR'}, "The frame range is empty")
            return {'CANCELLED'}
        frames = list(range(start, end + 1, self.step))
        if frames[-1] != end:
            frames.append(end)

        pbones = arm.pose.bones
        # Channels written per bone: with Keyed bones only those keyed (or moved by constraints), else both.
        both = {'rot', 'loc'}
        if self.bones == 'KEYED':
            channels = keyed_channels(arm)
            if self.constrained:
                for pb in pbones:
                    if len(pb.constraints):
                        channels[pb.name] = both
            chosen = [pb for pb in pbones if pb.name in channels]
        else:
            channels = {pb.name: both for pb in pbones}
        if self.bones == 'DEFORM':
            chosen = [pb for pb in pbones if pb.bone.use_deform]
        elif self.bones == 'SELECTED':
            chosen = [pb for pb in pbones if pb.bone.select]
        elif self.bones == 'ALL':
            chosen = list(pbones)
        if self.skip_end:
            chosen = [pb for pb in chosen if not pb.name.lower().endswith("_end")]
        if not chosen:
            self.report({'ERROR'}, "No bones to export (is the armature's action keyed? try All deforming bones)")
            return {'CANCELLED'}

        to_game = TO_GAME_ZUP if self.axes == 'ZUP' else TO_GAME_YUP
        from_game = to_game.inverted()

        # Rest pose, constant: each bone's rest rotation in world axes.
        world3 = arm.matrix_world.to_3x3()
        rest_world = {}
        for pb in chosen:
            rest_world[pb.name] = rotation_of(world3 @ pb.bone.matrix_local.to_3x3())

        saved_frame = scene.frame_current, scene.frame_subframe
        rots = {pb.name: [] for pb in chosen}
        moves = {pb.name: [] for pb in chosen}
        try:
            for f in frames:
                scene.frame_set(f)
                for pb in chosen:
                    rest = pb.bone.matrix_local
                    pose = pb.matrix  # evaluated: keys, constraints and IK
                    if pb.parent:
                        rel_rest = pb.parent.bone.matrix_local.inverted() @ rest
                        rel_pose = pb.parent.matrix.inverted() @ pose
                    else:
                        rel_rest, rel_pose = rest, pose
                    basis = rel_rest.inverted() @ rel_pose  # the bone's own change from rest, in its rest frame
                    rw = rest_world[pb.name]
                    # The turn from rest about the model's axes, and the bone's move in metres along them.
                    turn = (to_game @ rw @ rotation_of(basis.to_3x3()) @ rw.inverted() @ from_game).to_quaternion().normalized()
                    move = to_game @ (world3 @ (rest.to_3x3() @ basis.translation))
                    prev = rots[pb.name][-1] if rots[pb.name] else None
                    if prev is not None and prev.dot(turn) < 0:
                        turn.negate()
                    rots[pb.name].append(turn)
                    moves[pb.name].append(move)
        finally:
            scene.frame_set(saved_frame[0], subframe=saved_frame[1])

        moving = set()
        if self.moves == 'AUTO':
            for name, ms in moves.items():
                if any(m.length > 1e-5 for m in ms):
                    moving.add(name)

        def num(x):
            s = "%.6f" % x
            s = s.rstrip("0").rstrip(".")
            return "0" if s in ("-0", "") else s

        lines = []
        for i, f in enumerate(frames):
            entry = {}
            for pb in chosen:
                value = {}
                if 'rot' in channels[pb.name]:
                    q = rots[pb.name][i]
                    value["rot"] = [q.x, q.y, q.z, q.w]
                if 'loc' in channels[pb.name] and pb.name in moving:
                    m = moves[pb.name][i]
                    value["move"] = [m.x, m.y, m.z]
                if value:
                    entry[pb.name] = value
            bones_text = ", ".join(
                json.dumps(name) + ": {" + ", ".join(json.dumps(k) + ": [" + ", ".join(num(c) for c in v) + "]" for k, v in value.items()) + "}"
                for name, value in entry.items())
            lines.append('    {"time": %s, "bones": {%s}}' % (num((f - start) / fps), bones_text))

        header = {
            "generator": "NutTool Animation Keys for Blender %d.%d.%d" % bl_info["version"],
            "armature": arm.name,
            "action": arm.animation_data.action.name if arm.animation_data and arm.animation_data.action else "",
            "frames": [start, end],
            "fps": fps,
            "space": "model",
            "interpolation": "linear",
            "unknownBones": "skip",
            "base": self.base,
        }
        text = "{\n" + "".join("  %s: %s,\n" % (json.dumps(k), json.dumps(v)) for k, v in header.items())
        text += '  "timeline": [\n' + ",\n".join(lines) + "\n  ]\n}\n"
        with open(self.filepath, "w", encoding="utf-8") as fh:
            fh.write(text)

        seconds = (end - start) / fps
        self.report({'INFO'}, "Wrote %d bone(s) over %d key(s), %.2f s (%s)" % (len(chosen), len(frames), seconds, self.filepath))
        return {'FINISHED'}


def menu_func_export(self, context):
    self.layout.operator(EXPORT_ANIM_OT_nuttool_keys.bl_idname, text="NutTool Animation Keys (.json)")


def register():
    bpy.utils.register_class(EXPORT_ANIM_OT_nuttool_keys)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(EXPORT_ANIM_OT_nuttool_keys)


if __name__ == "__main__":
    register()
