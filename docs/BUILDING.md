# Building Dova Stash

## Prerequisites

| Tool | Notes |
|---|---|
| VS 2022 Build Tools (MSVC v143, "Desktop development with C++") | Generator `Visual Studio 17 2022`. |
| CMake ≥ 3.25 | On `PATH`. |
| vcpkg | At `E:\vcpkg`. `VCPKG_ROOT` must be set **in the shell that runs cmake** (the presets read `$env{VCPKG_ROOT}`); without it configure stops with *Set VCPKG_ROOT to your vcpkg folder (docs/BUILDING.md, Prerequisites)*. |
| Git submodules | `git submodule update --init --recursive` (CommonLibSSE-NG and its `extern/openvr`). |

```powershell
$env:VCPKG_ROOT = 'E:\vcpkg'          # Git Bash: export VCPKG_ROOT=E:/vcpkg
```

## Plugin (DovaStash.dll, SE + AE + VR)

```powershell
cmake --preset plugin
cmake --build build/plugin --config Release --target DovaStash --parallel
# or: cmake --build --preset plugin-release   (this also builds core + tests)
# or, configure and build in one: cmake --workflow --preset plugin
```

Output: `build/plugin/src/plugin/Release/DovaStash.dll` (+ `.pdb`).

The `--target DovaStash` build also builds and links `dova_core` (the plugin consumes it since M1),
but not the unit tests. Run the `core-tests` preset (below) to test core changes.

### Deploy to MO2

A post-build step (`src/plugin/cmake/Deploy.cmake`) runs every time the DLL relinks. It copies files into
`DOVASTASH_DEPLOY_DIR`: the environment variable of that name when it is set, else
`E:/modding/Kexodus Skyrim/mods/Dova Stash`. It deploys only when the environment variable is set or that folder's
parent (the MO2 `mods` folder) exists; otherwise configure says *Deploy off: &lt;dir&gt; not found (set
DOVASTASH_DEPLOY_DIR)* and the build works without the developer's drive. The editor's deploy follows the same rules:
the plugin build no longer copies `Tools/DovaStashEditor.exe`; the editor build (preset `editor-release`) deploys the
`.exe` and `.com` itself, so a plugin build never replaces a newer editor with an older one.

