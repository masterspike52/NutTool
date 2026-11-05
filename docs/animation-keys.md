# Animation keys (`anim-import`)

`nuttool anim-import` gives an existing animation new motion from a JSON timeline that you
write by hand. You set poses on chosen bones at chosen times, and NutTool eases between them.

```
nuttool anim-import <bundle> <animation> <keys.json> [-o out] [--xctd]
```

In the GUI, use **Replace motion...** in a model tab's **Animation** section, with the
animation picked in the list. The viewport plays the result, and **Ctrl+S** writes it into
every bundle holding the animation.

Every copy of the animation in the file is rewritten. The animation keeps its name, length,
frame count, facial channels and everything else; only its bone motion changes. So the
game plays it wherever it played the original (replace an idle and the character does your
motion whenever it stands still).

`docs/examples/banjo-macarena.json` is a complete example: Banjo's idle
(`banjox_actor_banjo_stand`, 13.47 s) replaced by the Macarena.

## The file

```json
{
  "base": "keep",
  "poses": {
    "rightArmUp": { "RT_S": { "rot": [0, 0, -75] }, "RT_E": { "rot": [0, 0, 0] } },
    "hop":        { "BASE": { "move": [0, 0.15, 0] } }
  },
  "timeline": [
    { "time": 0.0, "pose": "rightArmUp", "hold": 0.5 },
    { "time": 1.0, "pose": ["rightArmUp", "hop"] },
    { "time": 1.4, "bones": { "BASE": { "move": [0, 0, 0] } } }
  ]
}
```

- **base**: what bones without keys do. `"keep"` (the default) leaves them playing the
  animation's own motion: the face, the fingers, a companion. `"rest"` holds them in the
  bind pose.
- **space** (optional): `"local"` (the default) reads **rot** as the bone's local rotation
  and **move** in its parent's frame, as described below. With `"model"`, **rot** is the
  bone's turn from its rest pose about the model's axes (+X left, +Y up, +Z forward), and
  **move** is its offset from its rest position along those axes. NutTool converts both
  using the skeleton's rest pose. That way they mean the same on every skeleton, whatever
  its bones' rest rotations; the Blender add-on writes this form. On a skeleton bound with
  no rotations, the two spaces are the same.
- **interpolation** (optional): `"smooth"` (the default) eases between keys; `"linear"`
  blends in straight lines, for files with a key on every frame.
- **unknownBones** (optional): `"fail"` (the default) stops at a bone the skeleton lacks;
  `"skip"` leaves such bones out and lists them in the report.
- **poses**: named sets of bones, so a pose can be reused. Bone names are the skeleton's
  (the model export's `.bones.txt` lists them, and so does the model viewer). Case doesn't
  matter.
- **timeline**: entries with a **time** in seconds, and a **pose** name, a list of pose
  names (merged in order), and/or inline **bones**. **hold** keeps the entry's pose for that
  many seconds before easing on to the next key.

Each bone value can set:

- **rot**: the bone's local rotation, as `[x, y, z]` Euler degrees (X turns first, then Y,
  then Z) or an `[x, y, z, w]` quaternion. It replaces the animation's rotation for that
  bone. Most of the game's skeletons are bound with no rotation, so on those `[0, 0, 0]` is
  the rest pose (the T-pose) and the axes are the model's: +X is the character's left, +Y
  up, +Z forward.
- **move**: the bone's offset from its rest position, in metres. Moving `BASE` lifts or
  shifts the whole body. Offsets are limited to 1 m.
- `"base"` instead of a value takes the animation's own value at that time.

## From Blender

[`docs/blender/nuttool_animation_keys.py`](blender/nuttool_animation_keys.py) is a Blender
add-on (4.2 or later) that writes this file from an animated armature, so you can animate
in Blender instead of by hand.

1. **Install it**: *Edit > Preferences > Add-ons*, the menu at the top right, *Install from
   Disk...*, pick `nuttool_animation_keys.py` and tick **NutTool Animation Keys**.
2. **Get the skeleton**: in NutTool, open the character's model tab and use **Export FBX...**.
   To start from the motion you will replace, play that animation first and export with
   **Only the one playing**; with **None** you start from the T-pose. Import the FBX in
   Blender with the default settings.
   *Reorient bones* and *Short end bones* are fine; the add-on works either way.
3. **Animate** the armature: keyframes, constraints and IK all count, because the add-on
   reads the evaluated pose. Set the scene to **30 fps** and its frame range to your
   motion. The first frame is time 0, and the motion should fit the animation's length
   (the model tab shows it).
4. **Export**: select the armature (or the mesh), then *File > Export > NutTool Animation
   Keys (.json)*.
5. **Use it** in NutTool: **Replace motion...** in the model tab's **Animation** section, or
   `nuttool anim-import`.

Don't change the armature's rest pose (*Apply Pose as Rest Pose*): the add-on measures the
motion from it, so it must stay the game's bind pose.

Export options:

- **Bones**: **Keyed bones** (the default) writes the bones and channels your action keys,
  plus bones moved by constraints. Every other bone keeps playing the game animation's own
  motion (the face, the fingers). **All deforming bones**, **Selected bones** and **All
  bones** write every rotation of those bones instead.
- **Bone moves**: **Bones that move** writes `move` for bones whose position changes (the
  hips, mostly); **None** writes rotations only.
- **Other bones**: the file's `base`.
- **Every n-th frame**: 1 writes a key on every frame. The game stores 15 or 30 frames a
  second, so more isn't needed.
- **Axes**: **Blender (Z up)** for an armature imported from NutTool's FBX; **Game (Y up)**
  if your scene already uses the game's axes.
- **Skip _end bones**: leaves out the `<bone>_end` helpers of *Short end bones*.

The file it writes sets `"space": "model"`, `"interpolation": "linear"` and
`"unknownBones": "skip"`, with one timeline entry per frame. Rig helper bones the game
skeleton lacks, such as IK controls, are skipped and listed when you import.

## How keys play

Each bone has its own keys: one at every timeline entry that names it (two with
**hold**). Between two keys the bone eases (slow in, slow out); before its first key and
after its last it holds. So:

- A bone named only once stays in that pose for the whole animation.
- When several bones move together, name all of them in the same entries. If one of them
  is left out of an entry, it starts drifting from its previous key toward its next one.
- A looping animation should end in the pose it starts with.
- Rotations ease along the shortest way round, so give large turns an extra key halfway.

## Tips

- Plan arm poses with the bind pose in mind: on a T-posed skeleton the shoulder rotation
  alone points the upper arm, the elbow rotation bends it from there, and rotations about the
  arm's own axis (X for arms along X) turn the palm.
- Key the spine and the shoulder's parents (`BACK`, the clavicles) too, or the original
  animation's lean still moves your arm poses.
- Bending the legs lowers the hips: lower `BASE` by the same amount (`move`) to keep the feet
  on the ground.
