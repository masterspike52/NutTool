# Model swaps

A model swap puts one model in another's place. The game keeps loading the original
name (say `actor_banjotuxedo`) and gets the new model (say fat Banjo, `actor_banjofat`).
NutTool copies everything the new model needs, so the game can load and animate it.

- [Before you start](#before-you-start)
- [Swapping in the GUI](#swapping-in-the-gui)
- [Swapping from the command line](#swapping-from-the-command-line)
- [Reading the swap notes](#reading-the-swap-notes)
- [Testing in reNut](#testing-in-renut)
- [Troubleshooting](#troubleshooting)
- [Undoing a swap](#undoing-a-swap)
- [What a swap changes](#what-a-swap-changes)

## Before you start

- **Keep backups on.** *File > Make .bak backups when saving* is on by default. The
  first save of a file copies the original to `<file>.bak`. Restoring and packaging mods
  both depend on those backups.
- **Swap from the original.** A swap works on the model as it is on disk. If the target
  was swapped before, restore it first (**Tools > Restore original files**), or the new
  swap builds on the old one, including any problems the old one had.
- **Pick the right target:** the model the game shows in the situation you care about.
  For Banjo's outfits that is the outfit's own model (`actor_banjotuxedo` for the
  tuxedo).
- **A good source** has a skeleton like the target's. Models of the same character
  (Banjo's outfits, fat Banjo) match well. A different character (Mumbo on Banjo) can
  load and animate, but limbs are likely to bend in the wrong places. See
  [the notes](#reading-the-swap-notes).

## Swapping in the GUI

The GUI is the easiest way. It previews the result before anything is written, and
saves every copy of the model at once.

1. Open the assets folder and wait for indexing to finish.
2. In the browser, open **Models** and the **target**, for example
   `actor_banjotuxedo`.
3. In the tab's **Replace** section, click **Replace with another model...** and pick the
   **source**, for example `actor_banjofat`.
4. The viewport now shows the swapped model. **Read the notes** under the Replace
   buttons; [the next section](#reading-the-swap-notes) explains them.
5. **Play some animations** from the Animation list: idles, walking, attacks. The
   preview uses the same retargeted skeleton the game will use, so a model that bends
   wrongly here bends wrongly in game.
6. Happy? Press **Ctrl+S**. NutTool writes the swap into every bundle that holds the
   model, plus the paired stream bundle (`Bundle/50/<same name>`) when it copied
   textures. Each file gets its `.bak` on the first save.

Not happy? Click **Discard changes** before trying another source. Previews stack: a
second swap without discarding previews the new source on top of the first, while saving
would apply only the last one. Discarding keeps the preview honest.

## Swapping from the command line

```
nuttool model-swap <bundle> <target model> <source bundle> <source model> [-o out] [--stream-out out]
```

For example, fat Banjo in the tuxedo's place:

```
nuttool model-swap assets\Bundle\4f\685374 banjox_actor_banjotuxedo assets\Bundle\4f\01ca49 banjox_actor_banjofat
```

- Model names may leave out `aid_model_`.
- Without `-o`, the bundle is changed in place (with a `.bak`). So is its paired stream
  bundle when textures are copied. With `-o`, give `--stream-out` too, for where the new
  stream bundle goes.
- The command changes **one bundle**. Some models ship in several bundles, and the game
  may load any of them. The GUI's model tooltip shows how many copies there are and saves
  them all. From the command line, swap each bundle that holds the target. The
  [scripting recipes](scripting.md#find-every-bundle-holding-a-model) show how to find
  them.
- The notes are printed under the "now holds" line.

## Reading the swap notes

A swap reports what it had to adapt. Here are the notes for fat Banjo on the tuxedo:

```
aid_model_banjox_actor_banjotuxedo now holds aid_model_banjox_actor_banjofat
  retargeted onto the original's skeleton (175 bones): 166 of its 173 bones matched by name, 7 follow their nearest matching parent
  the new model has 0 part switch(es), the original 3; parts the game switches on and off (Kazooie, the wrench...) may be missing
  added 33 shared string(s) and shader(s) to the destination's pools
  copied 8 texture(s) it needs into the bundle (1476 KB, 8 streamed top level(s) for the stream bundle)
```

**No notes at all** is the best case. The skeletons are identical and the parts match:
`actor_banjo` on `actor_banjotuxedo` gives none.

| Note | What it means | What to do |
|---|---|---|
| **retargeted onto the original's skeleton: X of its Y bones matched by name, Z follow their nearest matching parent** | The game animates the model with the **original's** skeleton and animations, so every vertex is re-bound to the original's bones. X found a bone with the same name. The other Z had none and now move with the nearest parent bone that matched. | A small Z is fine (fat Banjo: 7 of 173). A large Z means whole parts ride on the wrong bone and **will look wrong when animated** (Mumbo on Banjo: 68 of 127). Check the animations in the preview, or pick a closer source. |
| **the new model has N part switch(es), the original M; parts ... may be missing** | The original had parts the game switches on and off, and finds by name: for Banjo, the Kazooie part and the wrench. The new model lacks them. | Expect Kazooie to stay in her bind pose (a T-pose), and no wrench, so **no attack hitbox**. Swap between models with the same parts to keep them. See [Troubleshooting](#kazooie-t-poses-or-there-is-no-wrench-and-no-attack-hitbox). |
| **added N shared string(s) and shader(s) to the destination's pools** | Names and shaders the new model uses were added to the bundle. | Nothing. |
| **copied N texture(s) it needs into the bundle (… streamed top level(s) for the stream bundle)** | The game binds every texture a model names when it loads, and crashes on a missing one. So the textures the bundle lacked were copied in, with their full-size levels going into the paired stream bundle. | **Ship both files**: the bundle and `Bundle/50/<same name>`. |

## Testing in reNut

- **Package the change** with **Tools > Export mod package (.nbpatch)** (or `nuttool
  make-nbpatch`). It takes every file that has a `.bak`, so the stream bundle is
  included when the swap changed it.
- **If the game crashes,** reNut writes a log (`logs\renut_N.log`, look for "Unhandled
  guest access violation") and a crash dump (`crashdumps\*.dmp`) next to `renut.exe`.
  Keep both with the notes from the swap.

## Troubleshooting

### The character looks distorted, melted or stretched (the "horror from the crypt")

This happened once with fat Banjo on the tuxedo. His belly and shorts collapsed in game.

**The cause was in NutTool, and it is fixed.** That version of the swap moved every
vertex to fit the original skeleton's joint positions, which crushed parts of the mesh.
NutTool now never moves vertices. It only re-binds them to the original's bones (by
name, else the nearest matching parent). At rest, the model keeps its exact shape;
animated, its joints turn around the original's joint positions, which for fat Banjo on
the tuxedo are within 0.04 units of his own. Today's NutTool writes exactly the file
that then ran correctly in game, byte for byte (Kazooie T-posing aside, see below).

If a swapped model looks wrong anyway, work down this list:

1. **Is the file from a current NutTool?** A swap made by the old version keeps its
   distortion, and swapping again on top of it builds on the damaged copy.
   **Tools > Restore original files**, then swap again from the original. Rebuild any
   `.nbpatch` you made from the old files.
2. **Does it look wrong in NutTool's preview?** Open the target's tab after saving (or
   preview the swap) and play several animations.
   - **Wrong in NutTool too:** the source's skeleton doesn't fit. Look at the
     retarget note. Many bones following a parent means parts move with the wrong
     bones. Use a source with a closer skeleton.
   - **Distorted even standing still, with no animation:** that isn't skinning. Check
     that you swapped the model you meant to, and that no OBJ import was applied with it.
3. **Right in NutTool but wrong in game:** keep the swap notes, a screenshot, the source
   and target names, and the reNut log. The game layers some animations (Kazooie's idles
   on Banjo's), which NutTool's viewer doesn't, so that combination is the first
   suspect.

### Kazooie T-poses, or there is no wrench and no attack hitbox

The original model had parts the game switches on and off and finds by name: the Kazooie
part and the wrench. The new model doesn't have them (the swap note says so). Kazooie then
stays in her bind pose, and without the wrench there is no attack hitbox.

This is a **known limit** of swaps today. Swap between models that have the same parts,
such as Banjo's outfits (`actor_banjo` on `actor_banjotuxedo` keeps them), and they work.
Grafting the original's parts into the new model was tried and dropped: it crashed the
game.

### The game crashes when the level loads

- **Ship both files.** If the swap copied textures, the paired stream bundle changed
  too. A mod with only the bundle is missing the textures, and the game crashes binding
  them.
- **Redo it from the original with a current NutTool.** Earlier versions crashed the game
  this way: they didn't copy missing textures, or put new data after the bundle's
  `manifest` (which the game reads as the last entry). Both are fixed.
- Look at the reNut log. A crash in texture binding points at textures, and one in
  animation blending at the skeleton (next item).

### The game crashes when a save loads, or when the character appears

The game animates a model with the skeleton its animations were made for. A model with a
different skeleton, not retargeted, crashed it in animation blending. Current swaps always
retarget when the bone lists differ. Restore and swap again with a current NutTool.

### The swap doesn't show in game

- The target is in **several bundles** and only one was swapped (command line). Use the
  GUI, which saves every copy, or swap each bundle.
- It's the wrong target. The game shows a different model there, for example another
  outfit's.
- The mod wasn't rebuilt after the swap, or the game is loading the untouched files.

## Undoing a swap

- **Before saving:** **Discard changes** in the model's tab.
- **After saving:** **Tools > Restore original files** lists every file that differs from
  its `.bak`. Tick the bundle (and its `Bundle/50` stream bundle) and restore. Or copy the
  `.bak` files back by hand.

## What a swap changes

For the curious. The README's Models section has the full technical description.

- The target's `.data`, `.gpu` and `.stream` parts become copies of the source's, with
  their internal pointers fixed up. Names and shaders the bundle lacks go into its string
  pools.
- **Textures** the bundle lacks are copied in, each as two symbols, placed before the
  bundle's `manifest` and listed in it. The streamed full-size level of a split texture
  goes into the paired stream bundle.
- **Skeleton:** when the bone lists differ, the model keeps pointing at the original's
  skeleton (saved in a `<model>_skeleton` symbol), the bone table is the original's, and
  each vertex is re-bound by bone name, or else to the nearest parent that matches.
  Vertices are never moved.
- **Parts** (switched nodes such as Kazooie and the wrench) come from the new model,
  which is why a source without them loses them.