| Source | Destination (under the mod folder) |
|---|---|
| `DovaStash.dll`, `DovaStash.pdb` | `SKSE/Plugins/` |
| `src/plugin/DovaStash.ini.default` | `SKSE/Plugins/DovaStash.ini.default` (as the release zip has it), and `SKSE/Plugins/DovaStash.ini`: created if missing; otherwise kept, and any `[Section]` it lacks is appended from the default (user values are never overwritten) |
| `dist/Data/SKSE/Plugins/DovaStash/*` | `SKSE/Plugins/DovaStash/`, an exact mirror: files `dist` no longer has are removed; `Layouts/`, `Backups/`, `Templates/User/` and `AutoTest/` are never touched |
| `dist/Data/Interface/*` | `Interface/` (the hotkey list menu's logo; `Interface/DovaStash/` is mirrored exactly) |

Options:

- `-DDOVASTASH_DEPLOY=OFF` turns off the post-build copy.
- `-DDOVASTASH_DEPLOY_DIR=<path>` deploys to a different mod folder.
- If you only changed `dist/` JSON, redeploy without relinking:
  `cmake --build build/plugin --config Release --target DovaStash_deploy`

The deploy warns about top-level entries in the mod folder that no build writes: today `root/` (the `stress_loot`
output `ds_*.txt`, `ds_manifest.csv`), which Root Builder copies into the game folder. Remove it: `cmake -E rm -rf "<DOVASTASH_DEPLOY_DIR>/root"` (the deploy
prints this command; `tools/stress_loot.py` now writes to the "dovastash inventory testing" mod).

### Release

Build the editor (`cmake --build --preset editor-release`) and the guide PDF (`presskit/guide`) first. Then
`cmake --build --preset plugin-release --target DovaStash_package` stages `build/release/Dova Stash/`
(`DovaStash.dll`/`.pdb`, `DovaStash.ini.default`, the `dist` data, `Interface`, `Tools/DovaStashEditor.exe`/`.com`
from `build/editor` and `Tools/DovaStashEditor-UserGuide.pdf`) and zips its contents to `build/release/DovaStash-<version>.zip` (`SKSE/`, `Interface/` and `Tools/` at the top, no
wrapping folder, so it maps onto `Data/`).
It stops when the editor or the guide is missing, unless configured with `-DPACKAGE_ALLOW_PARTIAL=ON` (test zips
only). The zip ships no `DovaStash.ini`: the plugin creates it from `DovaStash.ini.default` on the first start, so
an update never overwrites the player's settings. The stage is made fresh each time, so nothing a test session
wrote gets in. With the guide built, a release is two commands in this order: `cmake --build --preset editor-release`,
then `cmake --build --preset plugin-release --target DovaStash_package` (a CMake workflow preset takes one configure
preset, so there is no single `release` preset).

At runtime the log is written to `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash.log`
(on VR the folder is `Skyrim VR`; on GOG it is `Skyrim Special Edition GOG`). On a good load it contains
`Dova Stash loaded`.

### Debug builds

```powershell
cmake --preset plugin-source
cmake --build --preset plugin-debug
```

The `plugin` preset turns on CommonLib's prebuilt-bundle opt-in
(`COMMONLIB_PREBUILT=ON`, `COMMONLIB_PREBUILT_MULTICONFIG=ON`), and that bundle covers Release only.
`plugin-source` always compiles CommonLib from source, so it can build Debug.

### Automated in-game tests

`pwsh -File tools\autotest\run.ps1` starts Skyrim unattended in the MO2 profile *Dova Stash AutoTest*, runs the
test plans in `tests/ingame` and prints the results (exit code 1 on a failure). The harness is part of
`DovaStash.dll` (`src/plugin/AutoTest/`) but does nothing unless that profile's *Dova Stash AutoTest* mod provides a
plan. Set the profile up once with `pwsh -File tests\ingame\install.ps1 -Profile`. Options:

- `-Build`: builds the plugin first (`cmake --build --preset plugin-release --target DovaStash`, after
  `cmake --preset plugin` when `build/plugin` is not configured yet); it prints where the build deploys, as
  configured above.
- `-Dll <path>`: tests that DLL (copied into the AutoTest mod, so it overrides the Dova Stash mod's in that profile
  only). Use it to test a branch without deploying it to the mod folder (configure that build with
  `-DDOVASTASH_DEPLOY=OFF`). Without it, the profile loads the Dova Stash mod's DLL.

`run.ps1` and `install.ps1` take `$env:DOVASTASH_MO2` as their `-Mo2` default (the MO2 instance folder).

Details, what is covered and what stays manual: docs/TESTING.md §9.

### Data check

Check the shipped categories and templates against the game data before a release or after editing `dist/`
(no game needed; Python 3, stdlib only):

```powershell
python -I tools/esm_dump.py --profile "Dova Stash AutoTest" check dist/Data/SKSE/Plugins/DovaStash
```

The MO2 instance comes from `--mo2 <instance folder>`, else `$env:DOVASTASH_MO2`, else the developer's instance
(`E:\modding\Kexodus Skyrim`), so the command works in a fresh shell there; the profile supplies the home mods
the `Templates/Mods` files need. Expected output and the OCF set's check: docs/TESTING.md §0; options:
`tools/README.md`.

## Core logic + unit tests (no CommonLib)

```powershell
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
# or all three in one: cmake --workflow --preset core-tests
```

## Dova Stash Editor (DovaStashEditor.exe, no CommonLib)

The standalone template editor (docs/EDITOR.md). Its own preset and build directory (`build/editor`), so the
plugin and test builds are unaffected; the root option `DOVASTASH_BUILD_EDITOR` is off everywhere else.
vcpkg feature `editor`: Dear ImGui (`dx11-binding`, `win32-binding`), lz4 and zlib; it links `dova_core`. The 3D view
needs nothing more: its NIF, DDS and BSA readers are the editor's own, and it links Windows' `d3d11`, `dxgi`,
`d3dcompiler` and `windowscodecs` (PNG screenshots).

```powershell
cmake --preset editor
cmake --build --preset editor-release        # or editor-debug
ctest --preset editor                        # DovaStashEditorTests
# or, configure, build and test in one: cmake --workflow --preset editor
```

The editor preset calls `enable_testing()` (root `CMakeLists.txt`). Output: `build/editor/editor/Release/DovaStashEditor.exe`
and `DovaStashEditor.com`, copied into `<DOVASTASH_DEPLOY_DIR>/Tools/` (the same option and folder as the plugin; `-DDOVASTASH_DEPLOY=OFF` to skip). Only the editor build copies it, when the exe relinks; if the editor was running, close it and run `cmake --build --preset editor-release --target DovaStashEditor_deploy`. Without arguments it
opens the window (the start panel); commands print to the console instead. The window remembers the instance, the
author, each home's to-do stage and its layout in `%LOCALAPPDATA%\DovaStashEditor\state.json`; set
`DOVASTASH_EDITOR_STATE` to another file for test runs and screenshots:

```powershell
$exe = 'build\editor\editor\Release\DovaStashEditor.exe'
& $exe --mo2 'E:\modding\Kexodus Skyrim' --list-mods whitepeak         # mods, Nexus ids, plugins
& $exe --mo2 'E:\modding\Kexodus Skyrim' --mod 92177 --homes           # player homes in a mod's plugins
& $exe --mo2 'E:\modding\Kexodus Skyrim' --cell WhiterunBreezehome     # containers, displays, doors
& $exe --mo2 'E:\modding\Kexodus Skyrim' --cell WhiterunBreezehome --draft > Breezehome.json
& $exe --mo2 'E:\modding\Kexodus Skyrim' --cell WhiterunBreezehome --draft --save-to C:\Temp\Templates --author Me  # as the window saves
& $exe --mo2 'E:\modding\Kexodus Skyrim' --gui --home 'Whitepeak Tower' --screenshot shot.png
& $exe --mo2 'E:\modding\Kexodus Skyrim' --gui --home 'Whitepeak Tower' --inspector template --window 1280x720 --screenshot tpl.png

# Bulk drafts and where items go (docs/EDITOR.md §3.4, §3.5): the whole active load order
& $exe --mo2 'E:\modding\Kexodus Skyrim' --draft-all                                  # every home: status, master + reason, suggestions
& $exe --mo2 'E:\modding\Kexodus Skyrim' --draft-all --only Aldamar --verbose         # one home, every reason
& $exe --mo2 'E:\modding\Kexodus Skyrim' --draft-all --compare --base-game            # accuracy against the shipped templates
& $exe --mo2 'E:\modding\Kexodus Skyrim' --draft-all --save-to C:\Temp\Drafts --author Me   # save the drafts of homes without a template as auto-drafts (never over someone's)
& $exe --mo2 'E:\modding\Kexodus Skyrim' --preview WhiterunBreezehome --sample haul   # per container: what it would receive
& $exe --mo2 'E:\modding\Kexodus Skyrim' --gui --screenshot start.png                 # the start panel
& $exe --mo2 'E:\modding\Kexodus Skyrim' --gui --drafts --home Breezehome --screenshot drafts.png   # Draft all homes, Breezehome's Draft panel
& $exe --mo2 'E:\modding\Kexodus Skyrim' --gui --home Breezehome --tab preview --screenshot preview.png
& $exe --mo2 'E:\modding\Kexodus Skyrim' --gui --home Breezehome --tab 3d --colours fill --popover colours --screenshot fill.png
& $exe --mo2 'E:\modding\Kexodus Skyrim' --gui --home Breezehome --palette --screenshot palette.png    # the command palette

# The 3D view without a window (docs/EDITOR.md §4.5): an overview, a framed selection, a camera of your own
& $exe --mo2 'E:\modding\Kexodus Skyrim' --mod 92177 --render-cell bcr58WhiteTower --screenshot wt.png
& $exe --mo2 'E:\modding\Kexodus Skyrim' --mod 92177 --render-cell bcr58WhiteTower --select 'Whitepeak Tower.esp|0x00230B30' --frame --screenshot satchel.png
& $exe --mo2 'E:\modding\Kexodus Skyrim' --render-cell WhiterunBreezehome --camera -600,-100,110,20,-10 --screenshot inside.png
& $exe --mo2 'E:\modding\Kexodus Skyrim' --render-cell 'Skyrim.esm|0x0001A27A' --screenshot whiterun.png   # exterior: terrain + 8 cells around
& $exe --mo2 'E:\modding\Kexodus Skyrim' --render-cell WhiterunBreezehome --verbose --pick 800,450 --screenshot b.png  # boxes, bounds check, frame time, a click
& $exe --mo2 'E:\modding\Kexodus Skyrim' --nif-info 'clutter\upperclass\upperchest01.nif'                 # how a mesh is read
```

It is a Windows-subsystem program: from PowerShell or cmd, pipe the output (`| Out-Host`, `| more`) to wait
for it; Git Bash waits by itself. To run it from MO2, add it under *Tools > Executables*; it finds the
instance from MO2 and reads the merged Data folder (docs/EDITOR.md §4.8; step by step, with the controls and a
short test: docs/EDITOR.md §9). `--render-cell` needs a Direct3D 11 GPU (it falls back to the WARP software
device) but no window, so it also works over remote sessions. `--screenshot` with `--gui` also works without a
desktop: in session 0 it draws off-screen at the `--window` size (a window without `--screenshot` exits 3 there). Errors exit 1 with no message box (stderr and
the log), and `--timeout <s>` (default 180) saves what it has, logs *screenshot timed out waiting for &lt;job&gt;* and
exits 4.

## Timings (16-thread machine, cold)

| Step | Time |
|---|---|
| `cmake --preset plugin`, first run (vcpkg builds spdlog, fmt, directxtk, simpleini, …, and downloads the 133 MB prebuilt bundle) | ~1 min |
| First `DovaStash` build (compiles all of CommonLibSSE-NG from source with `/MP`, then LTCG link) | ~2.5 min |
| Incremental plugin rebuild | ~15 s |

## How CommonLibSSE-NG is consumed

- **Submodule:** `extern/CommonLibSSE-NG` → `alandtse/CommonLibVR`. `.gitmodules` tracks branch `ng`, but the
  submodule commit is **pinned to tag `v11.0.0`**. CommonLib only fetches a prebuilt binary when the
  submodule sits exactly on a clean release tag. `git submodule update --remote` would move it to the
  `ng` tip, which disqualifies the prebuilt bundle (a source build still works).
- **Inclusion:** `add_subdirectory(extern/CommonLibSSE-NG ... SYSTEM EXCLUDE_FROM_ALL)` from
  `src/plugin/CMakeLists.txt`, with `ENABLE_SKYRIM_SE/AE/VR=ON` forced and CommonLib's own tests off.
- **Plugin declaration:** `add_commonlibsse_plugin(DovaStash ... USE_ADDRESS_LIBRARY)` generates
  `build/plugin/src/plugin/__DovaStashPlugin.cpp`, which contains `SKSEPluginInfo(...)`. That expands to
  the `SKSEPlugin_Version` data export (read by AE's SKSE) **and** an `SKSEPlugin_Query` function (needed by
  SE 1.5.97 and SKSEVR). The name and version come from the root `project()`. Check the exports with:

  ```
  dumpbin /exports build\plugin\src\plugin\Release\DovaStash.dll
  → SKSEPlugin_Load, SKSEPlugin_Query, SKSEPlugin_Version
  ```

## Gotchas

1. **The prebuilt CommonLib bundle doesn't link with VS 2022.** The v11.0.0 CMake bundle was built with
   MSVC **19.51** (VS 2026 toolset). VS 2022 Build Tools ship **19.44**. `cmake/Prebuilt.cmake` detects the
   mismatch at configure time, prints `built with a different MSVC toolset ... building from source`, and
   falls back to a source build automatically. The source build is fast enough (see timings), so nothing
   needs to change. If the toolchain moves to VS 2026 (v145), the same preset links the prebuilt
   library with no further changes.
2. **CommonLib doesn't set `/MP`.** Under the Visual Studio generator, its ~1000 translation units would
   otherwise compile one at a time. `src/plugin/CMakeLists.txt` adds `/MP` to the `CommonLibSSE` target
   when it is built from source. With `/MP`, the first build takes about 2.5 minutes. Without it the
   files compile one at a time; CommonLib's own docs estimate about 16 minutes.
3. **CommonLib only defines `add_commonlibsse_plugin()` in prebuilt mode.** For source builds,
   `src/plugin/CMakeLists.txt` includes `extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake` itself.
4. **Git Bash mangles MSBuild switches.** `cmake --build ... -- /m` becomes `M:/`. Use `--parallel`.
5. **SKSE Menu Framework header** (`extern/SKSEMenuFramework/SKSEMenuFramework.h`, vendored from
   QTR-Modding/SKSE-Menu-Framework-3-API @ `1dcb701`, LGPL-2.1):
   - It includes `<windows.h>` and uses `std::filesystem` without including `<filesystem>`. Include it
     **only** from `src/plugin/UI/MenuFramework.cpp` and `src/plugin/UI/Panel/PanelRender.cpp` (as the
     last include), never from a header or the PCH.
   - It is on a `SYSTEM` include path because it emits C4099/C5054 under `/W4`.
   - Call into it only from `kPostLoad` or later. Its function-pointer lookups are cached in function
     statics, so calling during `SKSEPluginLoad`, before the framework DLL is loaded, would cache null
     pointers for good.
