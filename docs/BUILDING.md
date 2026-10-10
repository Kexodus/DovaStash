# Building Dova Stash

## Prerequisites

| Tool | Notes |
|---|---|
| VS 2022 Build Tools (MSVC v143, "Desktop development with C++") | Generator `Visual Studio 17 2022`. |
| CMake ≥ 3.25 | On `PATH`. |
| vcpkg | At `E:\vcpkg`. `VCPKG_ROOT` must be set **in the shell that runs cmake** (the presets read `$env{VCPKG_ROOT}`). |
| Git submodules | `git submodule update --init --recursive` (CommonLibSSE-NG and its `extern/openvr`). |

```powershell
$env:VCPKG_ROOT = 'E:\vcpkg'          # Git Bash: export VCPKG_ROOT=E:/vcpkg
```

## Plugin (DovaStash.dll, SE + AE + VR)

```powershell
cmake --preset plugin
cmake --build build/plugin --config Release --target DovaStash --parallel
# or: cmake --build --preset plugin-release   (this also builds core + tests)
```

Output: `build/plugin/src/plugin/Release/DovaStash.dll` (+ `.pdb`).

The `--target DovaStash` build also builds and links `dova_core` (the plugin consumes it since M1),
but not the unit tests. Run the `core-tests` preset (below) to test core changes.

### Deploy to MO2

A post-build step (`src/plugin/cmake/Deploy.cmake`) runs every time the DLL relinks. It copies files into
`DOVASTASH_DEPLOY_DIR`, which defaults to `E:/modding/Kexodus Skyrim/mods/Dova Stash`:

| Source | Destination (under the mod folder) |
|---|---|
| `DovaStash.dll`, `DovaStash.pdb` | `SKSE/Plugins/` |
| `src/plugin/DovaStash.ini.default` | `SKSE/Plugins/DovaStash.ini`, **only if that file doesn't exist** (user edits are never overwritten) |
| `dist/Data/SKSE/Plugins/DovaStash/*` | `SKSE/Plugins/DovaStash/` (skipped when missing or empty) |

Options:

- `-DDOVASTASH_DEPLOY=OFF` turns off the post-build copy.
- `-DDOVASTASH_DEPLOY_DIR=<path>` deploys to a different mod folder.
- If you only changed `dist/` JSON, redeploy without relinking:
  `cmake --build build/plugin --config Release --target DovaStash_deploy`

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

## Core logic + unit tests (no CommonLib)

```powershell
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

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
