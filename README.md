# WRG-Patcher

WRG-Patcher is a Windows x86 proxy `version.dll` for Wargame: Red Dragon.
It loads enabled mods from the game's `mods/` directory, and provides necessary changes to game memory to make mod features available.
The release configuration supports SDK-generated append mods, data redirects, byte overlays, and additional nations.
Mod-provided DLL plugins are disabled by `WRG_RELEASE` in the release build.

Use the [WGRD Mod Toolkit](https://github.com/BlackTeaRemos/WGRD-Mod-Toolkit)
to author and rebuild mod packs.

## Install the loader

Copy a release `version.dll` beside `WarGame3.exe`, then launch the game normally.
The proxy forwards version-information calls to the Windows system library.

The optional [install script](tools/install.bat) can copy the loader into the game directory, create `mods/`, and provide that fallback library.
Place the script beside the built or downloaded `version.dll`, then run:

```bat
install.bat "D:\Games\Wargame Red Dragon"
```

To remove the loader, close the game and remove its file: `version.dll`.

## Enable mods and set their order

Put each installed mod in a separate directory under `mods/`. Create `mods/load_order.txt` as UTF-8 text with one directory name per line:

```text
# Loaded in this order
MyMod
AnotherMod
```

Use directory names, without quotes or path separators. Blank lines and full-line `#` comments are ignored; duplicate names are ignored.
Later mods have higher replacement priority.

A typical SDK-generated mod looks like this:

```text
Wargame Red Dragon/
  WarGame3.exe
  version.dll
  mods/
    load_order.txt
    MyMod/
      mod.json
      131666/
        NDF_Win.dat
        ZZ_1.dat
        ZZ_2.dat
        ZZ_3a.dat
        ZZ_3b.dat
        ZZ_4.dat
        ZZ_Win.dat
        Data.dat
        DataMap.dat
```

## SDK mod manifest

Append delivery is declared by `"delivery": "append"` in the mod's `mod.json`.
The loader also accepts `wrd_mod.json` for this declaration.

```json
{
  "name": "MyMod",
  "version": "1.0",
  "author": "Mod author",
  "description": "An SDK-authored mod",
  "offset": "M0D1",
  "delivery": "append",
  "nations": [
    { "code": "SPAIN", "voice_country": "US" }
  ],
  "packs": [
    "131666/NDF_Win.dat",
    "131666/ZZ_1.dat",
    "131666/ZZ_2.dat",
    "131666/ZZ_3a.dat",
    "131666/ZZ_3b.dat",
    "131666/ZZ_4.dat",
    "131666/ZZ_Win.dat",
    "131666/Data.dat",
    "131666/DataMap.dat",
    "Maps/WarGame/PC/map_0123456789abcdef0123456789abcdef.dat"
  ],
  "built_from_revision": "131544"
}
```

| Field | Usage |
| --- | --- |
| `delivery` | `append` makes the enabled mod own an additional pack tier |
| `nations` | Optional country declarations, with `code` and a stock `voice_country`; read directly by the release loader |
| `pin_revision` | Optional override for the revision reported by the game  |
| `packs` | Output inventory for SDK and mod-manager consumers |
| `built_from_revision` | Build prove |
| Other metadata | Names, versions, author details, `offset`, and SDK origin markers are retained for authoring and distribution tools |

The SDK writes `nations` into `mod.json` from the mod's authored country definitions.
Omit it or use an empty array when the mod adds no countries. Nation support is
built into `version.dll` and identified by `WrgNationFeatureV2`; no plugin or
separate nation file is required. Names, flags, side, deck rules, and units remain
in the packed game data. Update the loader before repacking a mod that uses this
manifest contract.

## Diagnostics and optional controls

The loader writes `mods/patcher.log` for each launch. Useful entries include
`LOADED`, `BYPASS`, `TIER-OWNER`, `TIER-ADD`, `TIER-NO-PACKS`,
`REVISION-BUMP`, `REVISION-PIN`, `NATIONS`, and `NATIONS-FAIL`.

## Build the release DLL

Use the MSVC x86 environment and `/std:c++latest`. The release configuration has been compiled with Visual Studio 2026. From an x86 Native Tools Command Prompt in the repository root:

```bat
cl /nologo /O2 /LD /EHsc /std:c++latest /D_CRT_SECURE_NO_WARNINGS /DWRG_RELEASE /I include ^
  src\patcher.cpp src\hook.cpp src\redirect.cpp src\overlay.cpp src\overlay_load.cpp ^
  src\manifest.cpp src\edat.cpp src\version_gate.cpp src\version_anchor.cpp ^
  src\plugin.cpp src\ipc.cpp src\mapdir.cpp src\tier.cpp src\revision.cpp ^
  src\version_shim.cpp /Fe:version.dll /link /DEF:src\version.def /OUT:version.dll
```

This produces the core `version.dll`, including nation support, with plugin
loading compiled out.

To build and copy that release DLL to a game installation, use the existing
staging script from a command prompt:

```bat
set "VCVARS=<VisualStudio>\VC\Auxiliary\Build\vcvars32.bat"
set "GAME=D:\Games\Wargame Red Dragon"
stage.bat
```

`stage.bat` sets up the compiler, builds the release DLL, and copies
`version.dll` into `%GAME%`.

## License

Copyright 2026 BlackTeaRemos (https://github.com/BlackTeaRemos).
Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
