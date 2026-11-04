# NutTool starter tutorial

This tutorial takes you from a fresh copy of the game's files to a working mod you can
play in reNut. It covers the everyday jobs: finding things, swapping and upscaling
textures, recolouring a character, changing text, and packaging your work. When you are
comfortable with these, the [advanced tutorial](tutorial-advanced.md) covers replacing
characters, bringing your own models, and the deeper editors.

- [1. Set up](#1-set-up)
- [2. Find your way around](#2-find-your-way-around)
- [3. Swap a texture](#3-swap-a-texture)
- [4. Upscale many textures at once](#4-upscale-many-textures-at-once)
- [5. Recolour a character](#5-recolour-a-character)
- [6. Change the game's text](#6-change-the-games-text)
- [7. Listen to and export audio and video](#7-listen-to-and-export-audio-and-video)
- [8. Package your work as a mod](#8-package-your-work-as-a-mod)
- [9. Undo things](#9-undo-things)
- [Quick reference](#quick-reference)

## 1. Set up

You need:

- **NutTool**: `nuttool-gui.exe` (the editor this tutorial uses) and `nuttool.exe` (the
  command-line version, optional).
- **A copy of the game's `assets` folder** (the folder with `Bundle`, `Debug`,
  `loctext` and `default.xex` in it). Work on a copy, never on your only one.
- **reNut**, to play your changes.
- Optionally the **title update** files (TU3: a folder of files named like `4f3ef5f9`,
  plus `db_index.txt` and `default.xexp`). Put them in a folder called `update` next to
  your `assets` folder and NutTool finds them by itself.

Then:

1. Start `nuttool-gui.exe`.
2. **File > Open assets folder...** and pick your `assets` folder (it also opens on its
   own if `assets` sits next to the program).
3. Wait for the progress bar at the bottom ("Indexing...") to finish. The first time
   takes a minute; after that it is cached. Indexing adds the **Textures**, **UI icons**,
   **Models**, **Audio**, **Scripts** and **Vehicle parts** groups to the browser.
4. Check that **File > Make .bak backups when saving** is ticked. It is on by default.

> **Why backups matter.** The first time NutTool saves over a file, it keeps the original
> as `<file>.bak`. Packaging a mod, restoring files and fitting menus all compare your
> files with those backups. Leave the option on, and don't delete the `.bak` files.

## 2. Find your way around

The window has three parts: the **browser** on the left, **tabs** in the middle, and the
**status bar** at the bottom (**View > Log** shows every message).

The browser lists everything in the game:

| Group | What is in it |
|---|---|
| **Bundles** | the game's main containers. Each holds hundreds of assets: models, textures, scripts... |
| **Stream bundles** | the big companion files: full-size textures, audio banks |
| **Textures**, **UI icons** | every texture in the game, once each, however many files hold a copy |
| **Models** | every model, once each |
| **Audio** | every sound bank, grouped (Music, Vocals, Engines...) |
| **Scripts**, **Vehicle parts** | the game's scripts and the garage parts (see the advanced tutorial) |
| **Text** | the game's text, per language |
| **Videos**, **Fonts** | the cutscene movies and the menu fonts |
| **Title update** | the title update's own files, when NutTool found them |

Type in the **search box** at the top of the browser to filter every group at once.
Names are the game's own, such as `aid_texture_banjox_ui_buttons_a` or
`aid_model_banjox_actor_mumbo`, so short words work well: `mumbo`, `jiggy`, `buttons_a`.

Click anything to open it in a tab. Tabs with unsaved changes are marked; **Ctrl+S** saves
the current tab.

## 3. Swap a texture

Let's repaint one texture: the green A button the menus show.

1. Search for `buttons_a` and open **`aid_texture_banjox_ui_buttons_a`** from the
   **Textures** group.
2. The tab shows a preview, its size and format, and **Where it lives**: every file that
   holds a copy. NutTool changes all of them together, so you never have to hunt for copies.
3. Click **Export PNG...** and save it somewhere.
4. Edit the PNG in any image editor. Keep transparency where the original has it.
5. Back in NutTool, click **Import image...** and pick your edited file.
6. NutTool compresses it into the game's format, rebuilds the smaller mip copies, writes
   every copy, and saves straight away (with backups).

That's a texture swap. A few things worth knowing:

- **Any size works.** Import a 256x256 image over a 64x64 texture and the texture becomes
  256x256. The model or menu doesn't need any change. Keep the same shape (a square stays
  a square) or it will look squashed.
- **Bigger textures use more memory.** A few sharper character or menu textures are fine;
  quadrupling a whole level may not fit in the game's memory.
- **Menu textures are fitted for you.** Menus draw many images at their pixel size, so a
  bigger button icon would show only its top-left corner, cropped. After an import that
  changes a texture's size, NutTool fits the menus that use it so the icon keeps its size on
  screen. The log says how many menu elements it adjusted.
- **PNG for anything with transparency.** JPG has no alpha channel.
- **DDS files** are imported as they are, without recompressing, but must use the
  texture's exact format (the tab tells you which).

You can also do this inside a bundle: open a bundle, pick a texture symbol in the list,
and use the same buttons. That changes only that one file, and you save it with Ctrl+S.

## 4. Upscale many textures at once

For upscaling packs (AI upscalers and the like) you want every texture out, and the
improved ones back in.

1. **Tools > Extract everything...**, tick **Textures only**, choose an output folder, and
   start. Use **Only names containing** to narrow it down: `ui_` gets just the menu and HUD
   textures.
2. Upscale or edit the images you want. Put the finished ones in **one folder**.
3. **Keep the exact file names.** Each image goes into the texture it is named after.
   Many upscalers rename their output (`name-gigapixel-4x.png`); rename those back to
   `name.png`, or NutTool skips them as "not named after a texture".
4. **Tools > Bulk import textures...**, pick the folder, and start.

The result line tells you what happened, for example *"imported 9 textures into 1 file; 0
unchanged, 2 not named after a texture, fitted 63 menu elements"*:

- **unchanged**: the image is identical to the texture (an unedited export), so it was skipped.
- **not named after a texture**: rename the file to the texture's exact name.
- **not imported (see the log)**: open the log for the reason, one line per file.

**Animated textures** come as several images: the action buttons
(`aid_texture_banjox_ui_buttons_actionbuttons_*`), for example, have two frames each and
export as `..._slice0.png` and `..._slice1.png`. Upscale both frames and keep both names;
the bulk import puts each back in its frame. If you give only one image, every frame gets
it and the animation stands still.

## 5. Recolour a character

Some outfits have no colour texture and take their colours from material settings
instead. Banjo's tuxedo is one.

1. Open **Models** and pick **`aid_model_banjox_actor_banjotuxedo`**.
2. The 3D view shows the model. Drag with the left mouse button to orbit, use the wheel
   to zoom, and double-click to reset the camera.
3. Open the **Material colours** section. Each entry is one colour setting of the model's
   materials, such as `banjoTuxedoModel1_tuxFabric` and its `c:basecolour`.
4. Click a colour and pick a new one. The view updates as you go.
5. Click **Save**. NutTool writes the colours into every file that holds the model.

To see your character move, pick an animation in the **Animation** section and press play.

## 6. Change the game's text

1. Open **Text**, pick a language, and open a table (English tables are under `english`).
2. Use the search box to find a line, and edit its text in place. Other languages show the
   English line next to theirs for reference.
3. **Keep the markup.** Codes in braces such as `{MOODSAD}` are part of the text and tell
   the game how to show it.
4. **Add string...** and **Delete string** add and remove lines; **Export JSON...** and
   **Import JSON...** let you edit a whole table in a text editor.
5. Press **Ctrl+S** to save.

## 7. Listen to and export audio and video

- **Audio**: open a bank from the **Audio** group and click a wave to play it.
  **Export WAV...** saves the selected sound as an ordinary WAV, and **Export all as
  WAV...** the whole bank. The browser search also finds banks by the names of the
  sounds inside them.
- **Videos**: open one from **Videos** to play it; **Export as .wmv...** saves it as a
  normal video file, and **Replace with...** puts a new one in.
- **Fonts**: open a font to see every letter. **Export...** writes the letters as images
  plus a ready-made `.ttf` font file.

## 8. Package your work as a mod

reNut never changes your game folder. It loads mods (`.nbpatch` files) from its `mods`
folder at startup, and serves your changed files in place of the originals.

1. **Tools > Export mod package (.nbpatch)...**
2. Give the mod a **name** (plus author, version and description if you like).
3. Leave the source on **Files that have a .bak backup (saved by NutTool with backups
   on)**, then click **Find changed files**. Every file you have changed is listed,
   title update files included (they show as `update/...`). Untick any you want to
   leave out.
4. Click **Build package** and save the `.nbpatch`.
5. Copy it into reNut's **`mods`** folder (next to `renut.exe`) and start the game.

The package stores only the bytes you changed, and records exactly which original files
it was made from. If a player's files differ, reNut skips the mod rather than break the
game.

> If you edited textures that a menu uses, the menus were changed too. Their files are in
> the list automatically, because they have backups. Keep them in the mod.

## 9. Undo things

- **Before saving:** close the tab without saving, or use **Discard changes** in a model
  tab.
- **After saving:** **Tools > Restore original files (revert all)...** lists every file that
  differs from its backup. Tick the ones to put back and click **Restore**. This undoes
  *everything* saved to those files, not just the last change.
- **Check your files**: **Tools > Verify all files** reads every file in the game and
  confirms NutTool can read it back exactly.

## Quick reference

| I want to... | Where |
|---|---|
| repaint a texture | its tab in **Textures**: Export PNG, edit, Import image |
| upscale lots of textures | Tools > Extract everything (Textures only), then Tools > Bulk import textures |
| fix cropped menu icons after resizing | automatic; or Tools > Fit menus to resized textures |
| recolour an outfit | the model's tab: Material colours, then Save |
| change text | **Text** > language > table, then Ctrl+S |
| save a sound | its bank in **Audio**: Export WAV |
| make a mod | Tools > Export mod package (.nbpatch) |
| put files back | Tools > Restore original files |

Next: the [advanced tutorial](tutorial-advanced.md), for replacing characters, bringing
your own models, working in Blender, new animations, vehicle parts, scripts and the
command line.
