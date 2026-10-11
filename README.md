# Dova Stash

Auto-sorting loot storage for **any** Skyrim player home: vanilla, Creation Club or mod-added.
Point at a chest, press a key, and your haul goes where it belongs. Per-home **templates** mean popular
homes work with zero setup.

An SKSE plugin built on CommonLibSSE-NG with Address Library. One DLL covers SE 1.5.97, AE 1.6.x and VR.
No ESP is needed.

> Status: **0.2.2, pre-release.** It builds and has unit tests, but in-game verification is in progress.

## Features
- **Any home:** eligibility comes from runtime safety checks (ownership, respawning containers), not a whitelist.
- **Category sorting:** 37 default categories defined in JSON, driven by keywords, so items distributed by KID sort correctly.
- **Learns as you go:** put an item in a chest once, and that chest becomes its home.
- **Templates:** shareable JSON layouts per home. 22 are included (vanilla, Hearthfire and Creation Club homes).
  You can export your own from inside the game.
- **Never lose an item:** it preserves tempering, enchantments, names and stolen flags, and never touches
  quest, equipped or favorited items, or script tokens.
- **Looks like your game, not like a mod:** in-world menus are UIExtensions lists, chests show their role
  in the activate prompt, QuickLoot IE shows what a chest holds and where the selected item goes, and
  icons above containers use your own SkyUI/I4 icons. Any skin works; nothing is drawn with Dova Stash art.
- **Editor:** the SKSE Menu Framework panel for full editing; native message boxes as the fallback and on VR.

## Documentation
| Doc | Covers |
|---|---|
| [docs/DESIGN.md](docs/DESIGN.md) | Specification and architecture |
| [docs/BUILDING.md](docs/BUILDING.md) | Building and deploying |
| [docs/TESTING.md](docs/TESTING.md) | In-game test script |
| [tools/README.md](tools/README.md) | Template authoring with `esm_dump.py` |
| [nexus/](nexus/) | Nexus Mods page copy and art |

## Building
```
git clone --recursive <this repo>
cmake --preset plugin && cmake --build build/plugin --config Release --target DovaStash
```
See [docs/BUILDING.md](docs/BUILDING.md) for full instructions. You need VS 2022 and vcpkg with `VCPKG_ROOT` set.

## License
GPL-3.0-or-later (see [LICENSE](LICENSE)), as required by CommonLibSSE-NG. The vendored SKSE Menu
Framework API header is LGPL-2.1.
