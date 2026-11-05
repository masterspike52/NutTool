# NutTool advanced tutorial

This tutorial assumes you have done the [starter tutorial](tutorial-starter.md): you can
open the assets, swap a texture, and package a mod. Here you'll replace characters with
other characters from the game, bring in characters you made yourself, give characters
new animations, and use the deeper editors: split and animated textures, menus and the
title update, vehicle parts, game scripts, fonts, raw assets, the command line and Lua.

- [1. How the game's files fit together](#1-how-the-games-files-fit-together)
- [2. Replace a character with another one from the game](#2-replace-a-character-with-another-one-from-the-game)
- [3. Bring your own character](#3-bring-your-own-character)
- [4. Work in Blender with FBX](#4-work-in-blender-with-fbx)
- [5. Make new animations](#5-make-new-animations)
- [6. Textures in depth](#6-textures-in-depth)
- [7. Menus and the title update](#7-menus-and-the-title-update)
- [8. Vehicle parts](#8-vehicle-parts)
- [9. Game scripts](#9-game-scripts)
- [10. Fonts](#10-fonts)
- [11. The command line](#11-the-command-line)
- [12. Automate with Lua](#12-automate-with-lua)
- [13. Raw assets](#13-raw-assets)
- [14. Mods in depth](#14-mods-in-depth)

## 1. How the game's files fit together

A few facts explain most of what NutTool does, and most of what can go wrong.

- **Assets have names**, such as `aid_model_banjox_actor_banjotuxedo`:
  `aid_<type>_<rest>`. The game finds them by an id computed from the name.
  **Tools > Asset ID calculator...** shows the id and the file for any name.
- **Bundles** (`Bundle\4f\...`) hold the assets of a level or a group of things: models,
  textures, scripts, menus. **Stream bundles** (`Bundle\50\<same name>`) are their
  companions and hold the bulky parts: full-size textures and audio.
- **The same asset is often in several bundles**, because each level ships what it needs.
  The game may load any copy. NutTool's tabs (Textures, Models, Scripts, Vehicle parts)
  change **every copy** at once; editing a single bundle changes only that one.
- **Big textures are split**: the bundle holds the smaller mips (`..._0x<id>mip`) and the
  stream bundle holds the full-size level (`..._0x<id>top`). NutTool merges and writes both
  halves for you, but a mod must then contain **both files**.
- **Models** are made of **draw groups** and **meshes**. One draw group is a detail level or
  a switchable part (Banjo's wrench, the Kazooie part, a hat). Each mesh is one draw with
  one material. Exports name them `group<G>_mesh<N>`, and imports match them by that `N`.
- **Characters are skinned**: every vertex follows one or more bones of a skeleton, and the
  game's animations move those bones. Animations belong to a skeleton, so a model's
  skeleton decides which animations it can play.

## 2. Replace a character with another one from the game

A **model swap** puts one model in another's place: the game still loads the original
name, and gets the new model. Example: fat Banjo instead of tuxedo Banjo.

1. Open **Models** and the **target**, the model the game shows where you want the change:
   `aid_model_banjox_actor_banjotuxedo`.
2. In the **Replace** section, click **Replace with another model...** and pick the
   **source**: `aid_model_banjox_actor_banjofat`.
3. The view shows the result before anything is saved. **Read the notes** under the
   buttons, and **play a few animations** (idle, walk, attack) to check that it bends
   properly.
4. Happy? **Ctrl+S** saves every copy of the target, plus the stream bundles when textures
   had to be copied. Not happy? **Discard changes**, then try another source.

What the notes tell you:

| Note | Meaning |
|---|---|
| *retargeted onto the original's skeleton: X of Y bones matched by name, Z follow their nearest matching parent* | The new model is animated with the **target's** skeleton. A few unmatched bones are fine; many mean whole parts follow the wrong bone. |
| *the new model has N part switch(es), the original M* | The target had switchable parts (Banjo: the Kazooie part and the wrench) the source lacks. Kazooie then stays in her T-pose, and with no wrench there is no attack hitbox. |
| *copied N texture(s) it needs into the bundle* | The source's textures were added. **Ship the bundle and its stream bundle** together, or the game crashes when it loads them. |

Rules of thumb:

- **Same family swaps best.** Banjo's outfits and fat Banjo share his skeleton and parts.
  `actor_banjo` on `actor_banjotuxedo` gives no notes at all.
- **Different characters** (Mumbo on Banjo) can load and animate, but limbs often bend in
  the wrong places, because their bones don't line up with Banjo's.
- **Always swap from the original.** If the target was swapped before, restore it first;
  otherwise the new swap builds on the old one.

[model-swaps.md](model-swaps.md) has the full walkthrough and a troubleshooting guide
(crashes, T-poses, distortion).

## 3. Bring your own character

Any model you make or download can go into the game: a new hero in Banjo's place, a
different companion in his backpack, a new look for an enemy or a prop. You can't add a
new character slot to the game, but you can **replace the geometry of an existing
character** with yours. The game keeps everything else: the skeleton, the animations, the
materials and the gameplay, so your model walks, jumps and fights the way the character
you replaced does. This section explains how that works and how to set your model up so
it does.

### 3.1 What the game keeps, and what you change

When NutTool imports an OBJ over a model, each object in the file replaces **one mesh**:

- **Your geometry** replaces the mesh's vertices and triangles. Any shape, any number of
  vertices (up to 65,535 per vertex buffer).
- **The material stays.** The mesh keeps its shader, its textures and its colours. You
  can't give a mesh a new material, but you can change its texture image and colours
  (3.5).
- **Bone weights are borrowed.** Each new vertex copies its bone weights from the
  **nearest original vertex of that same mesh**. This is the key to the whole process: put
  each part of your character into a mesh whose original vertices sit in the same body
  region, and it follows the right bones.
- **The skeleton and animations stay.** Your character plays the target's animations, so
  it must be built in the **target's bind pose** with its joints in the same places.
- **You can't add meshes or bones.** Every part of your character goes into an existing
  mesh. Meshes you don't use can be hidden.

### 3.2 Pick and study the target

**Pick a target** the game shows where you want your model: a playable outfit, a
character in town, an enemy, a prop. Characters of a similar build are easiest: a
two-legged model replaces a two-legged character best, because its limbs line up with
the skeleton's.

1. **Export everything.** In the target's model tab, tick **Show everything** in
   **Parts**, then **Export OBJ...** (or **Export FBX...** to get the skeleton and skin
   weights too, section 4). Or from the command line, which also writes the bone list:

   ```
   nuttool model-export <bundle> <folder> --all-groups --png
   ```

   You get one OBJ with an object per mesh (`group1_mesh30`...), an `.mtl` with each
   mesh's material, the textures as PNGs, and (from the command line)
   `<model>.bones.txt` with every bone's name, parent and world position.
2. **Learn the skeleton.** Turn on the skeleton (and bone names) in the model tab, and
   read the bones file. Note where the main joints are: hips, knees, ankles, spine, neck,
   shoulders, elbows and wrists, plus any extra bones for attachments (a backpack, a tail,
   a companion). The game works in **metres, Y up**, and characters face **+Z**. Most
   characters are stored in a T-pose or similar, arms out.
3. **Learn the meshes.** For each mesh, note **which body region its vertices cover** and
   **which texture it uses**. The model tab's **Meshes** list and the **Draw groups**
   toggles show which part is which; the exported OBJ gives each mesh's extent.
   - Check coverage, not just names. A "hands" mesh may start at the wrists, and a jacket
     mesh may reach down the arms; the actual vertices decide which bones a part gets.
   - Some draw groups are **lower detail levels**: copies of the body the game uses far
     away. Note which ones they are.
   - Note which meshes are **attachments** the game switches on and off (a weapon, a
     companion, a hat): **Parts** lists the switches.

### 3.3 Build the model

Use any 3D program. In Blender:

1. **Import the exported OBJ** (*File > Import > Wavefront (.obj)*, with **Split by
   Object** on) as a reference. Keep the default axis settings and use **the same ones when
   you export**, so the model comes back in the game's space.
2. **Bring in your model.** Model it from scratch, or import one you already have (FBX,
   glTF and most other formats open in Blender). Scale it to the target's height (the game
   works in metres), turn it to face the same way, and apply the scale and rotation.
3. **Pose it on top of the original**, in the original's pose: the same height, arms
   where its arms are, knees and elbows at its joints. A model that comes in another pose
   (arms down, say) needs reposing to match. Joints that line up are what make the
   animations bend your character in the right places.
4. **Split your model by body region**, one object per target mesh, and **name each
   object after the mesh it replaces** (for example `group1_mesh30` if that is the head).
   The number after `mesh` is what NutTool matches.
5. **Leave a little overlap** at the joints (a thigh that reaches into the hips), so
   bending doesn't open gaps.
6. **Keep faces counter-clockwise** seen from outside (Blender's normal behaviour), or the
   game culls them as back faces.
7. **Export as OBJ** (*File > Export > Wavefront (.obj)*) with the same axis settings, with
   normals, and with or without UVs (see 3.5).

**Or work in FBX and paint your own weights.** With OBJ, every vertex copies its bone
weights from the nearest original vertex. With FBX you choose them yourself:

1. Export the target with **Export FBX** (or `nuttool model-fbx`) and import it into
   Blender. You get the skeleton, the meshes and the animations.
2. Name your objects after the target's meshes as above, and parent them to the imported
   armature (*Ctrl+P > With Automatic Weights*, then fix the weights by hand).
3. Play the imported animations to check how it bends.
4. Export as **FBX** (binary, the default) with the armature and your meshes selected.
   Turn off *Add Leaf Bones*. Leaf bones aren't in the game's skeleton, so their weights
   would be dropped.

When you import the FBX, its weights are written into your vertices for every bone whose
name matches the target's skeleton. Other bones are listed in the report and left out.
Fur shells from an exported FBX (`..._fur1`, `..._fur2`...) are skipped, because the game
draws fur itself. FBX keeps an object with a single vertex and no faces, so hiding meshes
(3.4) works the same way.

How to choose the mesh for each part of your model:

| Part of your model | Put it into |
|---|---|
| head (and neck) | the target's head mesh |
| torso | a body or clothing mesh that covers the chest and hips |
| upper arms and forearms | a mesh whose vertices run down the arms (a sleeve, the arms themselves) |
| hands | the target's hand mesh |
| legs | the target's leg mesh |
| feet | the target's foot mesh |
| something on the back, a tail, a companion | the mesh of whatever sat there (a backpack, a tail): it rides on those bones |
| parts that should stay still relative to the body | a mesh skinned to the spine or the backpack |

A part's mesh decides two things at once: **which bones it follows** (from the mesh's
original vertices nearby) and **which texture it is drawn with** (the mesh's material). If
no mesh gives both the bones and the look you want, choose the bones first and fix the
look with UVs or a texture (3.5).

### 3.4 Hide everything else

Every mesh you don't fill keeps drawing the original: the character's other parts, its
attachments (a companion, a weapon, a hat), and its lower detail levels. To hide a mesh,
give the OBJ an object with that mesh's name and **one vertex but no faces**:

```
o group1_mesh5
v 0 0 0
o group1_mesh6
v 0 0 0
```

- An object **with no vertex at all is ignored**, so the single `v` line matters.
- Put the hidden objects **after all the faces** in the file, so their vertices don't
  shift the numbers your faces use.
- The exported OBJ lists every mesh name. Hide them all except the ones you filled,
  **including the lower detail levels**, or the original appears at a distance. With them
  hidden, your character can disappear when the game switches to a lower detail level;
  fill those meshes too (a simpler copy of your model works) if that matters.
- Hiding a mesh only stops it being drawn. The part it belongs to (a weapon, say) stays
  in the model with its bones.

### 3.5 Colours and textures

Each mesh draws with its own texture. You have four ways to colour your parts:

1. **No UVs.** Leave out texture coordinates (in Blender, untick UV coordinates on
   export). Each vertex then takes the UV of the nearest original vertex, so your part
   picks up the original's colours at that spot. Quick, but patchy.
2. **Point UVs at the existing texture.** Give your vertices UVs that land on a region of
   the mesh's texture with the colour you want. Many textures have large plain areas: a
   part whose UVs all sit in one of them is drawn in that colour.
3. **Paint your own texture.** Unwrap your model, paint a texture, and **Import image**
   it over the mesh's texture (any size; the texture is resized). **Careful:** textures are
   shared. A character's fur or skin texture may be used by all its outfits and by other
   models, and repainting it changes them too. Prefer a texture only your target uses:
   the texture tab's **Where it lives** and the model's texture list help you check.
4. **Untextured meshes** (many outfits and clothes) take their colour from material
   constants. Change those in **Materials** and **Save**.

A model that comes with one texture for everything works best with option 3: put it on
one mesh's texture and give every part UVs into it. Parts in other meshes can't use that
texture, so either keep the whole model in meshes that share it, or split your texture
across the meshes' textures.

### 3.6 Import, check, save

**In the GUI** (recommended): open the target's model tab, click **Import OBJ / FBX...** in
the **Replace** section and pick your file. The view shows the result straight away.

- **Play animations.** A part that stretches or swings the wrong way copied its weights
  from the wrong bones. Move it into a different mesh, or reshape it so it sits closer to
  the right part of the original.
- **Ctrl+S** writes it into every bundle that holds the model; **Discard changes** throws
  it away.

**From the command line:**

```
nuttool model-import <bundle> <model> my_character.obj -o <test copy>   # or .fbx
```

The report lists each mesh rebuilt (new vertex and triangle counts) and how many were
hidden. Check that the numbers match what your file contains: every part you made
rebuilt, everything else hidden.

### 3.7 Re-import from the original

Weights come from the mesh **as it is now**. After one import, the meshes hold your
geometry, and hidden ones hold a single placeholder vertex. A second import on top would
copy weights from those instead of from the original character. So start each new import
from the original model:

```
nuttool model-swap <bundle> <model> <bundle>.bak <model> -o step1
nuttool model-import step1 <model> my_character.obj -o <bundle>
```

The first line copies the untouched model back from the `.bak`; your other changes to the
bundle stay. In the GUI, **Discard changes** before importing again does the same for an
unsaved import; after saving, restore the model first.

### 3.8 When something goes wrong

| What you see | Likely cause | Fix |
|---|---|---|
| a part stretches or follows the wrong limb | its mesh's nearest original vertices belong to other bones | move the part to a mesh that covers that body region; check the joints line up |
| a part is invisible | faces wound clockwise, or its mesh is hidden elsewhere in the file | flip normals in your editor; check object names |
| the original still shows | that mesh isn't in the file | add it as a hidden object |
| your character vanishes at a distance | the lower detail levels are hidden | fill the lower detail levels' meshes as well |
| "one draw in two parts" error | two meshes share one draw | give geometry to only one of them |
| the swap or import works in NutTool but crashes the game | see the [troubleshooting guide](model-swaps.md#troubleshooting) | |

**Small changes** to the original (moving vertices, new UVs) keep its exact vertex and
triangle counts. NutTool then edits the mesh in place, and nothing else changes. Keep the
vertex order in your editor for that (no merging or deleting vertices).

## 4. Work in Blender with FBX

FBX carries everything a 3D program needs to show a character as the game does: its
skeleton, its skinned meshes, its textures, its animations and its fur. NutTool can also
read FBX back in.

### 4.1 Export

In the model tab, click **Export FBX...**. The options:

- **Animations**: every animation named after the model, only the one playing in the
  viewer, or none. Each one becomes a take (an *action* in Blender).
- **Fur shells** (furry characters only, on by default): adds the fur as the viewer draws it.
  Set **Fur length** and **Tiling** in the viewer first; the export uses the same values.
- **Reorient bones** (off by default): the game's bones all face the same way, so in
  Blender they stick straight up instead of following the arms and legs. With this on, each
  bone is turned to point at its child, which makes the rig much easier to select and pose.
  **Bone axis** picks which of the bone's axes points down it: **+Y** for Blender, or the
  axis your program's importer expects (often +X). The model, its skin and its animations
  look exactly the same either way; only the bones' own frames change.
- **Short end bones** (off by default): gives every bone that has no children (fingertips,
  eyes, the wrench's parts) a tiny `_end` child, so Blender draws it as a short stub instead
  of a huge spike (see 4.2). **End length** sets how short, in percent of the skeleton's size.

From the command line:

```
nuttool model-fbx <bundle> <model> out.fbx [--all-groups] [--no-anims] [--no-fur] [--fur-length 1.5] [--fur-tiling 6]
                  [--reorient-bones] [--bone-axis Y] [--bone-ends] [--bone-end-size 1.5]
```

What's in the file:

- the bones, under an empty named after the model, in their bind pose;
- one object per mesh, named `group<G>_mesh<N>` like the OBJ export, skinned to the bones;
- one material per colour map, with the textures **embedded** in the .fbx (no loose PNGs);
- the fur: for each furry mesh, one extra object per fur layer, named
  `group<G>_mesh<N>_fur<K>`. Each layer has its own texture: the fur colour, with
  transparency where that layer has no strands.

Sizes are in metres, with Y up, as the game uses them.

### 4.2 Open it in Blender

1. **File > Import > FBX (.fbx)**, and pick the file. The default settings are fine. If
   you exported with **Reorient bones** and **+Y**, the bones already point along the limbs:
   leave *Automatic Bone Orientation* off and *Primary Bone Axis* at Y. Without it,
   *Automatic Bone Orientation* does a rougher version of the same thing.
   **Some bones look huge** (the wrench's, the face's). Blender makes each bone as long as
   the average distance to its children, and a bone with no children copies its parent's
   length. Export with **Short end bones** and those childless bones become small stubs;
   tick *Ignore Leaf Bones* in Blender's import settings to hide the `_end` helpers
   themselves (the stubs keep their size). A bone with several children far apart (the neck,
   whose children include the eyes) still averages them; to tidy those too, open the **Scripting** tab,
   click **Open**, pick `docs/blender/tidy_bones.py` from NutTool's folder, select the
   armature, and click **Run Script** (▶). Each bone then reaches its main child, and
   bones with no children become short stubs. Only their drawn length changes: the mesh, the
   rest pose and the animations stay exactly the same. Or, just to see them less, select the
   armature and set *Object Data properties > Viewport Display > Display As* to **Stick**.
2. **Textures don't show at first.** Blender opens in *Solid* view, which draws everything
   grey. Press **Z** and choose **Material Preview** (or click the third sphere at the top
   right of the 3D view) to see the textures. Renders use them too.
3. **Animations** are actions. Select the armature, open the *Action Editor* (in the Dope
   Sheet) and pick one, then press Space to play it.
4. **Fur** shows as transparent layers in Material Preview and renders. In Blender versions
   before 4.2, set each fur material's *Blend Mode* to *Alpha Clip* (Material properties >
   Settings) if the layers look solid. Hide the `_fur` objects while you work on the meshes
   under them.

What looks different from the game:

- **Colours match the viewer.** Many parts share one plain texture (a grey metal, say) and
  get their colour from their material's tint, which the game's shaders multiply in. The
  export bakes each tint into a copy of the texture (`..._tint1`, `..._tint2`), the way
  the viewer shows it with **Material tint** on. With it off, the export keeps the textures
  as stored.
- **Metal and shiny parts look flat.** The FBX carries colour maps only. The game's shine
  comes from its shaders (specular, normal maps, reflections). For renders, give those
  materials *Metallic* about 1 and *Roughness* about 0.3.
- **Untextured parts** (many clothes) use their material's colour, which is the same colour
  the model tab's **Materials** section shows.

### 4.3 Bring it back

Click **Import OBJ / FBX...** in the model tab's **Replace** section (or
`nuttool model-import <bundle> <model> file.fbx`). For FBX:

- **Export binary FBX** (Blender's default). Text (ASCII) FBX files are refused.
- **Keep the object names** (`group1_mesh30`): they say which mesh each object replaces,
  exactly as with OBJ (section 3).
- **Axes and units are converted for you**, whatever the file uses, and object transforms
  are applied. You don't need *Apply Transform* in Blender.
- **Skin weights are read** for bones whose names match the target's skeleton (exactly, or
  ignoring case and underscores). This is the way to paint your own weights (3.3). Weights
  on other bones are dropped and listed in the report, so turn off *Add Leaf Bones* when you
  export.
- **Fur shells are skipped.** The game draws the fur itself, from the mesh under it.
- An object with a vertex but no faces still hides its mesh (3.4).

The skeleton is only read back when you ask for it:

- **Override rig** (the checkbox next to **Import OBJ / FBX...**, or `--rig` with
  `model-import`) moves the target's joints to your FBX's bones with the same names, so
  the game's animations bend your model where your rig's joints are. Use it when your
  model's proportions differ from the original's (longer legs, a higher waist).
- Only joint positions change. Bone orientations, the hierarchy and the bone count stay
  the game's, because its animations are made for them. Bones your file lacks follow their
  parent; extra bones are ignored and listed.
- The positions come from the file's bind pose (the skin, or the BindPose Blender writes),
  so posing the armature before you export doesn't matter.
- The file can hold just the armature, or the armature and the meshes together.
- Moving joints doesn't move the mesh: in the bind pose it looks exactly as you made it.

Animations aren't read back from FBX; the next section shows how to make new ones.

## 5. Make new animations

You can give any of the game's animations new motion. Replacing an animation keeps its
name, so the game plays your motion wherever it played the original: replace a
character's idle and it does your motion whenever it stands still; replace a cheer and it
plays when the character cheers.

You describe the motion in a JSON text file, a **timeline of poses**: which bones turn or
move, and when. NutTool eases between your poses and writes the result into the
animation.

**From Blender**: the add-on `docs/blender/nuttool_animation_keys.py` writes this file
from an animated armature (*File > Export > NutTool Animation Keys*). Animate the skeleton
from NutTool's **Export FBX...** with keyframes, constraints or IK, export, and bring the
file in as below. [animation-keys.md](animation-keys.md#from-blender) has the steps and
options. The rest of this section is about writing the file by hand.

**In the GUI** (recommended): open the character's model tab, pick the animation in the
**Animation** list, click **Replace motion...** and choose your JSON file. The viewport
plays the new motion at once; **Ctrl+S** writes it into every bundle holding the animation.
From the command line:

```
nuttool anim-import <bundle> <animation> my_motion.json
```

The full format is in [animation-keys.md](animation-keys.md), and
[examples/banjo-macarena.json](examples/banjo-macarena.json) is a complete example: a
dance routine over a character's idle.

### 5.1 Choose the animation

- **Find it.** Animations are named after their character:
  `aid_anim_<character>_<action>`, for example `..._stand`, `..._walk`, `..._cheer`. In the
  model tab, the **Animation** list shows the character's own animations; play one to see
  what it is and how long it lasts. `nuttool list <bundle>` lists them all.
- **Find its file.** A character's animations usually sit in one bundle with its model
  (`nuttool list <bundle>` shows them). If the animation is in several bundles, edit each
  of them.
- **Its length stays.** Your timeline runs over the animation's own length and frame rate.
  To make a longer routine, pick a longer animation (idles are often 10 seconds or more).
- **Loops.** Idles, walks and runs loop, so end in the pose you start with.

### 5.2 Learn the skeleton

- **Bone names**: the model tab's **Skeleton** tree, or the `.bones.txt` file that
  `nuttool model-export` writes. Typical names: `BASE` (the hips and whole body), `BACK`
  (spine), `NECK`, and per side (`LF_` left, `RT_` right) `_C` collarbone, `_S` shoulder,
  `_E` elbow, `_W` wrist, `_H` hip, `_K` knee, `_A` ankle.
- **The rest pose.** In `.bones.txt`, the last four numbers of a bone are its rest rotation.
  `0 0 0 1` means none, which is the case for most of the game's characters: their rest
  pose is a T-pose, arms straight out to the sides, and every bone's axes are the
  model's. **+X** is the character's left, **+Y** up, **+Z** forward.

### 5.3 Write poses

A pose sets bones' rotations (`"rot"`, in degrees) and, for `BASE`, its offset (`"move"`, in
metres). A rotation turns about X first, then Y, then Z. Each bone's rotation is measured
from the rest pose and is relative to its parent bone. On a T-posed character:

| To | Bone | rot |
|---|---|---|
| lower an arm to the side | `RT_S` / `LF_S` | `[0, 0, 80]` / `[0, 0, -80]` |
| point an arm forward | `RT_S` / `LF_S` | `[0, 90, 0]` / `[0, -90, 0]` |
| raise an arm straight up | `RT_S` / `LF_S` | `[0, 0, -85]` / `[0, 0, 85]` |
| turn the palm over (arm forward) | `RT_S` / `LF_S` | `[180, 90, 0]` / `[180, -90, 0]` |
| bend an elbow (arm out, palm down) | `RT_E` / `LF_E` | `[0, 90, 0]` / `[0, -90, 0]` |
| lift a thigh forward | `RT_H` / `LF_H` | `[-30, 0, 0]` |
| bend a knee | `RT_K` / `LF_K` | `[60, 0, 0]` |
| lean or nod forward | `BACK`, `NECK` | `[20, 0, 0]` |
| turn the head to its left | `NECK` | `[0, 30, 0]` |
| lean to its right | `BACK` | `[0, 0, 15]` |
| hop | `BASE` | `"move": [0, 0.15, 0]` |

Combine them: the X part turns a limb about its own length (the palm), then Y and Z aim it.
For poses where both the shoulder and the elbow bend (hands on hips, behind the head),
expect to adjust the numbers a few times. Small changes of 10 to 20 degrees are easy to
see in game.

Also key:

- **The bones above the ones you pose.** An arm hangs from the collarbone and the spine. If
  you don't key those, they keep the original animation's movement, and your arm poses lean
  and sway with it. Key `BACK`, `BACKA`, `BACKB` and the collarbones (`_C`) to `[0, 0, 0]`
  for a steady upper body.
- **The legs when you lower the body.** Moving `BASE` down without bending the knees pushes
  the feet into the floor. Bend the hips, knees and ankles, and lower `BASE` by the height
  that takes away (about 4 to 5 cm for a medium crouch).

Bones you never key keep playing the original animation: the face, blinking, fingers, a
backpack companion. Use `"rot": "base"` to hand a bone back to the original motion at a
key.

### 5.4 Put them on the timeline

```json
"timeline": [
  { "time": 0.0, "pose": ["upright", "armsDown"], "hold": 0.4 },
  { "time": 0.6, "pose": ["upright", "rightArmForward"], "hold": 0.3 },
  { "time": 1.2, "pose": ["upright", "bothArmsForward"] }
]
```

- **time** is when the pose is reached, and **hold** keeps it that long before easing to the
  next one. A quick move followed by a hold reads as a dance step; keys with no hold flow
  into each other.
- **Each bone eases between its own keys.** A bone left out of an entry doesn't stop
  there: it keeps easing from its previous key toward its next one. So **name every moving
  bone in every entry** of a sequence (both arms, even when only one changes). Reusable
  poses make that easy: list the poses for both sides in each entry.
- Before a bone's first key and after its last it holds still.
- Rotations take the shortest way round, so give a turn of more than 180 degrees an extra
  key halfway.

### 5.5 Import, check, repeat

Always start from the original animation, so a second try doesn't build on the first.

**In the GUI**:

- **Build on the original** (on by default) reads the animation from the bundle's `.bak`,
  which NutTool makes before it first writes the bundle. With no `.bak` yet, it uses the
  bundle as it is.
- After editing the JSON, click **Reload** to see the change; **Discard** goes back to the
  saved motion. Nothing is written until **Ctrl+S**.
- Under the buttons, the report says how many bones were keyed and how many timeline
  entries were used. It notes any key past the animation's end.

**From the command line**:

```
nuttool anim-import <bundle>.bak <animation> my_motion.json -o <bundle>
```

(The first time, run it without `.bak` and `-o`: NutTool makes the `.bak` itself.)

- The command prints the same report.
- **Check it in NutTool**: open the character's model tab (close and reopen it if it was
  already open), pick the animation in the **Animation** list and play it.
- **Check it in game**: package the mod (section 14) and load it in reNut.

### 5.6 Limits

- The animation's length and frame count stay. Very fast moves can fall between the
  stored frames (many animations store 15 frames a second).
- `move` reaches at most 1 m from a bone's rest position.
- The skeleton stays the same: you can't add bones.
- Facial animation channels are left as they are.
- Motion can't be read from Blender or other programs yet; you write the timeline by hand.

## 6. Textures in depth

- **Split textures and stream bundles.** A texture whose name ends in `mip` has its
  full-size level in the paired stream bundle (`Bundle\50\<same name>`). The texture tab and
  bulk import write both halves; the command line needs `--stream`:

  ```
  nuttool tex-import assets\Bundle\4f\01ca49 jinjo_body_0x0332d095 jinjo.png --stream assets\Bundle\50\01ca49
  ```

- **DDS is lossless.** A DDS in the texture's own format (the tab names it: DXT1, DXT5...)
  is stored as it is, with its own mips. PNG, JPG, BMP and TGA are compressed by NutTool
  and get a fresh mip chain.
- **Resizing.** Any size up to 8192x8192, for 2D and array textures. The mip chain keeps
  ending where it did. The model's UVs need no change.
- **Array (animated) textures** hold several frames. Bulk import takes
  `<name>_slice0.png`, `<name>_slice1.png`...; all frames must be the same size. The
  texture tab's **Import image...** puts one image in every frame.
- **Remove mipmaps** (texture tab, or **Tools > Remove mipmaps from changed textures...**
  for everything you changed) drops the smaller copies to save memory. Distant surfaces may
  shimmer without them. Importing an image again rebuilds them.
- **Cube maps and volume textures** can be viewed and exported, not imported.

## 7. Menus and the title update

The menus (XUI scenes, in `aid_xuipackage_*` assets) draw many images at the image's own
pixel size. When you enlarge a menu texture, NutTool fits every menu element that uses it so
it keeps its size on screen. That happens after each import that resizes a texture.
**Tools > Fit menus to resized textures** runs it by hand, for example after restoring files.

**The title update brings its own menus.** With title update 3, the front end and button
legends come from the update's bundle (`aid_bundle_banjoxdlc_titleupdatecommon`, the file
`4f3ef5f9`), not the base game's. If your upscaled buttons look cropped only on a
title-update build, those menus haven't been fitted:

1. Give NutTool its own copy of the title update files: a folder named `update` next to
   your `assets` folder, or set one with **File > Title update folder > Choose...**.
2. Re-index (the browser gets a **Title update** group), then **Tools > Fit menus to
   resized textures**. The log shows the update's menus being adjusted.
3. Build your mod again. The update's file goes in as `update/4f3ef5f9`, which reNut
   serves in place of the update's own.

Command line:

```
nuttool menu-fix assets --update F:\NutTool\update
```

Limit: **nine-grid** elements (stretchable panels) aren't adjusted; the log names any that
use a resized texture. The Start button in the full button menu is one.

## 8. Vehicle parts

Every garage part's settings are in an objparams asset
(`aid_objparams_banjox_vehicleblock_*`). Open one from the **Vehicle parts** group (sorted
by category: wheels, weapon, propulsion...). The tab shows its fields as text:

```
name               = "wheelmonster"  # 0x0A0
mass               = 40.0  # 0x190
health             = 40.0  # 0x194
ratingGrip         = 3  # 0x1B4
0x3C8              = 1.5
```

- **mass**, **health** and **power** (engines and jets) are what the game simulates
  with. Mass is confirmed in the game's code: it scales the part's physics.
- **rating...** fields are the shop's 0-5 stars only. Changing them doesn't change how the
  part behaves.
- **Fields by offset** (`0x3C8 = 1.5`) are class-specific settings without a name yet:
  wheel grip curves, engine tuning, weapon values. Edit them by experiment.
- Fuel tanks have **fuelCapacity**, ammo boxes **ammoCapacity**.
- Only values change; the layout is fixed. A line you remove keeps its value; a `0x...`
  line you add sets that word.
- **Save to every copy** writes the change.

Command line: `nuttool objparams-export assets\Bundle\4f\685374 parts --parts`, edit the
text files, then `nuttool objparams-import assets\Bundle\4f\685374 parts`. Any
`aid_objparams_` asset (not just parts) opens the same way from a bundle tab, with its
fields by offset.

## 9. Game scripts

The **Scripts** group lists the game's scene scripts (`aid_script_`), actor goal lists
(`aid_actorgoals_`, what characters do), script tables and state tables. A script tab shows:

- **the text**, one command per line:

  ```
  op_60 "Nutty Acres Act 6 Game 4 Nuts To Go" @aid_challenge_banjox_nuttyacres_act6_game4 1
  op_00
  ```

  `op_XX` is the command number (the game has no names for them), followed by its
  arguments: numbers, `"strings"`, and `@aid_...` for other assets. Actor goals read
  like `actorMindGoalId_Wait 1.5 3.0`.
- **Uses**: the assets it refers to. **Used by**: the assets that refer to it (an
  objparams pointing at a script table, which points at actor goals). Click a name to open
  it. Scripts nothing points at are started by the game's code.

Editing:

- Commands and goals can be added, removed and reordered.
- Each command has a fixed list of arguments; the line under the editor says what's wrong
  if yours don't fit.
- A scene script must end with `op_00`.
- **Save to every copy** writes it. In a bundle tab, **Apply** changes that bundle only.

Command line: `nuttool gamescript-export <bundle> <folder>` and `gamescript-import`.

## 10. Fonts

Open a font from **Fonts** to see every glyph. Select one to see it large, change its
**Metrics** (spacing), or **Replace from PNG...**. **Export...** writes the glyph pages as
PNGs (white letters, transparency = coverage) plus `font.json`, a TrueType `.ttf` and a
BMFont `.fnt`. Edit the pages and **Import pages...** to bring them back. The `.ttf` and
`.fnt` are for using the game's font elsewhere; NutTool doesn't read them back.

## 11. The command line

`nuttool.exe` does everything the editor does, for batch work and scripting. `nuttool help`
lists every command. The ones you'll use most:

```
nuttool info <file>                          # what is this file?
nuttool list <bundle>                        # every asset inside
nuttool index assets                         # every named asset and its file
nuttool extract-all assets out --png         # unpack everything
nuttool tex-export <bundle> tex --png        # textures
nuttool tex-import <bundle> <texture> <image> [--stream <stream bundle>]
nuttool model-export <bundle> models [--all-groups] [--png]
nuttool model-fbx <bundle> <model> out.fbx   # skinned, with animations and fur, for Blender/Maya
nuttool model-obj <bundle> <model> out.obj   # the same meshes, materials and fur as OBJ
nuttool model-import <bundle> <model> <file.obj|file.fbx> [--rig]   # --rig: your FBX's joint positions too
nuttool model-swap <bundle> <target> <source bundle> <source>
nuttool anim-import <bundle> <animation> <motion.json>   # new motion for an animation
nuttool menu-fix assets [--update <dir>]
nuttool make-nbpatch assets mymod.nbpatch --name "My mod"
nuttool verify assets                        # can every file be read back exactly?
```

- Commands that change a file write to `-o <path>` when you give one. Otherwise they change
  the file in place after a one-time `.bak`.
- The command line changes **one file at a time**. The GUI's tabs change every copy of an
  asset, so prefer them for anything that lives in several bundles.

## 12. Automate with Lua

**Tools > Script (Lua)...** opens an editor that runs Lua scripts over the game files
(Ctrl+Enter runs). Scripts can open bundles, read and replace assets, inspect models,
recolour materials, import OBJs, read audio and run any `nuttool` command:

```lua
local f = nut.open("Bundle/4f/685374")
for _, name in ipairs(f:models()) do
  local m = f:model(name)
  print(name, m.meshes, m.vertices, #m.bones)
end
```

From the command line: `nuttool script my.lua --assets F:\game\assets`. The full
reference and recipes are in [scripting.md](scripting.md).

## 13. Raw assets

For assets NutTool has no editor for yet, work with the raw bytes:

- In a bundle tab, select an asset and use **Export raw...** / **Replace raw...**.
- Or unpack a whole bundle, change files, and pack it again:

  ```
  nuttool caff-unpack assets\Bundle\4f\0a04d0 unpacked   # caff.json + one file per asset
  nuttool caff-pack unpacked assets\Bundle\4f\0a04d0
  ```

Assets can change size; later assets move and their pointers stay valid. But pointers
*inside* an asset are listed in `caff.json` (`relocsA`): if you change an asset's
internal layout, update those offsets too. Same-layout edits (changing values) are safe.

## 14. Mods in depth

- **Two ways to find your changes.** **Files that have a .bak backup** (the default) packs
  every file you saved with backups on. **Files that differ from a clean copy of the game
  folder** compares with an untouched copy instead, useful if you edited files outside
  NutTool.
- **Title update files** go in as `update/<file>` entries, which reNut serves in place of
  the update's own (see [7](#7-menus-and-the-title-update)).
- **Ship pairs together.** A changed bundle often has a changed stream bundle (split
  textures, model swaps). Both have backups, so both are listed; keep both ticked.
- **Mods are tied to the original files.** Each entry records the original's checksum, and
  reNut skips a mod whose originals differ from the player's.
- **Two mods can't change the same file**: reNut loads the first and skips the other. Merge
  such changes into one mod.
- `default.xex` changes are left out: reNut runs recompiled code, so they would have no
  effect.

When something breaks in game, reNut writes `logs\renut_N.log` and a crash dump next to
`renut.exe`. Keep them, with what you changed, when you ask for help.
