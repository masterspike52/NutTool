# Tidy bone lengths of an armature imported from a NutTool FBX: run it in Blender's Scripting tab with the armature selected.
# Only lengths change (heads, directions and rolls stay), so the skin, rest pose and animations are untouched.
import bpy

LEAF_SIZE = 0.015  # bones with no child to reach: this fraction of the armature's height
MIN_SIZE = 0.005   # no bone shorter than this fraction of the height

arm = bpy.context.view_layer.objects.active
if arm is None or arm.type != 'ARMATURE':
    raise RuntimeError("Select the armature (click a bone or the armature in the Outliner), then run the script.")

mode = arm.mode
bpy.ops.object.mode_set(mode='EDIT')
bones = arm.data.edit_bones

heads = [b.head for b in bones]
size = max(max(h[k] for h in heads) - min(h[k] for h in heads) for k in range(3)) or 1.0
leaf, shortest = LEAF_SIZE * size, MIN_SIZE * size

# Disconnect first: a connected child's head follows its parent's tail, which would move it.
for b in bones:
    b.use_connect = False

for b in bones:
    direction = (b.tail - b.head).normalized()
    # The main child: the one with the most bones under it, or the middle of tied ones.
    target = None
    if b.children:
        most = max(len(c.children_recursive) for c in b.children)
        tied = [c.head for c in b.children if len(c.children_recursive) == most and (c.head - b.head).length > shortest]
        if tied:
            target = sum(tied, b.head * 0) / len(tied)
    reach = (target - b.head).dot(direction) if target is not None else 0
    b.length = reach if reach > shortest else leaf

# Reconnect children that now start where their parent ends.
for b in bones:
    if b.parent and (b.head - b.parent.tail).length < shortest * 0.01:
        b.use_connect = True

count = len(bones)
bpy.ops.object.mode_set(mode=mode)
print("tidy_bones: %d bones" % count)
