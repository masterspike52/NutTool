# Animation keys (`anim-import`)

`nuttool anim-import` gives an existing animation new motion from a JSON timeline that you
write by hand. You set poses on chosen bones at chosen times, and NutTool eases between them.

```
nuttool anim-import <bundle> <animation> <keys.json> [-o out] [--xctd]
```

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
