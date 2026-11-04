# nuttool

A tool for reading, unpacking and editing the assets of
**Banjo-Kazooie: Nuts & Bolts** (Xbox 360). It handles everything in the game's
`assets` folder except `default.xex` / `default.xexp`.

It comes in two forms that share the same code:

- **`nuttool-gui`**: a desktop editor (Dear ImGui + SDL3).
- **`nuttool`**: a command-line tool for scripting and batch work.

Both are Windows programs.

New to NutTool? Start with the tutorials:

- **[Starter tutorial](docs/tutorial-starter.md)**: finding your way around, texture swaps
  and upscaling, recolouring, text, audio, and packaging a mod for reNut.
- **[Advanced tutorial](docs/tutorial-advanced.md)**: replacing characters with other
  game characters or your own models, split and animated textures, menus and the title
  update, vehicle parts, game scripts, fonts, the command line, Lua and raw assets.

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

Start `nuttool-gui.exe`. It finds an `assets` folder next to it or in the current
folder; otherwise use **File > Open assets folder**. You can also pass a folder or file on
the command line, or drag one onto the window.

- **Browser** (left): every bundle, stream bundle, string table (by language), video,
  font and metadata file, named from `db_index.txt`, with a search box. When a folder
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
- **Model tabs**: a 3D viewer (OpenGL) with textures, material colours, fur shells,
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
  - **Material colours**: a colour picker for each colour constant of the shown meshes
    (`c:basecolour`, `c:velvetColour`, `c:specularColour`...).
    - A change applies to every mesh and detail level that uses the same material
      colour.
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
  - **Textures** (thumbnails, each opening its own tab), the **skeleton** tree, and the
    **materials**.
  - **Export OBJ** writes the shown meshes with an `.mtl` and PNG textures.
  - **Export FBX** writes one `.fbx`: the shown meshes skinned to the skeleton, the
    materials with their colour maps embedded at full size, and animations as takes. You
    can include every animation named after the model, only the one playing, or none.
  - **Replace**: **Import OBJ** applies an edited export or new geometry (see
    "Models" below), and **Replace with another model** copies any other game model into
    this one's place. Both show in the viewport straight away; **Ctrl+S** writes them into
    every bundle holding the model, and **Discard changes** goes back to what is on disk.
  Bundle views have an **Open in model viewer** / **Open texture tab** button for
  models and textures.
- **Bundles**: every asset inside with a filter. Selecting one shows a hex view and
  **Export raw / Replace raw**. Selecting a texture shows a preview with mip/face/channel
  controls and **Export PNG / Export DDS / Import image / Remove mipmaps**. Streamed top levels from the
  matching stream bundle are merged automatically, and importing updates both files.
  There are also buttons for **Export all textures** and **Unpack to folder**.
- **Stream bundles**: an entry list. CAFF entries open in the same asset view; wave banks
  open the audio player (below); entries can be exported or replaced.
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
  languages, plus wrapped previews, add/delete string, and JSON import/export.
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
  that holds it. Any `aid_objparams_` symbol in a bundle tab shows the same editor.
- **Videos**: a player (Media Foundation decodes the WMV3 video and WMA audio) with
  play/pause (Space or click the picture), seeking, volume and loop, plus export/replace.
- **Font metadata, tile.bmp, asset index**: a text editor, an image view, and a
  searchable index (double-click opens the file).
