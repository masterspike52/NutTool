# nuttool

A tool for reading, unpacking and editing the assets of
**Banjo-Kazooie: Nuts & Bolts** (Xbox 360). It handles everything in the game's
`assets` folder except `default.xex` / `default.xexp`.

It comes in two forms that share the same code:

- **`nuttool-gui`**: a desktop editor (Dear ImGui + SDL3).
- **`nuttool`**: a command-line tool for scripting and batch work.

Both are Windows programs.

New to NutTool? NutTool's documentation lives in reNut's repository:
**[NutTool's docs](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/README.md)** cover
setting up, every window of `nuttool-gui` ([the NutTool window](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/gui-guide.md)),
custom models, animations, parts and vehicles, [worlds](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/worlds.md),
[characters](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/characters.md), player models,
textures and text, script graphs, the command line, packaging and sharing, and troubleshooting.

## What it handles

| Location | Format | Read / unpack | Edit / repack |
|---|---|---|---|
| `Bundle\4f\*` | Bundles: XCTD-compressed (LZX) CAFF containers | yes, plus every asset as a separate file | yes (lossless repack, resize assets) |
| `Bundle\50\*` | Stream bundles: archives of standalone CAFF assets and XACT wave banks | yes | yes |
| `Debug\11\*`, `loctext\<lang>\*` | Localised string tables (CAFF + LSBL) | to UTF-8 JSON | edit, add and remove strings |
| textures inside bundles/stream bundles | Xbox 360 tiled GPU textures (DXT1/3/5, DXN, DXT5A, 8888, A8, L8; 2D, cube, array) | to DDS / PNG, with streamed top mips merged | import DDS, PNG, JPG, BMP or TGA (mips regenerated; another size resizes the texture) |
| `Debug\36\*` | XMV movies (plain WMV/ASF) | info, copy out as `.wmv` | replace the file |
| wave banks in stream bundles | XACT wave banks (XMA / PCM / ADPCM) | `.xwb` plus per-wave `.xma` / `.wav` | replace a whole bank entry |
| `xuifontcachefont\*.sbp` | XUI glyph caches (zlib, 4 bpp) | PNG pages + `font.json`, and as a font (`.ttf` + BMFont `.fnt`) | edit glyphs and metrics |
| `xuifontcachemeta\*.ini` | font metadata (ASCII / UTF-16BE) | yes | yes |
| game scripts inside bundles | scene scripts, actor goal lists, script and state tables | to editable text, with what uses each one | edit the text (any number of commands) |
| objparams inside bundles | object parameters, including every vehicle part | to editable text (named fields for parts) | edit any field |
| `Debug\db_index.txt` | asset index | resolves every name to its file | — |
| `tile.bmp` | plain BMP | copied | any image editor |

`nuttool verify <assets>` parses and re-serialises every container, string table and
font in the game and checks the result is byte-identical. On the shipped data it
reports **16,663 passed, 0 failed**.

## Building

The GUI uses SDL3, Dear ImGui (docking branch, SDL3 + OpenGL 3 backends) and glad.
Scripting uses Lua 5.5 (built as C++), audio uses FFmpeg to decode XMA, and texture import
uses stb_image to read JPG and other images. All of them are declared in `vcpkg.json` and
fetched in vcpkg manifest mode. Without Lua the tools still build, but scripts cannot run;
without FFmpeg only the few PCM waves play; without stb only PNG and DDS can be imported.
You need clang, Ninja and the `VCPKG_ROOT` environment variable pointing at a vcpkg
checkout:

```
cmake --preset win-release
cmake --build out/build/win-release
```

This produces `out/build/win-release/nuttool-gui.exe` and `nuttool.exe`. They link
statically (`x64-windows-static`), so there are no DLLs to ship. Use `win-debug` for a
debug build.

The first vcpkg build takes a while: FFmpeg alone is several minutes.

### Command-line tool only

The command-line tool alone needs nothing but a C++20 compiler:

```
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DNUTTOOL_GUI=OFF
cmake --build build
```

## The GUI

This section is a summary for reference. **[NutTool's docs](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/README.md)**
in reNut's repository explain every window and button step by step:
[the NutTool window](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/gui-guide.md),
[custom models](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/custom-models.md),
[parts and vehicles](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/parts-and-vehicles.md),
[worlds](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/worlds.md),
[characters](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/characters.md) and
[troubleshooting](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/troubleshooting.md).

Start `nuttool-gui.exe`. It finds an `assets` folder next to it or in the current
folder; otherwise use **File > Open assets folder**. You can also pass a folder or file on
the command line, or drag one onto the window.

- **Tabs**: everything opens in a tab. **Collapse all** and **Close all** sit at the right
  end of the tab bar (also in the View and File menus). Right-click a tab for **Close**,
  **Close other tabs**, **Close tabs to the right** and **Close all tabs**; tabs with unsaved
  changes ask first.
- **Browser** (left): every bundle, stream bundle, string table (by language), video,
  font and metadata file, named from `db_index.txt`, with a search box and **Collapse
  all**. When a folder
  opens, the tool indexes every texture and model in the background (a few seconds;
  cached in the settings folder) and adds three groups:
  - **Textures**: every texture in the game, once each, wherever it lives.
  - **UI icons**: the textures the XUI menus use (those listed in `xuicachefile` and
    `*_ui_*` textures).
  - **Models**: every model, once each.
- **Texture tabs**: preview with mip/channel controls, the files and entries holding
  each copy (with **Open** buttons), **Export PNG / DDS**, and **Import image** (PNG,
  JPG, BMP, TGA or DDS), which writes the new image into every copy (bundles, stream
  bundle entries and the streamed top level of split textures). An image of another
  size, such as a higher-resolution repaint, resizes the texture in every copy. If any
  copy can't take it, no file is changed. **Remove mipmaps** drops the mip chain from
  every copy (see [Removing mipmaps](#removing-mipmaps)).
- **Model tabs**: a 3D viewer (OpenGL) with textures, material editing, fur shells,
  wireframe, the skeleton with optional bone names, and a grid.
  - **Camera**: left-drag orbits, right-drag turns the camera in place, middle-drag
    (or Shift+left-drag) pans, and the wheel zooms. W/A/S/D or the arrow keys fly
    (Q/E for down/up, Shift to go faster) while the mouse is over the view.
    Double-click resets.
  - **Parts**: the model's detail level and its switches, following the game's own
    rules (from the authoring node names). Switches pick alternative parts: Banjo's
    wrench or camera, goggles on or off. Every switch also has an **off** state.
    **Show everything** turns on every draw group, and the **Draw groups** list
    toggles them one by one.
  - **Meshes**: each draw with its material name and texture. The texture defaults to
    the colour map among those the draw binds; a dropdown picks another. Untextured
    materials show their base colour, and the tooltip lists every material constant.
  - **Materials**: every material of the shown meshes, one collapsible entry each, with
    all of its parameters: a colour picker for colours (`c:basecolour`,
    `c:velvetColour`, `c:specularColour`...) and four number fields for the rest
    (`c:specularPower`, `c:textureGain0`...).
    - The search box finds materials by name, by parameter name (listing just those
      parameters) or by the textures their meshes use. **Colours only** hides the
      other parameters.
    - **Click the model** to pick the material under the cursor: the list shows just that
      material, opened, until **Show all materials** (or a click on empty space). The
      picked material, or the one under the mouse in the list, is tinted orange in the
      view.
    - A material's tooltip lists its meshes; an edited material is marked `*` and has
      **Revert all**.
    - A change applies to every mesh and detail level that uses the same material
      value.
    - **Save** writes the new values into every bundle that holds the model, keeping a
      `.bak` of the original.
    - Many outfits have no colour texture and take their colour from these constants,
      Banjo's tuxedo (`banjoTuxedoModel1_tuxFabric`) among them.
  - **Material tint**: multiplies textures by the material's `c:textureGain`, as the
    game's shaders do (the grey metal textures are tinted per material).
  - **Fur**: draws fur as shells, one per slice of the fur volume texture, with length
    and tiling sliders.
  - **Lighting**:
    - **Studio** mode lights the model with a key, fill and rim light plus sky/ground
      ambient. The lights follow the camera, or stay fixed at a chosen direction and
      height.
    - **Headlight** mode uses a single light at the camera; **Unlit** shows flat colours.
    - Sliders set brightness, ambient and the light colour.
  - **Animation**: the animations named after the model (Banjo has 171), with a filter
    that searches every animation in the game when nothing matches.
    - Playback has play/pause, a time slider, speed, loop, and **Bind pose** to stop.
    - The mesh is skinned to the animated skeleton, and the skeleton overlay follows it.
    - Bones are matched by name, so other characters' animations apply only to the
      bones the two skeletons share.
    - **Replace motion...** gives the shown animation new motion from a JSON timeline
      (the `anim-import` format, [Animations](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/animations.md); the
      Blender add-on [`nuttool_animation_keys.py`](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/blender/nuttool_animation_keys.py)
      exports it from an animated armature) and
      plays it straight away. **Build on the original** keys the animation from the
      bundle's `.bak`, so a new try doesn't build on an earlier one, and **Reload**
      re-reads the edited JSON. **Ctrl+S** writes the motion into every bundle holding
      the animation.
  - **Textures** (thumbnails, each opening its own tab), the **skeleton** tree, and the
    **materials**.
  - **Export OBJ** writes the shown meshes with the FBX export's materials, tinted PNG
    textures and fur shells (`.obj`, `.mtl` and PNGs side by side). OBJ holds no
    skeleton, skin or animation; use **Export FBX** for those.
  - **Export FBX** writes one `.fbx`: the shown meshes skinned to the skeleton, the
    materials with their colour maps embedded at full size, and animations as takes. You
    can include every animation named after the model, only the one playing, or none.
    Furry models also get their fur (**Fur shells**, on by default), using the viewer's
    fur length and tiling. **More options...** in either export opens *Tools > Export a
    model* for the model's bundle (every animation, another bundle's animations, no
    textures, fur texture size: all of `model-fbx` / `model-obj`).
  - **Replace**: **Import OBJ / FBX** applies an edited export or new geometry (see
    "Models" below); with **Override rig** it also moves the joints to the FBX's bones of
    the same names. **Replace with another model** copies any other game model into
    this one's place. Both show in the viewport straight away; **Ctrl+S** writes them into
    every bundle holding the model, and **Discard changes** goes back to what is on disk.
  Bundle views have an **Open in model viewer** / **Open texture tab** button for
  models and textures.
- **Bundles**: every asset inside with a filter. Selecting one shows a hex view and
  **Export raw / Replace raw**. Selecting a texture shows a preview with mip/face/channel
  controls and **Export PNG / Export DDS / Import image / Remove mipmaps**. Streamed top levels from the
  matching stream bundle are merged automatically, and importing updates both files.
  There are also buttons for **Export all textures**, **Unpack to folder** and **More
  exports...** (*Tools > Export from a file* for this bundle).
- **Stream bundles**: an entry list. CAFF entries open in the same asset view; wave banks
  open the audio player (below); entries can be exported or replaced. **Unpack all**,
  **Export all audio** and **More exports...** work on the whole file.
- **Audio**: every XACT wave bank in the game (420 banks, 8,963 waves, about 5 hours),
  grouped by the first word of their names: Impacts, Vocals, Ambience, Objects,
  Bolt-ons, Dialog, Music, Footsteps, Engines and so on. The browser search also finds
  the banks that hold a wave whose name matches, and opens them filtered to it.
  - Click a wave to play it. The player shows a waveform with the playhead (click or
    drag to seek), Play/Pause, Stop, Loop, and volume. Loop repeats the wave's authored
    loop region, shaded in the waveform, as the game does; without one it repeats the
    whole wave.
  - **Export WAV** writes the selected wave decoded to 16-bit PCM, and **Export
    original** writes it as stored. **Export all as WAV** and **Export bank +
    originals** do the whole bank in the background.
  - Almost every wave is XMA, the Xbox 360's WMA Pro variant, decoded with FFmpeg.
- **Text**: a string table editor with search and an English reference column for other
  languages, plus wrapped previews, add/delete string, and JSON import/export (**Drop
  strings it lacks** makes the import remove the strings the JSON doesn't list, as `--prune`).
  CJK text uses the system fonts (Microsoft YaHei, Malgun Gothic).
- **Fonts**: a grid of every glyph. Select one to see it large, edit its metrics, or
  replace it from a PNG. Glyph pages can be exported and imported; the export also
  writes the font as a `.ttf` and a BMFont `.fnt`.
- **Scripts** (once indexing is done): every scene script, actor goal list, script table
  and state table in the game, by kind. Searching also matches the assets that use them.
  A script tab shows its text in an editor, the assets it points at (*Uses*) and the
  assets that point at it (*Used by*); click a name to open it. *Save to every copy*
  writes each bundle that holds it. In a bundle tab, a script symbol shows the same
  editor (*Apply* changes that bundle only), and every symbol lists the scripts it uses.
- **Vehicle parts** (once indexing is done): all 153 parts, by category (wheels, weapon,
  propulsion...). A part's tab shows its objparams as named fields (mass, health, shop
  ratings, model, animations...) in an editor; *Save to every copy* writes each bundle
  that holds it. **Save as reNut part...** instead makes your edits a new part of their own
  for reNut, leaving the shipped part as it is (see
  [New parts for reNut](#new-parts-for-renut-lua)). Any `aid_objparams_` symbol in a bundle
  tab shows the same editor.
- **Videos**: a player (Media Foundation decodes the WMV3 video and WMA audio) with
  play/pause (Space or click the picture), seeking, volume and loop, plus export/replace.
- **Font metadata, tile.bmp, asset index**: a text editor (with **Export / Import text**
  as UTF-8 and **Save as UTF-16 character list**), an image view, and a searchable index
  (double-click opens the file; **Export JSON** writes `index --json`).
- **Tools**: *Extract everything* (with progress and cancel; **Textures only** writes
  just the textures as PNG into one folder, and **Only names containing** keeps only what
  is named with some text, such as `ui_`), *Export mod package* (a
  `.nbpatch` of your edits for reNut, see below), *Bulk import textures* and *Remove
  mipmaps from changed textures* (see [Bulk texture jobs](#bulk-texture-jobs); it can also
  compare with a clean copy of the game folder), *Fit menus
  to resized textures* (see [Resized menu textures](#resized-menu-textures)), *Restore original files* (puts each
  file's `.bak` back, undoing everything saved to it; pick which files), *Verify all
  files*, *Script (Lua)* (see "Scripting" below; an **Arguments** field fills the script's
  `arg` table), and an *Asset ID calculator* (one name per line).
  The command-line jobs that have no tab of their own have windows there too; they run the
  same code as the commands, and their output goes to the log (**View > Log**):
  - *Export from a file*: a bundle's, stream bundle's or `.xwb`'s textures (DDS, PNG or
    both, with a stream bundle for the split ones), every model as OBJ with a bone list
    (every draw group, PNG textures), or its wave banks (decoded to WAV, by name).
  - *Export a model (FBX / OBJ)*: one model with every `model-fbx` / `model-obj` option;
    the model list is read from the bundle. *Export shape keys* writes a reNut model's shape
    keys as a `.glb` (`renut-shapes-export`).
  - *Import into a file*: a texture into one bundle (with its stream bundle, other output
    files, XCTD), an OBJ or FBX into one model (`model-import`, with `--rig`), a model swap
    from any file (`model-swap`, with `-o`, `--stream-out` and XCTD), or animation keys into
    one bundle; the name lists are read from the files.
  - *Game scripts and objparams*: export a bundle's scripts, objparams or vehicle parts as
    text files and import a file or folder of edited ones.
  - *Pack folders and convert XCTD*: unpack any bundle or stream bundle (`caff-unpack`,
    `sb-unpack`), rebuild one from an unpacked folder, decompress an XCTD file or wrap one.
  - *Inspect, extract and replace*: `info` and `list` on any file (output in the log),
    `extract` / `replace` of one symbol or entry (`--section .gpu`, `-o`, XCTD), `menu-fix`
    on chosen bundles, and `font-import` with or without a base font.
  - *Run a nuttool command*: any command line, with the commands' usage in a list and the
    output in the window.
- **reNut** menu: *New part from a shipped part* (pick the part; its tab opens **Save as
  reNut part**), *New block from a .glb* (`renut-block`: mesh node picked from the file,
  shape, attachable faces, category, shape key weights and the rest as fields), *New world from a model*
  (`renut-world`, with a character and challenge editor that writes `npcs.json` and a materials list,
  read from the model, that writes `materials.json`; see
  [Worlds](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/worlds.md) and
  [Characters](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/characters.md)), *Vehicle
  blueprint* (`renut-vehicle`, with a parts list editor that writes the vehicle file), *Script
  graphs* (nodes wired together as in a visual scripting editor, each graph made into one of a mod's
  Lua scripts on save: every `renut` and `rex` function and event, flow, maths, variables and Lua of
  your own; see [Script graphs](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/script-graphs.md); `graph-lua` does it
  from the command line), and *Export mod package*.

Edited documents are marked in their tab. **Ctrl+S** saves, writing a plain
(uncompressed) file and keeping a one-time `.bak` of the original; the backup can be
turned off in the File menu. **View > UI size** (or Ctrl+= / Ctrl+-) scales the
interface. Settings live in `%APPDATA%\NutTool\NutTool`.

**File > Title update folder** picks the title update's files (the `4f...` bundles,
`db_index.txt` and `default.xexp` that a title update adds); by default NutTool looks for
an `update` folder beside the assets folder. Its bundles show under **Title update**, are
indexed with the game's (textures, scripts, menus), and are included by *Fit menus to
resized textures*, *Remove mipmaps from changed textures* and *Restore original files*.

Command-line arguments: a folder or file to open, plus `--model <name>` and
`--texture <name>` (full `aid_` names), which open those tabs once indexing finishes.
`--anim <name>` plays that animation in the model tab. `--audio <bank>` opens a wave bank
(for example `--audio DialogJamJars`). `--script <file.lua>` opens the
script window and runs the file. `--export-fbx <out.fbx>` (with `--model`) exports that
model as **Export FBX** would, with its own animations.

## Command-line quick start

```
nuttool info assets\Bundle\4f\01ca49            # what is this file?
nuttool list assets\Bundle\4f\01ca49            # every asset inside it
nuttool index assets                            # every named asset and where it lives
nuttool extract-all assets out --png            # unpack everything
nuttool extract-all assets tex --textures-only  # just the textures, as PNG in tex\textures
nuttool extract-all assets ui --filter ui_      # only what is named with "ui_"
nuttool model-export assets\Bundle\4f\0a04d0 models   # models as OBJ + MTL + bone list
```

`model-export` writes the parts shown by default. `--all-groups` includes every detail
level and alternative part (OBJ objects are named `group<N>_mesh<M>`), and `--png` also
writes the file's textures. It is a quick dump of every model in a file; for one model
with everything the FBX export has, use `model-obj` (below) or the GUI's **Export OBJ**,
which also find textures in shared bundles.

```
nuttool model-obj <bundle> <model> out.obj [--all-groups] [--no-fur] [--no-tint]
```

`model-obj` takes the same mesh, texture and fur options as `model-fbx` and writes the
same meshes, materials, tinted textures and fur, as `.obj` + `.mtl` + PNGs.

Run `nuttool help` for the full command list.

Commands that modify a file write to `-o <path>` if you give one. Otherwise they
overwrite the input after making a one-time `<file>.bak` backup.

## Command-line editing workflows

### Text

```
nuttool text-export assets\loctext\french\11026b27 fr.json
# edit fr.json (UTF-8), change "text" values, or add {"name": "...", "text": "..."}
nuttool text-import assets\loctext\french\11026b27 fr.json
```

Strings are matched by `name`. New names get the key the game computes from the name.
Use `--prune` to delete strings that are missing from the JSON. Markup such as
`{MOODSAD}` is part of the text and should be kept. English tables live in `Debug\11`.

### Textures

```
nuttool tex-export assets\Bundle\4f\01ca49 tex --stream assets\Bundle\50\01ca49 --png
# edit tex\<name>.png, or paint a higher-resolution version (PNG or JPG)
nuttool tex-import assets\Bundle\4f\01ca49 jinjo_body_0x0332d095 tex\<name>.png --stream assets\Bundle\50\01ca49
```

Large textures are split for streaming. The bundle holds mips 1..n (`..._0x<hash>mip`)
and the stream bundle with the same file name holds level 0 (`..._0x<hash>top`).
`--stream` merges the two halves on export. On import it writes level 0 back to the
stream bundle and the remaining mips to the bundle.

- **DDS input** must use the texture's format (the tool tells you which one). Its
  data is used as-is, so a DDS round trip is lossless.
- **PNG, JPG, BMP or TGA input** is compressed to the texture's format by the tool and
  the mip chain is regenerated. JPG has no transparency, so use PNG for textures with
  alpha.
- **A different size resizes the texture.** Import a 1024x1024 image over a 512x512
  texture and it becomes 1024x1024, with one more mip level, so its smallest mips stay
  as small as before. The format stays the same, and the model's UVs need no change.
  - Only 2D textures can be resized (not cube maps, arrays or volumes), up to 8192x8192.
  - A split texture changes size only together with its streamed top level, so pass
    `--stream`. The GUI finds the stream bundle itself.
  - Larger textures use more memory in game. A 2048x2048 DXT1 texture is 2.7 MB with its
    mips, four times a 1024x1024 one. Raising a few character or prop textures is fine;
    raising a whole level's textures may not fit in the Xbox 360's memory.
  - The resized textures match the layout of the game's own textures of that size, but
    larger textures have not been tested in game yet.
  - Menu textures (button icons and such) need their menus fitted too; see
    [Resized menu textures](#resized-menu-textures).

#### Resized menu textures

The menus (XUI scenes in the `aid_xuipackage_*` assets) draw many textures at the
texture's own pixel size inside a fixed box. A button icon resized from 64x64 to 256x256
then shows only its top-left corner, cropped. NutTool fixes the menus to match:

- An image element whose box is the texture's original size is set to stretch the
  texture over its box.
- A shape with a texture fill (the A/B/X/Y buttons are these) gets a fill scale (and,
  for fills not centred on their box, an offset) that covers the box with the whole
  texture again.

So a sharper, larger icon shows at the same size on screen. In the GUI this happens
after any import that changes a texture's size, from a texture tab or **Bulk import
textures**. **Tools > Fit menus to resized textures** runs it by hand, for textures
resized earlier or after restoring files. From the command line:

```
nuttool menu-fix assets                          # finds the bundles with menus (reads them all)
nuttool menu-fix assets assets\Bundle\4f\685374  # only this bundle
nuttool menu-fix assets --update F:\renut2\update  # with the title update somewhere else
```

- **Title update**: TU3 brings menus of its own (`aid_xuipackage_banjoxdlc_tu15`,
  `_skins`, `_mp`, `_ingame` in `aid_bundle_banjoxdlc_titleupdatecommon`, the update's
  `4f3ef5f9`), and a game running TU3 draws the front end and button legends from them.
  Their buttons crop too unless they are fixed. NutTool fixes them along with the rest:
  the title update folder is found beside the assets folder (`<assets>\..\update`, files
  named like `4f3ef5f9`), or given with `--update` (in the GUI: **File > Title update
  folder**). The update's bundles also show in the GUI under **Title update** and are
  indexed with the rest. Mod packages (`make-nbpatch`, *Export mod package*) take the
  update's edited files too, as `update/<file>` entries, which reNut serves as `UPDATE:`
  files.
- Textures are compared with their `.bak` backups, so keep backups on.
- Each menu is rebuilt from its own backup each time, so running it twice changes
  nothing, and a texture put back to its original size gets its original menus back.
- Nine-grids and other elements are not adjusted. The log names any that use a resized
  texture.
- The changes follow the game's own drawing code, but they have not been tested in game yet.

#### Removing mipmaps

Mipmaps are the smaller copies of a texture (half size, quarter size...) that the game
shows on distant surfaces. **Remove mipmaps** in a texture tab (every copy, saved at once)
or a bundle view (saved with Ctrl+S), or from the command line:

```
nuttool tex-remove-mips assets\Bundle\4f\01ca49 particles_spanglewhite
```

- The full-size picture is kept byte for byte; nothing is recompressed.
- A **split texture** keeps level 1 as well. The game streams level 0 into a split
  texture from the stream bundle, and a split texture needs at least two levels, so its
  bundle half keeps level 1 and the stream bundle is not changed. The game never ships a
  split texture with only two levels, so this case has not been tested in game.
- Without mipmaps, distant surfaces can shimmer, and the game reads the full-size
  texture even where it is tiny on screen. Mipmaps cost a third more memory, so removing
  them saves that.
- To put mipmaps back, import an image (PNG, JPG...) of the texture: import always
  builds a fresh mip chain. Or restore the file from its `.bak`.

#### Bulk texture jobs

**Tools > Bulk import textures** imports a whole folder of images at once. Each image
goes into the texture it is named after, as **Export PNG** and *Extract everything* name
them (`aid_texture_banjox_particles_spanglewhite.png`), in every copy of that texture,
the same way **Import image** does (including resizing).

- Only images directly in the folder are read, not subfolders.
- Images identical to their texture are skipped. Export every texture into a folder, edit a
  few, and bulk import the folder: only the edited ones change. Unedited exports are not
  recompressed.
- Images named after no texture are ignored (an upscaler's renamed output, such as
  `name-gigapixel-4x.png`, is among these: rename it back).
- **Array textures** hold several slices, such as the two animation frames of each action
  button (`aid_texture_banjox_ui_buttons_actionbuttons_*`). They export as `<name>_slice0.png`,
  `<name>_slice1.png`..., and bulk import takes those names back, one image per slice, at any
  size as long as all slices match. A slice without its own image repeats slice 0, and so does
  every slice of an image named after the texture itself (its animation then stands still);
  the log says when. Slices must be PNG (or another image), not DDS. Cube faces and volume
  slices cannot be imported.
- Two images for the same texture (a `.png` and a `.jpg`, say) are both skipped, since it
  is not clear which one you meant.
- Each texture is all or nothing across its copies. A texture that fails (a DDS in the
  wrong format, say) is listed in the log, and the others are still imported.
- Changed files are saved with a `.bak` backup, and open tabs are closed first.

**Tools > Remove mipmaps from changed textures** removes the mipmaps (as above) of every
texture you have changed: those that differ from their file's `.bak` backup. The other
textures in those files are left alone. From the command line:

```
nuttool tex-remove-mips-changed assets
nuttool tex-remove-mips-changed assets --original D:\clean\assets   # compare with a clean copy instead
```

### Any asset inside a bundle

```
nuttool caff-unpack assets\Bundle\4f\0a04d0 onion
#   onion\caff.json            header, sections, symbols, relocations, string pools
#   onion\assets\NNNN_*.bin    one file per symbol (the assets)
# edit or replace files in onion\assets (sizes may change)
nuttool caff-pack onion assets\Bundle\4f\0a04d0
```

You can also work on single symbols:
`nuttool extract <file> "#21" out.bin` and `nuttool replace <file> "#21" new.bin`.

Pointers inside an asset are stored *relative to the target symbol* and patched at
load time from the relocation lists in `caff.json` (`relocsA`). Resizing an asset
moves later symbols but never breaks their pointers. If you change the internal
layout of an asset, update its relocation offsets as well.

### Stream bundles

```
nuttool sb-unpack assets\Bundle\50\01ca49 sb      # one .caff / .xwb per entry + streambundle.json
nuttool sb-pack sb assets\Bundle\50\01ca49
```

Each `.caff` entry is a standalone CAFF, so `caff-unpack`, `tex-export`,
`text-export` and the other commands work on it directly.

### Fonts

```
nuttool font-export assets\xuifontcachefont\euro.sbp euro
# edit euro\page_NNN.png (white glyphs, alpha = coverage) and/or metrics in euro\font.json
nuttool font-import euro euro_new.sbp --base assets\xuifontcachefont\euro.sbp
```

`--base` points at the original file so that unchanged glyphs keep their original
compressed data. Copy `euro_new.sbp` over the original when you are done.

The export also writes the font itself, ready to install or load elsewhere:

- `euro\euro.ttf` (named after the folder): a TrueType font with one square per glyph
  pixel (coverage of 8/15 or more). 1 em is the cache's line height, so at that pixel
  size (34 px for the shipped caches) it matches the game pixel for pixel, spacing included.
  The 16-level anti-aliasing is not kept.
- `euro\font.fnt`: a BMFont text descriptor over the `page_NNN.png` files, which keeps
  the anti-aliasing (for engines and tools that read BMFont).

Spacing follows the game: the pen moves right by `metrics[1]` before the cell is drawn and
advances by `metrics[1] + metrics[2] + metrics[3]`. The caches store no baseline, so it is
read off flat-bottomed letters (row 25 of the 48-pixel cell in the shipped caches) and
written to `font.json` as `baseline`. `font-import` ignores the `.ttf` and `.fnt`.

### Game scripts

```
nuttool gamescript-export assets\Bundle\4f\01ede7 na_scripts
# edit na_scripts\aid_script_banjox_nuttyacres_act6_game4.txt (and the others)
nuttool gamescript-import assets\Bundle\4f\01ede7 na_scripts
```

`gamescript-export` writes one `<asset>.txt` per scene script (`aid_script_`), actor
goal list (`aid_actorgoals_`), script table (`aid_scripttable_`) and state table
(`aid_statetable_`) in the bundle; `--name text` keeps those whose name contains the
text. `gamescript-import` takes one file or a folder of them; the file name says which
asset it replaces. Asset ids are written as `@aid_name` when the name is known (from the
bundle and `db_index.txt`; the GUI knows every name in the game).

The text has one record per line, and `#` starts a comment:

```
# a scene script: op_XX (the opcode in hex), then its arguments; op_00 ends it
op_60 "Nutty Acres Act 6 Game 4 Nuts To Go" @aid_challenge_banjox_nuttyacres_act6_game4 1
op_00
# actor goals: the goal, then its arguments
actorMindGoalId_Wait 1.5 3.0
```

Values are ints (`12`, `-3`), floats (`1.5`), hex words (`0x1A2B3C4D`), asset ids
(`@aid_...`), strings (`"text"`, padded to the field's size, or `"text":64` to give the
size) and raw bytes (`<0a0b>`). Commands and goals can be added, removed and reordered.
Each opcode and goal has a fixed argument layout, read off every script in the game;
the tool refuses text whose arguments don't fill it. The game has no names for the
opcodes, so they are shown by number.

### Vehicle parts and other objparams

```
nuttool objparams-export assets\Bundle\4f\685374 parts --parts
# edit parts\aid_objparams_banjox_vehicleblock_wheels_monster.txt
nuttool objparams-import assets\Bundle\4f\685374 parts
```

Every object's parameters (`aid_objparams_`) are one fixed-size struct, so the text lists
fields, one per line, and only values change:

```
name               = "wheelmonster"  # 0x0A0
mass               = 40.0  # 0x190
health             = 40.0  # 0x194
ratingGrip         = 3  # 0x1B4
0x3C8              = 1.5
```

Fields without a name are listed by offset, and words that are 0 are left out; a line such
as `0x3C4 = 1.5` sets one. Lines left out of the file keep their value. `--parts` exports
only the vehicle parts, `--name text` those whose name contains the text.

Named fields of every part (the 960-byte base the game reads at 0x82607A38):

| Field | What it is |
|---|---|
| `blockType` | the part's type number (the game special-cases some) |
| `name`, `description` | the part's name (some are internal, like `wheelmonster`) and a blurb; the shop's text comes from `garageDescription` |
| `garageDescription`, `model`, `havok`, `physics`, `texture` | the assets it uses |
| `paintColour` | default paint (a name from `paint_colours`) |
| `shopSeenFlag` | game flag set once the shop has shown it |
| `mass` | mass, which scales the part's physics shape |
| `health` | hit points |
| `power` | engine and jet power (0 for other parts) |
| `ratingPower` ... `ratingWeight` | the parts shop's 0-5 stars: power, fuel, fuel use, ammo, ammo use, firepower, grip, strength, weight. Display only |
| `anim1`...`anim10` | animations |
| `shopCategory`, `shopSubcategory`, `shopItem`, `partId` | where it sits in the shop, and its id |
| `surface`, `material` | collision surface and material names |
| `flags` | bit flags |

Fuel tanks add `fuelCapacity` and ammo boxes `ammoCapacity`. The other class fields
(engines, wheels, weapons...) are listed by offset with their values.

### New parts for reNut (Lua)

Editing a part changes it for everyone who loads your mod. For reNut you can instead make a **new
part** from a shipped one: it sits next to the original in the garage, already owned, and the
original stays as it is. NutTool writes it as a RexFabric Lua mod for reNut, a script calling
`renut.parts.add` (reNut's [parts and vehicles tutorial](https://github.com/masterspike52/reNut/blob/main/docs/tutorials/parts-and-vehicles.md)
describes it):

```
nuttool renut-part assets\Bundle\4f\685374 wheels_monster mods\mymod --id mymod_bigwheel --name "Big Wheel" --count 4 --fields edited.txt
```

- `<mod folder>` is the mod's folder in reNut's `mods` folder. NutTool writes
  `scripts\<id>.lua` there, and `mod.toml` (the mod's name, `--mod-name`, and `--author`) if the
  folder has none. An existing script is only replaced with `--force`.
- `--fields` takes objparams text as `objparams-export` writes it, edited. Every value that differs
  from the shipped part becomes one of the new part's `fields`. An edited `name` becomes its garage
  name, unless `--name` gives one.
- `--behaviour` makes it **a new kind of part**: the script gets every callback (`on_use`,
  `on_tick`, `on_physics`, `on_throttle`, `on_damage`...), empty, for you to fill in with what the
  part does in Lua: thrust, firing, repairs and so on.
- `--model <file.obj|fbx>` gives it **a model of its own**: the shipped part's model (or
  `--template <model>`'s, e.g. `wheel_standard`) with the file's meshes, as `model-import` applies
  them. Export the template with `model-export ... --all-groups`, edit it, and keep each object's
  name (`group1_mesh8`): a named object replaces that mesh, an object with a vertex and no faces
  removes it, and unnamed meshes stay. Give every level of detail (each group) the new shape. NutTool
  writes the model and a copy of the template's collision into the mod's own stream bundle,
  `<mod folder>\bundles\Bundle\50\<id>` (`aid_streambundle_banjox_<folder name>`, or `--bundle`),
  which reNut loads with the game's common bundles, and the script gets `model = "<id>"`. The
  collision keeps the template's size, so keep the new shape within it, or take another part
  model's with `--collision <model>` (`body_light_cube`, `body_light_wedge`, `body_light_corner`).
  Run it again after changing the file: an existing script is kept (unless `--force`) and only the
  model is rebuilt.

  A binary glTF (`.glb`) works too: `--mesh <node>` names the node whose mesh is taken (with its
  transforms), and `--into 8,0,17,25` the template's meshes that get it; every other mesh is
  removed, and it is bound to the template's root bone. Its normals are smoothed where faces meet at
  under 40 degrees, and its texture coordinates are projected along each face's main axis, or with
  `--flat u,v[,u2,v2]` all set to one spot of the textures (from the image's top left; u2, v2 for
  the second set, the ambient occlusion) for one flat colour. The horn's paintable metal is meshes
  8, 0 and 17 (its detailed metal, whose editable map is the paint mask), with mesh 25 its shadow;
  `--flat 0.098,0.598,0.82,0.055` is fully painted there, without scratches or baked shadow.

`renut-block <mod folder> --id <id> --model file.glb --mesh <node>` makes **a block of its own**
from scratch, with nothing taken from the game: a model of the node's mesh (drawn by reNut's own
part shaders in the part's paint colour, lit by the game's lights), a footprint (`--shape cube`,
`wedge` or `corner` for its collision, `--attachable` for its faces) and a script calling
`renut.parts.add` without a base (`--category body`, `--type`, `--mass`, `--health`, `--count`).
The mesh's shape keys (its glTF morph targets) come along, blended by reNut's shaders:
`scripts/<id>_shapes.lua` lists them with their starting weights (`--shapes name=weight,...`, else the
file's own), and Lua moves one part's with `part:set_shape(name, weight)` (reNut passes the part's
own weights to the draw through its paint constant's spare lane, c28.w).

- `--attachable top` (or `all`, `none`, `bottom`, `sides`, `top+sides`, or directions such as
  `-x,-y,+z`) gives it **a footprint of its own**: the base's cells and attach points, with only
  those faces taking other parts. `top` makes a part that goes under a block and hangs down. It goes
  into the same stream bundle, and the script gets `attach = "<id>"`.

  Two lines of NutTool's own go in the OBJ for jointed parts: `joint BONE x y z` moves a joint of
  the template's skeleton (its bind pose), and `skin BONE` binds the `v` lines after it wholly to
  that bone (`skin` alone ends it). reNut's `part:bone` then turns those bones from Lua. A third,
  `uv2 U V`, gives the `v` lines after it that second texture coordinate (written as `vt` is;
  `uv2` alone ends it).

In the GUI, **Save as reNut part...** in a part's tab does the same with the editor's text, and
can give the part a model of its own (mesh file, `.glb` node picked from the file, into meshes,
flat colour, template, collision) and a footprint of its own (attachable faces). **reNut > New
block from a .glb** and **reNut > Vehicle blueprint** do `renut-block` and `renut-vehicle`.

`renut-shapes-export <model or stream bundle> <out.glb> [--model text]` writes the shape keys of
NutTool's models (worlds, characters, blocks) back out: each draw that has them a mesh with them as
named morph targets (`extras.targetNames`), at the starting weights in the mod's scripts
(`renut.shapes.define`, read from the `scripts` folder beside the bundle's folder). From a stream
bundle it takes every model, or those named with `--model`'s text. Characters come out in their
rest pose, without the skeleton. In the GUI: **Tools > Export a model**, **Export shape keys**.

**A world of its own:** `renut-world <assets dir> <mod folder> --id <id> --model file.glb` makes a
world from every mesh node of the file (`--model a.dae;b.dae`: of several files, in one space, for a
level that comes in rooms), with its materials: base colour times vertex colours, and
the base colour texture at its texture coordinates (`TEXCOORD_n`; images in the `.glb` or files beside
it). A text glTF (`.gltf`) works the same, its buffer a data URI or a `.bin` file beside it, and its
images beside it. A skinned mesh (one with `JOINTS_0` and `WEIGHTS_0` and a skin) is placed by its
joints at their rest pose, as glTF says (its node's own transform is ignored), so a rigged level or
prop comes out as it stands in its bind pose. `--model` can also be a Wavefront `.obj` with its `.mtl`: a material's `map_Kd` (BMP, PNG, JPG or
TGA, beside the OBJ) at the `vt` coordinates, shown in white (an exporter's `Kd` under a texture is a
dim grey), or else its `Kd`. It can also be a COLLADA `.dae`: each geometry its visual scene places
(with the nodes' transforms; a Z-up file is turned Y-up), its `<triangles>`, `<polylist>` or
`<polygons>` with their material's diffuse texture (or colour) at the lowest `TEXCOORD` set, times their
`COLOR` (a mesh whose colours are out of range is drawn without them). `--colour-scale s` multiplies
every vertex colour (2 for 3DS rips, whose colours are stored at half). Vertices are split where their
texture coordinates differ, normals are smoothed by angle (`--smooth`), textures repeat, and texels
under half alpha are cut out. It is scaled by `--scale` and stands on a flat square ground `--ground`
units wide (`--ground-colour rrggbb`) when given:

- its model, drawn by reNut's world shaders: one draw per texture (each at most 65535 vertices, more
  draws when it needs them), binding its texture in the GPU's texture fetch constant 0. Its vertices
  are 32 bytes: position, normal, colour and texture coordinate;
- its textures, `aid_texture_renutworld_<id>_<n>`: each image at power-of-two sides, at most
  `--max-texture` pixels a side (256, 512, 1024 or 2048; default 1024; bigger images are shrunk),
  DXT1 (DXT5 when it has alpha) with every mip level, whole (not split for streaming), made from a
  car park texture's header. Materials without a texture get a small white one, so their colour shows.
  A world with hundreds of textures needs `--max-texture 512` (or less) to fit in the game's memory;
- its collision (the same triangles; reNut builds the collision's MOPP as the world loads);
- **a big world in chunks:** a Havok collision mesh indexes at most 65535 places (16-bit indices),
  so a world with more is cut into chunks of neighbouring triangles (in Morton order of their middles
  across x and z), each with at most 65000 places. Each chunk is a model and a collision of its own: the
  first keeps the world's names (`aid_model_renutworld_<id>`, `aid_havok_renutworld_<id>`), the others
  get `_<n>` after them, and the scene places each with an `op_01` line. The log lists each chunk's
  vertices, triangles, draws and places;
- a marker with the player's start (`--spawn x,y,z`, else 2 m above the flat ground nearest the
  model's middle, found by casting down onto its triangles; `--spawn-yaw`) and a door home to Showdown
  Town (`--exit x,y,z`, else on flat ground 10-15 m from the start, facing it; `--exit-yaw`; `--no-exit` for
  none, for a world a mod leaves from Lua with `renut.worlds.go`);
- its scene, in the mod's stream bundle (`--bundle`), and the world itself (textures, model,
  collision and marker) in `<mod folder>/worlds/Bundle/50`, which the game loads when the player goes
  through the door;
- `scripts/renutworld_<id>.lua`, declaring it (`renut.worlds.add`) and its door in Showdown Town
  (`--door x,y,z`, `--door-yaw`; by default beside Mumbo's Motors' door; `--no-door` for none, for a
  world a mod reaches from another with `renut.worlds.go`). `--door-style` picks its
  kind: `wall` (default: the world door, one-sided, for against a wall), `arch` (the stone arch home,
  two-sided: model, collision and objparams copied from the car park's bundle `4f\20abf9` as
  `aid_*_renutworld_<id>_arch`, with its two hologram textures, which Showdown Town lacks, copied as
  `aid_texture_renutworld_<id>_archhologram...`; the script gets `style = "arch"` and that `look`),
  `palace` (L.O.G.'s palace door, `banjox_portal_hornportal`) or `hidden` (no model:
  `banjox_portal_mumbosmotors`, a trigger for a doorway already there);
- with `--music song.ogg` (or `.mp3`, `.flac`, `.wav`), its music: the file is copied to
  `<mod folder>/music/` and the script gets `music = "music/<file>"` and `music_volume`
  (`--music-volume`, 0 to 1, default 0.8). reNut loops it while the player is in the world, pausing
  the level's own music under it; it pauses with the game and while a challenge's song or a Stereo's
  game song or file plays (reNut's [worlds and doors tutorial](https://github.com/masterspike52/reNut/blob/main/docs/tutorials/worlds-and-doors.md)).
  An existing script is kept unless
  `--force`, so a changed `--music` needs `--force` (or an edit to the script);
- with `--player-model <id>`, `player_model = "<id>"` in the script: reNut makes Banjo with that
  player model (`renut-player-model`, [below](#player-models)) in the world;
- objects named with `_water` (a glTF node or mesh, an OBJ object or group, a .dae node), and
  triangles whose object, material or texture name holds a `--water name,...` part, become the game's
  water instead of model and collision, drawn from those very triangles as the game's own levels'
  water is: a water region (chunk 0x26 of every chunk's model, which the scene's water setup reads from
  its background as it starts, `sub_82385678`), so it has the water's exact shape, every level of it in
  one (no limit on levels) and steep pieces (falls) too. For its physics, each triangle by its own
  slope: one sloping more than 60 degrees falls (water to look at, not to swim in); the rest, banded by
  height into levels (within 0.4 m of a level's lowest, at their area-weighted height), are water to swim
  in, filed by the cells of a 16 x 16 grid over the water they reach (z-major). The scene has one `op_7D`: the car park's `testtrack_watersetup_calmwater`
  arguments with argument 66, the land mask, replaced by `aid_texture_renutworld_<id>_watermask`
  (256-1024 a side, texels about 0.5 m, black where the water is and a texel past it; its smaller levels
  keep the darkest texels);
- `--stats jiggies=N,trophies=N,jinjos=N,notes=N` sets the totals the door's stats panel shows (the
  script's `stats`); left out, reNut counts them from the world's challenges;
- a `.glb`/`.gltf`'s shape keys (morph targets, deltas through the skin or node transform, scaled
  as the world) come along: each draw carries its shapes' deltas after its vertices
  ([Custom models](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/custom-models.md)),
  and `scripts/renutworld_<id>_shapes.lua` (written anew each time) names
  them with their starting weights (`--shapes name=weight,...`, else the file's own), which Lua moves
  with `renut.worlds.set_shape`. They move what's drawn, not the collision;
- each material keeps its look ([Worlds](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/worlds.md)): alpha (`opaque`, `cutout`, `blend`, `add`), cutoff,
  opacity, emissive colour, strength and texture, glow (the colour target's alpha, which the game's bloom
  reads), double-sided and unlit, from the file (glTF `alphaMode`, `alphaCutoff`, `doubleSided`, `emissiveFactor`,
  `emissiveTexture`, `KHR_materials_emissive_strength`, `KHR_materials_unlit`; a .dae's emission,
  transparency and double-sided flag; an .mtl's `Ke`, `map_Ke`, `d`), with `--materials materials.json`'s
  settings over them by name (`renut-materials model out.json` writes a model's to edit). Draws are split by
  texture and material; each carries a material record after its vertices, see-through ones (`blend`, `add`)
  go in the game's translucent bucket without a shadow, and `scripts/renutworld_<id>_materials.lua` names them
  for `renut.worlds.set_material` (emissive, strength, glow, opacity while played);
- with `--hologram image.png` (or `.jpg`) on an arch, the arch's hologram shows the image; on a wall
  door, a door of its own: a copy of the game's world door
  (`aid_model_renutworld_<id>_door` and its collision, from Showdown Town's common bundle `4f\234cec`)
  whose hologram shows the image, cropped to a square: its two hologram atlases (colour and alpha
  mask, split for streaming like the game's) are rebuilt as `aid_texture_renutworld_<id>_hologram...`
  with the image in every cell, and `aid_objparams_renutworld_<id>_door` names the copied model. They
  go in the mod's stream bundle, and the script's `renut.doors.add` gets `look = "renutworld_<id>_door"`;
- with `--sky`, a sky: `aid_model_renutworld_<id>_sky`, a sphere of 48 x 24 faces (both ways round)
  drawn by reNut's sky shaders, which put each vertex at infinity in its direction (just inside the
  far plane, so everything draws in front of it) and leave it unlit. Its texture,
  `aid_texture_renutworld_<id>_sky`, wraps once around it from straight up (its top) to straight down:
  `--sky panorama.png` (or `.jpg`) takes a 360-degree equirectangular image, `--sky rrggbb,rrggbb`
  makes a gradient from the first colour overhead to the second at the horizon. The scene shows it
  with `op_2C <model> 1 0 0 0 0`, as Spiral Mountain's sets its skydome: the game's sky slot
  (`0x8239FBB0`), drawn at the camera each frame (`0x8239FD90`).

The world borrows the car park's bundle for its lights, its door home and the vehicles' parts. In the
GUI, **reNut > New world from a model** does the same.

**Characters of its own:** `--npcs npcs.json` (in the GUI, the world window's *Characters* editor, or
*From a JSON file*; see [Characters](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/characters.md)) adds NPCs who
stand in the world with a looping idle, talk, and give a "reach the gate" challenge. Model, idle
and sound paths are read beside the JSON file:

```json
{
  "npcs": [{
    "id": "treasuregal",
    "name": "Treasure Chest Shop Gal",
    "model": "treasure_girl.dae", "scale": 0.0374, "smooth": 40,
    "position": [24, 0, 88], "yaw": 0,
    "rig": "auto", "idle": "breathe",
    "tag": "renut_hyrulefield_treasuregal",
    "lines": {"greeting": "...", "objectives": "...", "win": "...", "lose": "...", "completed": "..."},
    "voice": ["sounds/hey.ogg", "sounds/hmm.ogg"], "voiceVolume": 0.9,
    "sounds": {"win": "sounds/cheer.ogg"},
    "challenge": {
      "id": "treasuregal_race", "name": "Treasure Dash", "summary": "...",
      "goal": [-104.7, 6.5, 347.6], "goalYaw": -31.5, "gate": [16, 26],
      "timeLimit": 60, "tt": 25, "jiggy": 40, "notes": 1
    }
  }]
}
```

- `id` (a-z, 0-9 and `_`, unique in the world) names its assets `..._renutnpc_<world>_<id>`; `name` is
  who it is. `model` is a `.dae`, `.glb` or `.obj` (y up after loading, as `--model`), times `scale`,
  stood with its feet on its origin; `colourScale` (default 1) multiplies its vertex colours.
- `position` is where its feet go (the game drops it onto the ground), `yaw` in degrees (0 faces +z).
- `rig: "auto"` gives it a skeleton made from its shape (15 bones: `BASE`, `BACKA` hips, `BACKB` chest,
  `NECK1`, `HEAD`, and per side `LF_`/`RT_` `H` `K` `F` hip, knee, ankle and `S` `E` shoulder, elbow;
  elbows halfway out along a T-pose's arms, else halfway down) and skins every vertex to it (below the
  hips to each side's leg chain); `idle` is a built-in idle (`"breathe"`) or a JSON timeline file,
  looping while it stands. The built-in `walk` swings the legs (knees bending as each comes through)
  and the arms against them.
- `tag` is its objTag, a name of your own (reNut maps it onto one the game knows); `speaker` (default
  32) is the dialog speaker slot (`dbCharData_Id`) its dialogs name, which reNut gives its chardata.
- `lines` are its words: `greeting` is needed; with a challenge, all five. `{PAGEBREAK}` starts a new
  page and `{MOODHAPPY}`, `{MOODSAD}`, `{MOODANGRY}` set a mood, as in the game's own text.
- `voice` (optional) is what it sounds like as each page of a line comes up, as the game's characters
  babble: a game character's name (`"humba"`; also `banjo`, `kazooie`, `mumbo`, `grunty`, `klungo`,
  `log`, `bottles`, `jinjo`, `pikelet`, `pigpig`, `jolly`, `rhino`, `piddles`, `blubber`, `ghostpig`,
  `penguin`, `boggy`, `mrfit`, `kingjingaling`, `thomas`), whose voice cues are copied into its chardata,
  or a list of sound files (`.ogg`, `.mp3`, `.flac`, `.wav`), one played at random per page by reNut.
  Without it the character is silent. `voiceVolume` (0 to 1, default 0.9) is how loud its files play.
- `sounds` (optional) gives a line a sound file of its own (`greeting`, `objectives`, `win`, `lose`,
  `completed`), played once as the line starts instead of the voice on its first page. Sound files are
  copied to `<mod folder>/sounds/<id>/` (two with the same file name are refused).
- `challenge` (optional): drive through the gate at `goal` (its middle, on the ground; `goalYaw`
  degrees, `gate` width and height) within `timeLimit` seconds for notes (10 times `notes`), within
  `jiggy` for a jiggy, within `tt` for a T.T. trophy. The jiggy counts towards the world itself. The
  clock counts up (`"countDown": true` counts down from `timeLimit` instead, failing at 0). The player's
  own vehicle starts it at `start` (`startYaw` degrees), by default 12 m from the character towards
  the goal and facing it: a kind-21 marker record with the challenge's mask, which the game spawns the
  player's vehicle at (`0x824FCFD0`); without one the player would start on foot.
- `exclude` (optional): parts of object, material or texture names to leave out of the model (a rip's
  shadow sheet below the feet); `armsDown` (degrees, 0-90): both shoulders lowered by that much in
  every animation, for a T-posed model (about 70).
- `enemy` (optional): `{"health": 3, "behaviour": "mirror"}` makes it an enemy: no lines needed, no
  challenge, no talk block; its Lua gets `enemy = {health = n}`. `"mirror"` also writes
  `scripts/renutnpc_<world>_<id>_ai.lua` (kept unless `--force`), a `renut.npcs.on` script copying
  Banjo as his mirror image; `"none"` leaves its behaviour to the modder's Lua.
- `shapes` (optional): `{"blink": 0, "smile": 0.5}`, starting weights for the model's shape keys (a
  `.glb`'s morph targets, which come along), over the file's own. Its Lua gets a
  `renut.shapes.define{model = "renutnpc_<world>_<id>", ...}`, and `npc:set_shape(name, weight)` moves
  them in game.

Every NPC also gets `aid_anim_..._walk` (the built-in walk, a stride a second) as `walk` and `run` in its
animtable, for Lua moving it. Each NPC gets, in the world's bundles: `aid_model_renutnpc_<world>_<id>` (a skinned model drawn by
reNut's skinned shaders) and its textures (`aid_texture_..._<n>`, in the world's resident bundle),
`aid_anim_..._idle` and `aid_animtable_...`; its actor `aid_objparams_...` (Pikelet's layout, from
Showdown Town's common bundle `4f\234cec`), body `_body`, stand and idle states `_stand` / `_idle`, state
table `aid_statetable_...` (the game's common dialog, hit and other states besides), and
`aid_chardata_...` (Humba's layout and dialog camera; its voice cues at +0x90, 4 moods of short,
medium and long, are the `voice` character's, or none); a one-line `aid_dialog_..._<line>` per line,
whose text key is `dialog__renut_<world>_<id>_<line>`; and its talk script `aid_script_..._talk`. A
challenge is `aid_challenge_renutchallenge_<world>_<id>`, Park Prix 1's (`4f\685374`) with its own
header: its dialogs, flags, jiggy counter `renut_<world>_jiggies`, times and rewards. The marker gets a
record for the NPC (kind 6, snapped to the ground) and one for its goal gate (kind 22, in the
challenge's marker mask), and the scene registers the giver and what talking to it runs:

```
op_6F "<tag>" "renut_<world>_<challenge>_beaten" @aid_challenge_renutchallenge_<world>_<id> 0 0 0
op_71
op_58 "<tag>" 0 0 0
op_5A @aid_script_renutnpc_<world>_<id>_talk 1
op_1B
```

The talk script starts the challenge (`op_60`), which says the greeting (or, once beaten, the completed
line), the objectives, and afterwards the win or lose line; without a challenge it says the greeting.
`scripts/renutnpc_<world>_<id>.lua` (written anew each time: edit the JSON) calls `renut.npcs.add`
with the NPC's tag, scene (`renutworld_<world>`), chardata, speaker slot, every text (dialog lines, the challenge's title and summary,
the world's name), the challenge's flags (`..._seen`, `_notes`, `_beaten`, `_tt`) and jiggy counter, and its
`voice`, `voice_volume` and `sounds` (reNut loads a game character's dialog wavebank in the world, and
plays sound files itself as each page starts: reNut's
[characters and challenges tutorial](https://github.com/masterspike52/reNut/blob/main/docs/tutorials/characters-and-challenges.md)).

How a world draw binds its texture (the game's own models do the same): the rendergraph's +0x1C
counts its textures and +0x7C points at a word per texture, which the game fills as the model loads by
looking up the property dictionary's `texturefileN` (a texture asset's name; a name ending in `mip` or
`top` is a split texture's half). Draw field 6 is a sampler record `{count, ->program, samplers}`, each
sampler `{fetch constant, u16 N+1 (a split texture's top; N for a whole one), u16 N, clamp u, clamp v}`; at each draw the
game copies texture N's fetch constant into that fetch constant with those clamp modes (0 repeats).

**A vehicle for the mod:**

```
nuttool renut-vehicle assets\Bundle\4f\685374 mods\mymod\vehicles\mech.json mods\mymod
```

writes `mods\mymod\vehicles\mech.blueprint`, which reNut adds to the player's saved vehicles once.
The vehicle file names it and lists its parts: `{"name": "MECH", "parts": [{"part":
"body_light_cube", "cell": [3, 2, 4], "rotation": [0, 0, 180], "colour": "4e7bc7", "button": "X"},
...]}`. Cells are garage grid cells (y up), rotations are degrees about x, y and z, and a part
made with `renut-part` also names its shipped `"base"`, whose record gives its block type and mass.

[Parts and vehicles](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/parts-and-vehicles.md)
in NutTool's docs goes through parts, blocks and vehicles made this way, and reNut's
[parts and vehicles tutorial](https://github.com/masterspike52/reNut/blob/main/docs/tutorials/parts-and-vehicles.md)
their Lua.

#### Player models

```
nuttool renut-player-model assets mods\mymod Linkjo.fbx --id linkjo [--template actor_banjotuxedo]
```

makes a player model reNut makes Banjo with (`renut.player.set_model("linkjo")`, or
`player_model = "linkjo"` in `renut.worlds.add`; `renut-world --player-model linkjo` writes that).
It's a copy of Banjo's model from the common bundle (`4f\685374`): `--template` picks
`actor_banjo` or `actor_banjotuxedo`, and by default it's the one a `model-fbx` export of it names
(its root object). The file is an FBX, or a `.glb` Blender exported from one (its skinned mesh
nodes at rest, their joints by name). Its meshes are applied as `model-import` does them (objects
named like the export's, two naming one mesh, as Blender's `.001` copies, joined); the meshes the
model draws by default that the file left out (deleted in Blender) are removed, its hidden
alternatives kept. An object that needs a draw of its own (a second object naming a mesh, as
Blender's `.001` copy of an added part; a plain-coloured object over a textured draw whose colour
can't be set) takes one of those left-out meshes in its group, untextured, and is drawn as it.
An export holds the top detail level only, so that's what's drawn at every distance: each lower
level's groups draw the top level's (they pair in order). The model goes in the
mod's bundle as `aid_model_renut_player_<id>`, its textures Banjo's own. The skeleton stays, so
his animations work. A mesh whose material in the file is a plain colour (no texture) that differs
from the game's is drawn in it, as the export writes colours (the game's own values): an untextured
draw's `c:basecolour`, or a textured draw's colour map gain set to 0 and offset to the colour. When
other draws share them, the draw first gets its own copy of its material record and constants. A draw has one colour, so of two
objects joined into a mesh the first one's is used.

A `.glb` object whose material has a base colour texture of its own (an image the file holds, not the one the export wrote:
those are named after the game texture they show, and are left as the game's) takes it too: the texture its
draw shows is copied as `aid_texture_renut_player_<id>_<n>` (a split texture's mip half and streamed top level, from the
common bundle and `50\685374`) with the image in it, at most 1024 a side (at the texture's own size when its layout can't
change size), into the mod's bundle; the model's name for that texture points at the copy, and the draw's colour map gain
and offset are set to 1 and 0 so the image shows as it is. Every draw of the model that uses that texture shows the image,
so name the object after a mesh whose texture only it uses (a replaced wrench part, say: the wrench's meshes share one
texture, so delete the rest of them). An object rebuilt into another mesh keeps that mesh's other maps (normal, specular).

#### Character packs

```
nuttool renut-characters assets <reNut folder>\mods [--only npc_cpu_klungo,npc_humba]
```

makes the packs reNut's `renut.actors.spawn` loads to spawn a game character in any world. For each
character (an objparams with a model in an actor bundle, `aid_objparams_banjox_actor_<name>`) it
follows what the character uses: the ids in its objparams and the objparams they lead to, `aid_`
names in their strings, and its model's textures. What neither its actor bundle, its group's shared
one (`actor_npccommon` and `actor_townsfolkcommon`, `actor_gruntbotcommon`) nor `banjox_common`
holds is copied from the smallest bundle that has it (a split texture's streamed top level with it)
into `mods\.characters\Bundle\50\<id of renutchar_<name>>`. Without a pack, reNut loads the
character's whole world bundles (tens of MB), which a heavy world has no room for. The first run
reads every bundle of the game, which takes a while.

### Audio and video

`nuttool wave-export assets\Bundle\50\685374 waves` writes each wave bank as `.xwb`,
plus every wave as stored: RIFF XMA2 (`.xma`) or PCM `.wav`. Add `--wav` to decode
every wave to 16-bit WAV that any player opens, and `--name Music` to export only the
banks whose names contain that text. All 420 banks are in `Bundle\50\685374`.
XMV files are ordinary WMV: `nuttool xmv-info <file> copy.wmv`.

### Models

```
nuttool model-export assets\Bundle\4f\685374 models          # then edit the OBJ in Blender
nuttool model-import assets\Bundle\4f\685374 banjox_actor_banjotuxedo models\edited.obj   # or edited.fbx
nuttool model-swap assets\Bundle\4f\685374 banjox_actor_banjotuxedo assets\Bundle\4f\685374 banjox_actor_banjo
nuttool model-fbx assets\Bundle\4f\685374 banjox_actor_banjo banjo.fbx   # for Blender, Maya, Unity...
```

`model-fbx` writes a binary FBX 7.4 file:

- **Skeleton**: the bones as a joint hierarchy under a null named after the model, posed
  as the bind pose.
- **Meshes**: the meshes shown by default (`--all-groups` for every detail level and
  switch state), each named `group<G>_mesh<N>` like the OBJ export. Each has a skin with
  the original bone weights, normals, and one or two UV sets.
- **Materials**: diffuse colour and specular, with the colour map embedded as PNG. A
  material's colour gain and offset (`c:textureGain<slot>`, `c:textureOffset<slot>`, which
  the shaders apply as texture × gain + offset) are baked into a copy of its texture
  (`<texture>_tint<N>`), so parts that share one plain texture keep their own colours.
  `--no-tint` (or **Material tint** off in the viewer) keeps the textures as stored.
  Textures come from the bundle and its paired stream bundle (`Bundle/50/<same name>`),
  so split textures are full size. `--no-textures` leaves them out.
- **Animations**: the ones named after the model (`aid_anim_<model>_*`), one take each.
  They come from the same bundle, or from another with `--anims <bundle>`; `--all-anims`
  takes every animation there and `--no-anims` none. They are sampled at 30 frames a
  second the way the viewer plays them. Keys that linear interpolation reproduces are
  dropped, and so are channels that stay at the bone's rest transform. Rotations are XYZ
  Euler angles.
- **Fur**: meshes that draw fur (Banjo's, for one) get one extra mesh per layer of the fur
  volume, `group<G>_mesh<N>_fur<K>`. Each is pushed out along the surface, skinned like its
  mesh, and has a baked PNG layer embedded in the file: the fur colour, blended from root
  to tip, with alpha where that layer has strands. Any program shows it as an ordinary
  alpha-tested material. `--fur-length` (percent of the model's radius, default 1.5),
  `--fur-tiling` (default 6) and `--fur-size` (baked texture size, default 512) match the
  viewer's sliders. `--no-fur` leaves the fur out. The game draws fur itself, so
  `model-import` ignores these shell meshes.

The GUI's **Export FBX** finds animations and textures anywhere in the game through its
index, so it can include more than the command line, which sees only the bundles you
give it. Units are metres (the file says 1 unit = 100 cm), Y up. The file is laid out
as Blender's own exporter writes FBX. Bones keep the game's frames, which all face the same
way; `--reorient-bones` (**Reorient bones** in the GUI) turns each one so `--bone-axis`
(default Y, Blender's bone axis; X, Z, -X, -Y or -Z for other programs) points at its main
child: the one with the most bones under it, or the average of tied ones. Leaf bones carry
on along the line from their parent, and each turn is the smallest from the game's frame.
The skin, bind pose and animation curves are rewritten to match, so the model and its
motion are unchanged. Without it, Blender's *Automatic Bone Orientation* does a rougher
version. Blender sizes each bone by the average distance to its children (childless ones copy
their parent's). `--bone-ends` (**Short end bones**) gives each childless bone a
`<bone>_end` child, `--bone-end-size` percent of the skeleton's size (default 1.5) down its
bone axis, so it draws as a stub; Blender's *Ignore Leaf Bones* hides the helpers but keeps
the stub length. They carry no weights, and `model-import` ignores them.
[`tidy_bones.py`](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/blender/tidy_bones.py)
(in NutTool's docs), run in Blender with
the armature selected, sets each bone to reach its main child and childless ones to short
stubs, without moving anything.

`model-import` takes a Wavefront OBJ or a binary FBX (7.x; text FBX files are refused,
so export binary). It matches objects to meshes by the `mesh<N>` in their names, as the
exports write them (`group0_mesh13`), so keep those names. FBX files are read in the
game's space: node transforms, the file's axes and its unit scale are applied. Where an
FBX mesh has a skin, its weights on bones named like the model's (exactly, or ignoring
case and underscores) are written into the vertices: the four largest per vertex. Weights
on bones the skeleton lacks are dropped and listed. For each object:

- **Same vertex and triangle count** (moved, scaled or reshaped vertices, new UVs or
  normals): the mesh is edited in place. Nothing else changes, and an unedited export
  imports back byte for byte. In Blender, import with *Split by Object*, keep the vertex
  order (do not merge or delete vertices), and export with *Triangulated Mesh* off or on.
- **Any other geometry**: the mesh gets new vertex and index buffers, appended to the
  model's `.gpu` part. The draw keeps its material and textures. Each new vertex takes
  its tangent and extra texture coordinates from the nearest original vertex of that
  mesh, and its bone weights too unless the FBX gives them, so a replacement that sits
  where the old part was follows the same bones.
  A vertex buffer holds at most 65,535 vertices (counting the old ones other draws use).
- **An object with no faces** (just a vertex) removes the mesh: its draw is taken out of the
  model, so the model's later meshes number one lower when it is exported again.

Meshes not named in the file stay as they are. New parts cannot be added; replace an
existing mesh instead. Other detail levels draw their own meshes, so either edit those too
or set the detail level to 0 in game.

`model-import --rig` (**Override rig** in the GUI) also overrides the skeleton's joint
positions from an FBX. The game skins with the inverse of the model's bind pose (its
`pose` block, so a skin matrix is the identity in the bind pose) and adds each animation
translation to the bone's bind offset. Moving a joint therefore moves where animations
bend the mesh, without moving the mesh itself.
- Each model bone named like an FBX bone (as for weights) takes that bone's bind position:
  its skin cluster's `TransformLink`, else the `BindPose`, else where the node stands.
  Bones the file lacks keep their offset from their parent.
- Bone orientations, the hierarchy and the bone count stay, because the game's animations
  replace rotations and are made for its skeleton. Extra bones (other than `_end` helpers)
  are ignored and listed.
- The file may hold only the armature; meshes in it are imported as usual.

`model-swap` copies a whole model (its `.data`, `.gpu` and `.stream` parts) over another
one, from the same bundle or a different one; shared strings and shaders the destination
lacks are added to its pools. The game then loads the copy under the original's name.
When a model loads, the game binds every texture it names (`0x822D8078`), and one whose
bundle is not loaded crashes it. So textures the new model needs that the destination
bundle lacks (and the original model did not use) are copied in too: the texture's two
symbols into the bundle and, for split textures, the streamed top level into the paired
stream bundle (`Bundle/50/<same name>`), which is saved as well. The CLI takes them from the
source model's bundle and its stream bundle; with `-o`, give `--stream-out` for the
stream bundle. The GUI finds them through the asset index. New symbols go in front of the bundle's `manifest` (its last symbol, which
lists every asset; the game reads it from the end) and get manifest entries.

The game animates a model by bone index with the skeleton its animations were made for
(`0x822109B0` blends pose buffers sized from it), so a model with another skeleton is
retargeted onto the original's: its pose pointer leads to the original's pose (kept in a
`<model>_skeleton` symbol), the dictionary's bone entries are the original's, and every
vertex is re-bound to the original's bones by name (else its nearest matching parent).
Vertices are not moved: at rest the model keeps its exact shape, and animated, its joints
turn about the original's joint positions (for fat Banjo on the tuxedo, within 0.04).

The new model keeps its own parts. When the original had switched parts it lacks (the
player models' Kazooie part and wrench), the swap warns: in game, Kazooie then stays in
her bind pose and there is no wrench (so no attack hitbox). Swap between models with the
same parts (Banjo's outfits) to keep those.

**[Custom models](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/custom-models.md)**
in NutTool's docs walks through a swap step by step, and its
**[troubleshooting page](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/troubleshooting.md)** says what to do
when a swapped character crashes the game, T-poses or looks distorted.

### Animations

```
nuttool anim-import assets\Bundle\4f\685374 banjox_actor_banjo_stand banjo-macarena.json
```

`anim-import` gives an existing animation new motion from a JSON timeline of poses: bone
rotations in Euler degrees and offsets in metres, eased between keys. Bones without keys
keep the animation's own motion. The animation's length, frame count, pose and facial
channels stay, so the game plays the new motion wherever it played the old one.
**[Animations](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/animations.md)** in
NutTool's docs describes the format; the example,
[`banjo-macarena.json`](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/examples/banjo-macarena.json),
replaces Banjo's idle with the Macarena.

The rotation track (`QUAT_BITSTREAM`, decoded at `0x8220A6A8` / `0x829FBF00`) is
re-encoded: per bone a mask byte per channel triple (present / constant bits), 16-bit
bases and nibble widths, then one bit-packed record per stored frame, starting at
`data + bases + widths` with a word-aligned stride. The new tables are appended to the
animation's symbol and the track's pointers moved to them.

How the game reads this: at load it builds each vertex and index buffer from the
rendergraph's resource records (`0x82BC32D0`): vertex buffers `{->header, ->.gpu offset,
size}` and index buffers `{->header, ->.gpu offset, bytes, format}`; a draw's index count
is read when it draws. CAFF string pools are not read at load (`0x82A026B0` only places
symbols, swaps and relocates).

### Scripting (Lua)

Scripts are Lua 5.5 with a `nut` table over the asset code: open bundles and stream
bundles, read and replace symbols, inspect models, recolour materials, import OBJs, read
wave banks, and run any `nuttool` command. Run them in the GUI (**Tools > Script**:
an editor with Run (Ctrl+Enter), Stop and the output) or from the command line:

```
nuttool script recolour.lua --assets F:\game\assets [arguments for the script...]
```

```lua
local f = nut.open("Bundle/4f/685374")          -- relative paths start in the assets folder
for _, name in ipairs(f:models()) do
  local m = f:model(name)
  print(name, m.meshes, m.vertices, #m.bones)
end
f:set_colour("banjox_actor_mumbo", "blinn11", "velvetColour", 0.8, 0.1, 0.1)
f:save()                                         -- in place, with a one-time .bak
```

The Lua script window starts with an example of the basics, and
**[the command line](https://github.com/masterspike52/reNut/blob/main/docs/nuttool/cli.md)** page of
NutTool's docs has more.

### Mod packages for reNut (.nbpatch)

Once you have edited files, NutTool can package them as a mod in NB Studio's `.nbpatch`
format. reNut loads these at startup from its `mods` folder. It rebuilds each changed
file into `mods\.cache` and serves it in place of the original, so the game folder is
never written.

```
# files you saved in NutTool with backups on (each changed file has a <file>.bak)
nuttool make-nbpatch assets mymod.nbpatch --name "My mod" --author me --version 1.0
# or every file that differs from an untouched copy of the game folder
nuttool make-nbpatch assets mymod.nbpatch --name "My mod" --original D:\clean\game
```

In the GUI, use **Tools > Export mod package (.nbpatch)...**: name the mod, find the
changed files, untick any you want to leave out, and build.

Edited title update files (the TU3 menus that `menu-fix` adapts, say) go in as
`update/<file>` (`update/4f3ef5f9`), which reNut serves in place of its `UPDATE:` files.
The title update folder is the one in **File > Title update folder**, or `--update`;
with `--original`, the clean copy's own `update` folder (beside it) is compared, else the
update's `.bak` backups.

How files are stored:

- A changed game file becomes a small binary delta (NBDL) against the original. NBDL is
  built against the original's expanded form (XCTD decompressed), so a file NutTool saved
  uncompressed costs only the bytes you actually changed.
- A file the game does not have is stored whole.
- Each entry records the SHA-256 of the original and of the result. reNut skips a mod
  whose originals differ from the player's files.
- `default.xex` changes are left out, because reNut runs recompiled code.
- Two mods that change the same file cannot both load in reNut; the later one is skipped.

## Format reference

These were reversed from `default.xex` (TU3) and confirmed against every file in the
game. All values are big-endian unless noted.

**Asset ids.** An asset is named `aid_<type>_<rest>`. Its id is
`(typeIndex << 24) | (~crc32(rest) & 0xFFFFFF)`, where `crc32` is the standard
reflected CRC-32. The id gives the file path: `Bundle\<tt>\<xxxxxx>` for types 0x4F/0x50,
otherwise `Debug\<tt>\<xx>\<xx>\<xx>`. The 88 type names and versions come from the table
at `0x820CDAD8` (`assetdb.cpp`).

**XCTD** (bundles). The XDK's transparent decompression format. File header:
`0x0FF512ED, 0x01000000, hash, flags`:
- flags bits 4–5: block size = `0x8000 << n`
- flags bits 6–21: block count
- flags bits 22–23: size-table width (20 or 32 bits)

The header is followed by a bit-packed table of *uncompressed* block sizes. The file is
cut into 128 KB pieces, counting the header as part of the first. Each piece is an
independent XMemCompress LZX stream: frames prefixed by `u16 size`, or by
`FF u16 usize u16 csize`. A piece starting with `u16 0` is stored raw. Files without
the magic are read as-is, which is why repacked bundles are written as plain CAFF.

**CAFF** (`07.08.06.0036`):
- **Header** (0x78 bytes): counts, an ELF-style checksum at +0x18 (computed with the
  field zeroed, sign-extended bytes), and two block descriptors.
- **Info block**: 33-byte section records, section names, symbol names, and 14-byte
  symbols `{nameIndex, offset, size, section, kind}`.
- **Fixup block**: endian-swap tables, two relocation sets `{from, to, count}` plus
  offsets, and string-pool tables.
- **Sections** follow back to back.

Bundles use `.data`, `.gpu`, `.gpucached`, `.texturegpu` and `.stream`.

**Stream bundle**: `f32 281.41, 12, count, timestamp, depCount, deps[], {id, offset, size}[]`
followed by the entry data.

**LSBL** (little-endian, inside a loctext CAFF):
- **strings**: `{u16 key, u32 charOffset}` sorted by key with a `0xFFFF` sentinel, then
  UTF-16 text plus one extra terminator
- **names**: `{key, nameIndex, nameOffset}` plus strcmp-sorted names
- **notes** (optional): developer comments
- **order** (optional): authoring order

Key = `h = h*33 + c` truncated to 16 bits.

**Textures**: a 0x70-byte `texture` / `04.05.05.0032` header in `.data`, holding the
D3DFORMAT, kind (0 2D, 2 cube, 4 array, 5 volume), dimensions, base/mip offsets and mip
count; volumes keep their depth at +0x38. The GPU data in `.texturegpu` uses standard
Xbox 360 tiling: 32×32-block macro tiles, 4 KB-aligned levels, and a packed mip tail
once the smaller dimension is ≤ 16 texels. Volume textures (fur, animated water) are
stored exactly like arrays. Every slice is an ordinary 2D-tiled surface, and every mip
level keeps the full depth.

**Models** (`model.hpp` has the details):
- `.data` starts with a chunk directory of `{type, ptr}` pairs. Chunk 0x1E points at
  the `rendergraph` (`26.09.07.0039`), and chunk 0x0A at the render list.
- **Rendergraph**:
  - +0x20: a property dictionary. It maps bone names to indices, `texturefileN` to
    texture names (split textures use N for the mip half and N+1 for the streamed top),
    and `shader_*` to materials.
  - +0x104: the `pose`, 52-byte bone records `{local, world, quaternion, parent}` plus
    a name table.
  - +0x28: the authoring nodes, 0x50-byte records named by Maya paths.
    `LODBIT_|…|Banjo_0_1_` draws at detail levels 0 and 1, `…_LOD_2` at level 2, and
    `…_SWITCH1_2` while switch 1 is in state 2.
  - +0xFC: the vertex and index buffer records pointing into `.gpu`.
- **Render list**: draw groups `{node path, draws}`. A group draws when every node on
  its path is active. Each draw is 32 bytes `{stream binding, …, constants, sampler
  bindings, index pair}`:
  - Sampler bindings name the `texturefileN` each slot uses.
  - The constants point at register blocks in `.stream`. The material's named
    parameters (`c:basecolour`, `c:textureGain0`, `c:specularPower`, …) take those
    registers in order.
- **Vertex buffers**: `half4` position; skinned buffers add `u16×4` bone indices and
  `u8×4` weights; then a DEC3N normal, an optional DEC3N tangent, and `half2` UV sets.
- **Index buffers**: 16-bit triangle lists.

**Animations** (`aid_anim_*`, header `animation` / `19.12.06.0036`):
- **Header**: duration at +0x1C, then bone and authored-frame counts (30 per second).
  +0x5C is the fraction of frames stored (0.5 on most animations): the data holds
  ⌈(N − 1) × fraction + 1⌉ frames, and the last one is always the final authored frame.
  +0x24 points at a
  `QUAT_BITSTREAM32` (or 16-bit `QUAT_BITSTREAM`) track, and +0x48 at the `pose` the
  animation was made for (bone names and hierarchy).
- **Track**: per bone, 9 channels (rotation x/y/z, translation, scale), decoded into
  `{x y z w tx ty tz - sx sy sz -}` with w = √(1 − x² − y² − z²).
  - Mask bytes mark each channel absent (default value), constant (base × scale) or
    animated ((base + bits) × scale).
  - The bits are read LSB-first from big-endian words, with a fixed number of bytes per
    frame and per-channel widths.
  - This is the game's decoder at 0x8220A6A8.
- **Applying keys**: rotations replace the bind rotation (identity on the characters),
  translations add to the bind offset, and vertex bone indices are global skeleton
  indices.

**SBP fonts**: `01 01 lineHeight bpp u16 count`, then `{u16 code, u32 offset, i8 metrics[4]}`
per glyph, then one zlib stream per glyph (48×48 cells at 4 bpp, low nibble first).
`metrics` are `{unused, A, B, C}` ABC widths in pixels at the line height: the game
draws the cell at pen + A (cell row 0 = top of the line) and advances by A + B + C.

**Game scripts** are flat big-endian records with no pointers or relocations, so they
can change size:

- scene script: `{u32 size, u32 opcode, args}` per command, ending with opcode 0. Every
  opcode has one size; strings are fixed fields of 32, 64 or 256 bytes.
- actor goals: `{u16 size, u16 flags, char goal[64], args}` per goal, the goal named by
  its `actorMindGoalId_` enum name.
- script table: `{char name[64], u32 asset id}` per entry (names such as `carried`,
  `evade`, pointing at actor goal lists).
- state table: `{char state[64], u32 asset id, u32 asset id, char name[64]}` per entry.

Objects point at these by asset id: an `objparams` asset holds its script table's id, the
script table the actor goals' ids. Some scene scripts are started by the game's code.

## Limitations

- **Untested on hardware or emulators.** Nothing has been run in the game. The
  formats round-trip byte-for-byte and edited files parse correctly, but they have not
  been loaded by the game.
- **Cube and volume textures** can be viewed and exported (one image per slice) but not
  imported. Array textures can be imported and resized from images (not from DDS).
- **Models**: imported geometry replaces existing meshes only (no new parts or
  materials), skinning is copied from the nearest original vertex rather than painted, and
  bounding volumes are not updated, so geometry far outside the original may be culled.
  None of this has been tested in the game yet.
  - **Textures**: each mesh's texture is the colour map among those its draw binds,
    picked by name. The shader programs that give each sampler its meaning are not
    decoded.
  - **Fur**: drawn with an estimated length. Banjo's DXT1 fur volume carries no coverage,
    so per-texel strand noise stands in for it.
  - **Switches**: their default is the first authored state, which may differ from what
    the game shows in a given scene.
  - **Empty models**: 161 model records have no draw groups. They're invisible helpers,
    or geometry the game builds at runtime (grass, god rays).
- **Animations** play one at a time. The game layers some of them, for example Kazooie's
  idles on top of Banjo's. Their float tracks (facial and other custom channels) and
  animation events are not used. They can be viewed and exported (FBX), not edited.
- **Game scripts**: the opcodes have no names (the game keeps none), and argument types
  were inferred from the shipped scripts, so an argument that is always 0 shows as an
  int even where the game reads a float.
- **Objparams**: only vehicle parts have named fields, and only their shared base (plus
  fuel and ammo capacity); class fields and other objects' fields go by offset, with
  types guessed per value (a field that is 0 shows as `0`).
- **Havok data and other gameplay assets** are unpacked as raw
  binary files together with their relocation tables. No converter to standard formats
  exists for them yet. Same-layout edits (values, hex edits) work.
- **Textures embedded in models** (named like `_0x05B6FB15.bin`) live inside the
  model's `.gpu` data and are not extracted as images.
- **Wave banks** are exported but not rebuilt. To change audio, replace a whole
  `.xwb` entry with one built by another XACT tool, then `sb-pack`. XMA is extracted
  in its RIFF container, not decoded to PCM.
- **Changing a texture's size or format** is not supported. Imports must match the
  existing dimensions.
