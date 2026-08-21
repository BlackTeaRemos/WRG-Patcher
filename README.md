# WRG-Patcher

32-bit proxy `version.dll` that instruments Wargame: Red Dragon (x86) to load
mods without touching game files.

## Subsystems

1. **Data mods** - declarative `patcher.toml` manifests: whole-file pack redirects, byte splices into packs, per-version gates. No code, no compiler.
2. **Code mods** - plugin DLLs linking `include/wrg_patcher.h`, driven by the stable C API (`WrgApi`): redirects, splices, import hooks, memory read/write, module enumeration, version queries, event dispatch. Disabled in release builds.
3. **Live control** - opt-in named-pipe IPC (`\\.\pipe\wrd_patcher`) for runtime mount/reload via external tools.

## Load mechanism

The game imports `version.dll` from its own folder before `main()`. This proxy.

## Installation

Grab `version.dll` and `install.bat` from the
[latest release](https://github.com/BlackTeaRemos/WRG-Patcher/releases/latest).

1. Copy both files into your Wargame: Red Dragon folder
2. Double-click `install.bat`.

The script finds the game (the folder it sits in, or via the Steam registry keys), copies the system `version.dll` to `version_real.dll` so the proxy can forward the real version exports, and creates the `mods\` folder. Launch the game normally afterward.

If the script cannot locate the game automatically, pass the folder:

```
install.bat "D:\Games\Wargame Red Dragon"
```

To uninstall, delete `version.dll` from the game folder

## Installing mods

1. Put each mod in its own subfolder under `mods\`: `mods\<ModName>\`.
2. Control which mods load, and in what order, with `mods\load_order.txt`, one mod folder name per line, `#` for comments

To build mods, use the
[WGRD Mod Toolkit](https://github.com/BlackTeaRemos/WGRD-Mod-Toolkit).

```
Wargame Red Dragon\
  WarGame3.exe
  version.dll
  version_real.dll
  mods\
    load_order.txt
    MyMod\
      patcher.toml
      packs\ZZ_3a.dat      # mod asset files, referenced by the manifest
```

### Manifest format (`patcher.toml`)

A manifest is a sequence of `[[redirect]]` and `[[overlay]]` blocks. An optional top-level `requires_version` gates the whole file against the detected game build.

```toml
requires_version = "..."          # optional; skip all blocks on mismatch

# REDIRECT: swap every open of `tail` for a whole replacement file.
# Whole-file. Used for mesh and NDF packs.
[[redirect]]
tail = "48574\\ZZ_3a.dat"         # path tail the engine opens
file = "packs\\ZZ_3a.dat"         # mod-relative replacement file

# OVERLAY: splice bytes into the engine's reads of the real pack.
# Leaves the original pack open (preserves engine identity/hash checks).
# Used for texture packs where an identity hash gates engine state.
[[overlay]]
tail   = "48574\\textures.dat"
file   = "patch\\block.bin"       # mod-relative bytes to splice in
offset = 4096                     # absolute byte offset in the pack
# -- OR, instead of offset, resolve at runtime (survives game repacks): --
inner  = "some/inner/asset.tgv"   # host resolves current offset via edat trie
```

## Building

x86 only. Requires Visual Studio 2026 with C++26 (`/std:c++latest`).

Set two environment variables, then run the staging script:

```
set VCVARS=<path>\VC\Auxiliary\Build\vcvars32.bat
set GAME=<path>\Wargame Red Dragon
stage.bat
```

`stage.bat` compiles the core translation units into `version.dll` and copies it into `%GAME%`. For a dev build with plugin loading enabled, use `build_msvc.ps1`.

Install the built `version.dll` next to `WarGame3.exe` alongside `version_real.dll` (a copy of the system `System32\version.dll`).

## License

Copyright 2026 BlackTeaRemos (https://github.com/BlackTeaRemos).
Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