- **Tools**: *Extract everything* (with progress and cancel; **Textures only** writes
  just the textures as PNG into one folder, and **Only names containing** keeps only what
  is named with some text, such as `ui_`), *Export mod package* (a
  `.nbpatch` of your edits for reNut, see below), *Bulk import textures* and *Remove
  mipmaps from changed textures* (see [Bulk texture jobs](#bulk-texture-jobs)), *Fit menus
  to resized textures* (see [Resized menu textures](#resized-menu-textures)), *Restore original files* (puts each
  file's `.bak` back, undoing everything saved to it; pick which files), *Verify all
  files*, *Script (Lua)* (see "Scripting" below), and an *Asset ID calculator*.

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
writes the file's textures. Most models use textures from shared bundles; the GUI's
**Export OBJ** collects those.

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

### Audio and video

`nuttool wave-export assets\Bundle\50\685374 waves` writes each wave bank as `.xwb`,
plus every wave as stored: RIFF XMA2 (`.xma`) or PCM `.wav`. Add `--wav` to decode
every wave to 16-bit WAV that any player opens, and `--name Music` to export only the
banks whose names contain that text. All 420 banks are in `Bundle\50\685374`.
XMV files are ordinary WMV: `nuttool xmv-info <file> copy.wmv`.

### Models

```
nuttool model-export assets\Bundle\4f\685374 models          # then edit the OBJ in Blender
nuttool model-import assets\Bundle\4f\685374 banjox_actor_banjotuxedo models\edited.obj
nuttool model-swap assets\Bundle\4f\685374 banjox_actor_banjotuxedo assets\Bundle\4f\685374 banjox_actor_banjo
nuttool model-fbx assets\Bundle\4f\685374 banjox_actor_banjo banjo.fbx   # for Blender, Maya, Unity...
```

`model-fbx` writes a binary FBX 7.4 file:

- **Skeleton**: the bones as a joint hierarchy under a null named after the model, posed
  as the bind pose.
- **Meshes**: the meshes shown by default (`--all-groups` for every detail level and
  switch state), each named `group<G>_mesh<N>` like the OBJ export. Each has a skin with
  the original bone weights, normals, and one or two UV sets.
- **Materials**: diffuse colour and specular, with the colour map embedded as PNG.
  Textures come from the bundle and its paired stream bundle (`Bundle/50/<same name>`),
  so split textures are full size. `--no-textures` leaves them out.
- **Animations**: the ones named after the model (`aid_anim_<model>_*`), one take each.
  They come from the same bundle, or from another with `--anims <bundle>`; `--all-anims`
  takes every animation there and `--no-anims` none. They are sampled at 30 frames a
  second the way the viewer plays them. Keys that linear interpolation reproduces are
  dropped, and so are channels that stay at the bone's rest transform. Rotations are XYZ
  Euler angles.

The GUI's **Export FBX** finds animations and textures anywhere in the game through its
index, so it can include more than the command line, which sees only the bundles you
give it. Units are metres (the file says 1 unit = 100 cm), Y up. The file is laid out
as Blender's own exporter writes FBX. In Blender, *Automatic Bone Orientation* makes the
bones point along the limbs.

`model-import` matches OBJ objects to meshes by the `mesh<N>` in their names, as the
export writes them (`group0_mesh13`), so keep those names. For each object:

- **Same vertex and triangle count** (moved, scaled or reshaped vertices, new UVs or
  normals): the mesh is edited in place. Nothing else changes, and an unedited export
  imports back byte for byte. In Blender, import with *Split by Object*, keep the vertex
  order (do not merge or delete vertices), and export with *Triangulated Mesh* off or on.
- **Any other geometry**: the mesh gets new vertex and index buffers, appended to the
  model's `.gpu` part. The draw keeps its material and textures. Each new vertex takes
  its bone weights, tangent and extra texture coordinates from the nearest original vertex
  of that mesh, so a replacement that sits where the old part was follows the same bones.
  A vertex buffer holds at most 65,535 vertices (counting the old ones other draws use).
- **An object with no faces** (just a vertex) hides the mesh.

Meshes not named in the file stay as they are. New parts cannot be added; replace an
existing mesh instead. Other detail levels draw their own meshes, so either edit those too
or set the detail level to 0 in game.

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

**[docs/model-swaps.md](docs/model-swaps.md)** walks through a swap step by step: what
the swap notes mean, testing in reNut, and what to do when a swapped character crashes the
game, T-poses or looks distorted.

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

**[docs/scripting.md](docs/scripting.md)** is the full reference: every function, path
rules, binary data, saving and backups, and recipes (batch FBX export, text edits,
audio, byte patching).

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
