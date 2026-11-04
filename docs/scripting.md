# Lua scripting in NutTool

NutTool runs Lua scripts that work directly on the game's files. A script can open
bundles and stream bundles, inspect models, recolour materials, import geometry, edit any
asset's raw bytes, read wave banks, and run every `nuttool` command (texture import and
export, model swaps, FBX export, text editing and the rest). It can also repeat any of
this over hundreds of files.

- [Running scripts](#running-scripts)
- [The script environment](#the-script-environment)
- [Paths](#paths)
- [Bytes and binary data](#bytes-and-binary-data)
- [Errors](#errors)
- [Reference: `nut` functions](#reference-nut-functions)
- [Reference: File objects](#reference-file-objects)
- [Running commands with `nut.run`](#running-commands-with-nutrun)
- [Saving, backups and open tabs](#saving-backups-and-open-tabs)
- [Recipes](#recipes)
- [Performance](#performance)
- [Limits](#limits)

> **Scripts are programs.** They run with the full Lua standard library, including `io`
> and `os`, so a script can read, write and delete any file you can, and start other
> programs. Only run scripts you trust.

## Running scripts

### In the GUI

**Tools > Script (Lua)...** opens the script window:

- **Run** (or **Ctrl+Enter**) runs the script in the background. The output pane shows
  everything it prints, then `-- finished` or `-- failed`.
- **Stop** interrupts a running script, as does **Cancel** in the status bar.
- **New** puts the example script back. **Open...**, **Save** and **Save as...** work
  on `.lua` files.
- The editor text is kept between sessions in `script.lua` next to NutTool's settings
  (`%APPDATA%\NutTool\NutTool`).

Scripts in the GUI see the assets folder that is open (`nut.assets`). To open the
window and run a file at startup, pass `--script <file.lua>` on the command line.

### From the command line

```
nuttool script <file.lua> [--assets <assets folder>] [arguments...]
```

- `--assets` sets `nut.assets`. It must come right after the file name.
- Everything after the file name (and `--assets`) goes to the script's `arg` table,
  including words that start with `-`.
- The exit code is 0 when the script finishes and 1 when it fails.
- Output goes to the console. Ctrl+C ends the whole program.

```
nuttool script recolour.lua --assets F:\game\assets 0.8 0.1 0.1
```

## The script environment

Scripts are Lua 5.5 with every standard library loaded (`string`, `table`, `math`,
`utf8`, `io`, `os`, `coroutine`, `debug`, `package`). The [Lua 5.5 reference
manual](https://www.lua.org/manual/5.5/) covers the language. NutTool adds:

| Name | What it is |
|---|---|
| `print(...)` | Writes its arguments, separated by tabs, to the output (the script window's output pane, or the console) |
| `nut` | The table of NutTool functions described below |
| `nut.version` | NutTool's version, for example `"0.2.0"` |
| `nut.assets` | The assets folder: the GUI's open folder, or `--assets`; `""` when there is none |
| `nut.script_dir` | The folder holding the running script; `""` for an unsaved script in the GUI |
| `arg` | Arguments from the command line: `arg[1]`, `arg[2]`... and `arg[0]`, the script's name |

Use `print` for output. `io.write` writes to the program's standard output, which the
GUI doesn't show.

Lua indexes from 1, and so does NutTool: symbol 1 is a bundle's first symbol, and entry 1
a stream bundle's first entry.

## Paths

Every path a `nut` function takes is resolved the same way:

1. An **absolute** path (`F:/game/assets/Bundle/4f/685374`, `/home/me/x.lua`) is used as
   it is.
2. A **relative** path that **exists in the assets folder** is taken from there, so
   `"Bundle/4f/685374"` means that bundle in `nut.assets`.
3. Any other relative path is relative to **the script's folder** (`nut.script_dir`).
   Output files with relative names land next to your script, never inside the game
   folder.
4. When there is no script folder (an unsaved GUI script), it is relative to the
   program's current folder. Use absolute paths there.

Forward slashes work on Windows too. Paths are UTF-8.

`nut.run` is different: it passes its arguments to the command untouched. Build full
paths for it yourself (`nut.assets .. "/Bundle/4f/685374"`), see
[Running commands](#running-commands-with-nutrun).

## Bytes and binary data

Asset data travels as Lua strings, which hold any bytes. `#s` is the size, and
`string.byte`, `string.sub` and `string.unpack` read them. The game ran on the Xbox
360, so its data is **big-endian**. Use `>` in `string.unpack` formats:

```lua
local u32, nextPos = string.unpack(">I4", data, offset + 1) -- offset counted from 0
local f1, f2 = string.unpack(">ff", data, 1)               -- two big-endian floats
local patched = data:sub(1, 8) .. string.pack(">I4", 1234) .. data:sub(13)
```

## Errors

A failing `nut` function raises a Lua error with a message, and so do Lua errors in the
script. Uncaught, an error stops the script and prints `error:` followed by the message,
which starts with the script's name and line number when Lua knows them
(`error: recolour.lua:12: no model 'banjox_actor_mumb'`). Catch errors with `pcall`:

```lua
local ok, f = pcall(nut.open, "Bundle/4f/does_not_exist")
if not ok then print("skipped:", f) end   -- f holds the message here
```

Raise your own with `error("message")`.

## Reference: `nut` functions

### nut.open(path) → File

Opens an asset file and returns a [File object](#reference-file-objects). It reads the
whole file (decompressing XCTD bundles) and detects its kind. It raises an error when the
file is missing or can't be parsed. Before loading, it frees Files the script no longer
refers to, so loops over many files don't pile them up in memory.

```lua
local f = nut.open("Bundle/4f/685374")
print(f:kind())   --> bundle
```

### nut.run(command, args...) → code, output

Runs one `nuttool` command and returns its exit code (0 means success) and everything it
printed. See [Running commands](#running-commands-with-nutrun).

```lua
local code, out = nut.run("hash", "aid_model_banjox_actor_banjo")
print(code, out)   --> 0   04041d5e  aid_model_banjox_actor_banjo  Debug/04/04/1d/5e
```

### nut.list([folder [, recursive]]) → { names }

Returns the names of the files (not folders) in a folder, sorted. `folder` defaults to
`"."`, which the [path rules](#paths) make the assets folder (or the script's folder when
there is none). With `recursive` true, it searches subfolders too and returns paths
relative to `folder`, with `/` separators.

```lua
for _, name in ipairs(nut.list("Bundle/4f")) do
  if not name:find("%.bak$") then print(name) end
end
```

### nut.exists(path) → boolean

Whether a file or folder exists.

### nut.read(path) → bytes

A whole file as a string of bytes.

### nut.write(path, bytes)

Writes a file, replacing it if it exists. It creates missing folders, and makes no
backup.

### nut.hash(name) → id

The 32-bit asset id of an `aid_` name. Raises "not an asset name" for names whose type
prefix is unknown. An asset stored as a loose file lives at
`Debug/<id byte 1>/<byte 2>/<byte 3>/<byte 4>` (hex):

```lua
local id = nut.hash("aid_loctext_banjox_actor_hitdialogs")
local path = string.format("Debug/%02x/%02x/%02x/%02x", id >> 24, (id >> 16) & 255, (id >> 8) & 255, id & 255)
```

### nut.wavebank(bytes) → table or nil

Parses an XACT wave bank (a stream bundle entry) and returns `nil` for any other data:

| Field | |
|---|---|
| `name` | the bank's name, for example `"DialogJamJars"` |
| `version` | XACT version |
| `waves` | one table per wave, below |

Each wave has `name`, `format` (`"xma"`, `"pcm"`, `"adpcm"` or `"wma"`), `channels`,
`rate` (Hz), `bits`, `samples`, `seconds`, `loop_start` and `loop_length` (in samples;
0 when it doesn't loop), and `size` (stored bytes).

To get the audio itself, export it with `wave-export` (see the recipes).

## Reference: File objects

`nut.open` returns a File. Its methods are called with `:`, as in `f:symbols()`. A File
holds the whole asset in memory, and changes stay there until `f:save()`.

### Any file

| Method | Returns |
|---|---|
| `f:kind()` | `"bundle"` (a CAFF: bundles in `Bundle/4f`, loose assets in `Debug/`), `"streambundle"` (`Bundle/50`), or another kind: `"XACT wave bank"`, `"XUI font cache"`, `"XMV/ASF video"`, `"INI text"`, `"BMP image"`, `"asset index"`, `"unknown"` |
| `f:path()` | the resolved path |
| `f:save([path])` | writes the file and returns the path written. See [Saving](#saving-backups-and-open-tabs) |
| `f:close()` | frees the file's memory now (unsaved changes are dropped); using `f` afterwards raises "the file is closed" |
| `tostring(f)` | `nut.File(<path>, <kind>)` |

A File can also be a to-be-closed variable, freed as soon as it goes out of scope:

```lua
for _, name in ipairs(nut.list("Bundle/4f")) do
  local f <close> = nut.open("Bundle/4f/" .. name)
  -- ...
end
```

### Bundles and CAFFs

These raise "<path> is not a bundle" on other kinds of file.

#### f:symbols() → { symbol }

Every symbol (the pieces a CAFF is made of), in order. Each is a table:

| Field | |
|---|---|
| `index` | its number (from 1), for `f:read` and `f:write` |
| `name` | the symbol's name, for example `aid_model_banjox_actor_mumbo` or `manifest` |
| `asset` | the asset name without per-part suffixes |
| `section` | `.data`, `.gpu`, `.stream`... |
| `size` | bytes |

An asset usually has one symbol per section, all sharing the asset name: a model has
`.data` (its rendergraph), `.gpu` (vertex and index buffers) and often `.stream` parts.

#### f:find(name [, section]) → index or nil

The first symbol whose name or asset name is `name`, optionally only in `section`
(`".gpu"`).

#### f:read(index) → bytes

A symbol's bytes.

#### f:write(index, bytes)

Replaces a symbol's bytes. The size may change: later symbols in the same section move
(keeping their alignment) and the file's internal pointers are updated. The change stays
in memory until you save.

#### f:textures() → { names }

The names of the textures stored in the bundle (split textures appear as their "mip"
halves).

#### f:models() → { names }

The names of the models in the bundle (`aid_model_...`).

Wherever a method takes a **model** name, you may leave out `aid_model_`:
`"banjox_actor_mumbo"` and `"aid_model_banjox_actor_mumbo"` are the same model.

#### f:model(model) → table

A summary of a model:

| Field | |
|---|---|
| `name` | full name |
| `meshes`, `vertices`, `triangles` | counts over every mesh (all detail levels and alternative parts) |
| `groups`, `nodes`, `switches` | draw groups, authoring nodes, part switches |
| `bones` | bone names, in skeleton order |
| `textures` | texture names it uses |
| `materials` | shader names it uses (`shader_...`) |

#### f:colours(model) → { colour }

The model's colour constants: the material parameters with "colour" in their name, such
as `c:basecolour`, `c:specularColour` or `c:velvetColour`. Like the GUI's *Material
colours* list, it groups constants that share a material, a name and a value. Each entry
has:

| Field | |
|---|---|
| `material` | the material (shader) name, for example `blinn11` |
| `constant` | the constant's name, with its `c:` prefix |
| `r`, `g`, `b`, `a` | the value (usually 0–1; can be higher for brighter-than-white) |
| `uses` | how many draws use this value (each detail level repeats it) |

#### f:set_colour(model, material, constant, r, g, b [, a]) → count

Sets a colour constant wherever the model uses `constant` on `material`, at every detail
level. The `c:` prefix on `constant` is optional. Alpha keeps its old value unless you
pass `a`. Returns how many uses changed; 0 means nothing matched, so check your names
against `f:colours`.

```lua
local n = f:set_colour("banjox_actor_mumbo", "blinn11", "velvetColour", 0.8, 0.1, 0.1)
```

#### f:import_obj(model, objPath) → report

Applies a Wavefront OBJ to the model, exactly like `nuttool model-import`. OBJ objects
are matched to meshes by the `mesh<N>` in their names, as the export writes them
(`group0_mesh13`):

- an object with the **same vertex and triangle count** edits that mesh in place;
- any other object **replaces** the mesh's geometry, taking skinning from the nearest
  original vertex;
- an object **without faces** hides the mesh.

Returns `{edited, rebuilt, hidden, notes}`: three counts and a list of messages. It raises
an error when the file names no mesh of the model or doesn't fit.

### Stream bundles

These raise "<path> is not a stream bundle" on other kinds of file.

| Method | Returns |
|---|---|
| `f:entries()` | one `{index, id, size}` per entry: its number, asset id and size |
| `f:entry(i)` | entry `i`'s bytes (a CAFF, or an XACT wave bank) |
| `f:entry_name(i)` | entry `i`'s asset name, for example `wavebank_DialogJamJars` or `aid_texture_..._0x05910195top`; `""` when it has none |
| `f:set_entry(i, bytes)` | replaces entry `i` (any size) |

An entry that is itself a CAFF can't be opened as a File directly. Write it out with
`nut.write` and open that, or use the `extract` and `replace` commands.

## Running commands with `nut.run`

`nut.run(command, ...)` runs any `nuttool` command with the arguments you give, and
returns its exit code and output. Nothing is printed unless you print it:

```lua
local code, out = nut.run("tex-export", nut.assets .. "/Bundle/4f/685374", nut.script_dir .. "/textures", "--png-only")
if code ~= 0 then error(out) end
```

- Every argument must be a string: use `tostring(x)` for numbers.
- Paths are passed through untouched and relative ones are relative to the program's
  current folder, so build absolute paths from `nut.assets` and `nut.script_dir`.
- `nut.run("help")` returns the full list of commands. The README has examples of each.
- A command can't be stopped halfway. **Stop** takes effect when it returns.

The commands scripts use most:

| Command | Does |
|---|---|
| `info <file>`, `list <file>` | describe a file, list its symbols or entries |
| `tex-export <file> <dir> [--png\|--png-only]` | textures as DDS/PNG |
| `tex-import <file> <texture> <image> [--stream <sb>]` | replace a texture from a PNG, JPG, BMP, TGA or DDS; another size resizes it |
| `tex-remove-mips <file> <texture>` | remove a texture's mipmaps (a split texture keeps level 1) |
| `tex-remove-mips-changed <game dir> [--original <dir>]` | remove the mipmaps of every texture that differs from its `.bak` (or `--original`) |
| `menu-fix <game dir> [bundle...]` | fit the menus to textures whose size differs from their `.bak` |
| `extract-all <game dir> <out> [--textures-only [--dds]] [--filter text]` | unpack everything, just the textures, or only what is named with some text |
| `model-export <file> <dir> [--png]` | models as OBJ + MTL |
| `model-import <file> <model> <in.obj>` | apply an OBJ (same as `f:import_obj`) |
| `model-swap <file> <model> <source file> <source model>` | put another model in a model's place |
| `model-fbx <file> <model> <out.fbx>` | export a model with skeleton, textures and animations |
| `text-export <loctext> <out.json>`, `text-import <loctext> <in.json>` | edit strings |
| `wave-export <stream bundle> <dir> [--wav] [--name text]` | wave banks and their waves, optionally decoded to WAV |
| `extract`, `replace` | one symbol or stream bundle entry to or from a file |
| `make-nbpatch <game dir> <out.nbpatch> --name <name>` | package your edits as a mod |

Commands that change a file in place make its one-time `.bak` backup, just like saving.

## Saving, backups and open tabs

- `f:save()` writes the file **in place**. The first time a file is saved, the original
  is copied to `<file>.bak`. Later saves keep that first backup, so it always holds the
  untouched original. **Tools > Restore original files** in the GUI puts backups back.
- `f:save(path)` writes to another file and makes no backup.
- Saving keeps the file's container: a bundle that was XCTD-compressed is written back
  inside an XCTD wrapper, stored uncompressed, the same way the GUI saves bundles.
- `nut.write` never makes backups.
- **Open GUI tabs don't reload** when a script changes their files. Close and reopen
  the tab afterwards, or a later Ctrl+S in that tab will overwrite the script's change.
- Nothing is written until you save. A script that fails halfway leaves its unsaved
  changes behind in memory, and the files untouched.

## Recipes

### What is in a bundle

```lua
local f = nut.open("Bundle/4f/685374")
local sections = {}
for _, s in ipairs(f:symbols()) do
  sections[s.section] = (sections[s.section] or 0) + s.size
end
for name, bytes in pairs(sections) do print(name, bytes) end
print("models:", #f:models(), "textures:", #f:textures())
```

### Find every bundle holding a model

```lua
local want = arg[1] or "aid_model_banjox_actor_mumbo"
for _, name in ipairs(nut.list("Bundle/4f")) do
  if not name:find("%.bak$") then
    local ok, f = pcall(nut.open, "Bundle/4f/" .. name)
    if ok and f:find(want, ".data") then print(name) end
  end
end
```

This decompresses every bundle, which takes a few minutes. The GUI's browser finds the
same answer instantly from its index.

### Recolour a model and save

```lua
local path, model = "Bundle/4f/685374", "banjox_actor_mumbo"
local f = nut.open(path)
for _, c in ipairs(f:colours(model)) do
  print(c.material, c.constant, string.format("%.2f %.2f %.2f", c.r, c.g, c.b), c.uses)
end
local n = f:set_colour(model, "blinn11", "velvetColour", 0.8, 0.1, 0.1)
if n == 0 then error("no such material colour") end
print("saved", f:save())   -- in place; the original is kept as 685374.bak
```

### Export every model in a bundle as FBX

```lua
local bundle = nut.assets .. "/Bundle/4f/685374"
local out = nut.script_dir .. "/fbx"
local f = nut.open(bundle)
for _, m in ipairs(f:models()) do
  local code, text = nut.run("model-fbx", bundle, m, out .. "/" .. m .. ".fbx")
  print(code == 0 and "ok  " or "FAIL", m)
  if code ~= 0 then print(text) end
end
```

### Edit game text

Strings live in loctext files. English ones are loose assets under `Debug/`, and other
languages are in `loctext/<language>/`. This replaces a word in every string of one file:

```lua
local id = nut.hash("aid_loctext_banjox_actor_hitdialogs")
local file = nut.assets .. string.format("/Debug/%02x/%02x/%02x/%02x", id >> 24, (id >> 16) & 255, (id >> 8) & 255, id & 255)
local json = nut.script_dir .. "/strings.json"
assert(nut.run("text-export", file, json) == 0)
local text = nut.read(json):gsub("Banjo", "Bonjo")
nut.write(json, text)
local code, out = nut.run("text-import", file, json)
print(code, out)
```

### List the longest sounds

```lua
local f = nut.open("Bundle/50/685374")   -- all 420 wave banks; about 700 MB to read
local all = {}
for i, e in ipairs(f:entries()) do
  local bank = nut.wavebank(f:entry(i))
  if bank then
    for _, w in ipairs(bank.waves) do all[#all + 1] = {bank.name, w.name, w.seconds, w.loop_length > 0} end
  end
end
table.sort(all, function(a, b) return a[3] > b[3] end)
for k = 1, 10 do
  local s = all[k]
  print(string.format("%-28s %-10s %6.1f s%s", s[1], s[2], s[3], s[4] and "  (loops)" or ""))
end
```

### Export one bank as WAV files

```lua
local code, out = nut.run("wave-export", nut.assets .. "/Bundle/50/685374",
                          nut.script_dir .. "/audio", "--wav", "--name", "DialogJamJars")
print(out)
```

### Read binary data

A bundle's last symbol, `manifest`, lists the assets the bundle provides. Its entry
count is a big-endian u32 at byte offset 0x0C:

```lua
local f = nut.open("Bundle/4f/685374")
local data = f:read(f:find("manifest"))
local count = string.unpack(">I4", data, 0x0C + 1)
print("manifest entries:", count)
```

### Patch bytes and save a copy

How to change bytes inside a symbol. The value here is only an illustration:

```lua
local f = nut.open("Bundle/4f/685374")
local i = f:find("aid_model_banjox_actor_mumbo", ".data")
local d = f:read(i)
-- Bytes 16-19 (counted from 0) are Lua positions 17-20: keep 1-16, put 4 new bytes, keep 21 on.
d = d:sub(1, 16) .. string.pack(">I4", 0) .. d:sub(21)
f:write(i, d)
f:save(nut.script_dir .. "/685374.patched")   -- a copy: the game files stay untouched
```

## Performance

- `nut.open` reads and decompresses the whole file, so open a file once and reuse the
  File. `Bundle/50/685374`, the stream bundle with every wave bank, is about 700 MB and
  takes tens of seconds.
- In a loop over many files, keep each File in a `local` inside the loop (or use
  `<close>` or `f:close()`), so finished ones can be freed.
- Stop is checked every 10,000 Lua instructions, so loops stop promptly, but a running
  `nut.run` command or `nut.open` finishes first.
- In the GUI, scripts run on a background thread, so the window stays responsive.

## Limits

- No access to the GUI: scripts can't open tabs, select things or draw.
- Textures are reachable only through commands (`tex-export`, `tex-import`). There is no
  pixel API.
- Models are summarised (`f:model`) and their colours editable. Their geometry is changed
  through OBJ files (`f:import_obj`, `model-export`), not vertex by vertex.
- Audio is read-only: `nut.wavebank` describes waves, `wave-export` writes them out, and
  wave banks can only be replaced whole (`f:set_entry`).
- Every script starts fresh: nothing carries over between runs.
