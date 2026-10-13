# Dova Stash

Auto-sorting loot storage for **any** Skyrim player home: vanilla, Creation Club or mod-added.
Point at a chest, press a key, and your haul goes where it belongs. Per-home **templates** mean popular
homes work with zero setup.

An SKSE plugin built on CommonLibSSE-NG with Address Library. One DLL covers SE 1.5.97, AE 1.6.x and VR.
No ESP is needed.

> Status: **0.2.4, pre-release.** It builds and has unit tests, but in-game verification is in progress.

## Features
- **Any home:** eligibility comes from runtime safety checks (ownership, respawning containers), not a whitelist.
- **Category sorting:** 37 default categories defined in JSON, driven by keywords, so items distributed by KID sort correctly.
- **Learns as you go:** put an item in a chest once, and that chest becomes its home.
- **Fills your displays:** turn a weapon rack, mannequin or item plaque (dragon priest masks, claws, Elder
  Scrolls) on with the hotkey, pick the weapon or armor categories it takes, and Deposit puts your best
  matching gear on it, as if placed by hand. Works in any home.
- **Dresses your mannequins:** each wears your best matching armor set, or one you pick from a searchable
  index of every set in your load order, modded sets included.
- **Materials at the workbench:** at a home's forge, smelter, tanning rack, cooking pot, alchemy lab or
  enchanter, the materials stored in the home come to you, and what you don't use goes back afterwards.
  Never what you just crafted; modded recipes and materials work too.
- **Homes over several cells:** add the cellar or the grounds outside to a home (walk out of it, press the
  hotkey: *Add this area*, give it a name), so the forge outside brings what is stored inside and Deposit
  works in every area. Existing setups and templates, cellar templates included, work exactly as before.
- **Templates:** a ready-made setup per home, as shareable JSON. 22 are included (vanilla, Hearthfire and
  Creation Club homes). Save your own from inside the game (hotkey menu → Save as template...), learned
  items included, to share it or to get the setup back after a reset. A template can also pre-define the
  home's racks and mannequins, down to the exact weapon or armor set, even from other mods: a missing mod's
  items are skipped (one line in the log) and the rest of the template still works.
- **Setups backed up outside Data:** every change to a home's setup is also copied to
  `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups`, which MO2's overwrite cleanup never
  touches. Enter a home whose setup is gone and the hotkey offers *Restore your last setup*.
- **Never lose an item:** it preserves tempering, enchantments, names and stolen flags, and never touches
  quest, equipped or favorited items, or script tokens.
- **Looks like your game, not like a mod:** in-world menus are UIExtensions lists, chests show their role
  in the activate prompt, QuickLoot IE shows what a chest holds and where the selected item goes, and
  icons above containers and in the lists use your own SkyUI/I4 icons. Any skin works; the only Dova Stash
  art in game is the small logo above the hotkey lists.
- **Editor:** the SKSE Menu Framework panel for full editing; native message boxes as the fallback and on VR.

## Documentation
| Doc | Covers |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Specification and architecture |
| [docs/BUILDING.md](docs/BUILDING.md) | Building and deploying |
| [docs/TESTING.md](docs/TESTING.md) | In-game test script |
| [tools/README.md](tools/README.md) | Template authoring with `esm_dump.py` |
| [docs/EDITOR.md](docs/EDITOR.md) | Dova Stash Editor (in development): making templates outside the game |
| [presskit/](presskit/) | Nexus Mods page copy and art (press kit) |
| [CHANGELOG.md](CHANGELOG.md) | Changes per version |

## Building
```
git clone --recursive <this repo>
cmake --preset plugin && cmake --build build/plugin --config Release --target DovaStash
```
See [docs/BUILDING.md](docs/BUILDING.md) for full instructions. You need VS 2022 and vcpkg with `VCPKG_ROOT` set.

## Dova Stash Editor (in development)
A standalone program for making home templates without the game: pick the home's mod from your MO2 list
(by name or Nexus mod id), and it finds the player homes in the mod's plugins and shows each cell in a Creation
Kit style 3D view and as a floor plan, with its containers, racks, mannequins, plaques and doors. Click one to
make it the master, give it categories, ignore it or use a display, then save the template into a "Dova Stash
Templates" mod. Run it as an MO2 executable. See [docs/EDITOR.md](docs/EDITOR.md) (§9: how to try it); build
it with `cmake --preset editor && cmake --build --preset editor-release`.

## License
GPL-3.0-or-later (see [LICENSE](LICENSE)), as required by CommonLibSSE-NG. The vendored SKSE Menu
Framework API header is LGPL-2.1.
