# Dova Stash

Auto-sorting loot storage for **any** Skyrim player home: vanilla, Creation Club or mod-added.
Point at a chest, press a key, and your haul goes where it belongs. Per-home **templates** mean popular
homes work with zero setup.

An SKSE plugin built on CommonLibSSE-NG with Address Library. One DLL covers SE 1.5.97, AE 1.6.x and VR (experimental: message boxes, keyboard hotkey).
No ESP is needed.

> Status: **0.2.4, pre-release.** It builds and has unit tests, but in-game verification is in progress.

## Features
- **Any home:** eligibility comes from runtime safety checks (ownership, respawning containers), not a whitelist.
- **Category sorting:** 39 default categories in 7 groups (the log counts 46: groups included) defined in JSON, driven by keywords, so items distributed by KID sort correctly.
- **Learns as you go:** put an item in a chest once, and that chest becomes its home. Enchanted variants
  count as the item they are made from; a learn can be undone for a few seconds; and after a few learned
  items of one kind, Dova Stash asks once whether the whole category should go there.
- **Fills your displays:** turn a weapon rack, mannequin, bookshelf or item plaque (dragon priest masks, claws,
  Elder Scrolls) on with the hotkey, pick the weapon, armor or book categories it takes, and Deposit puts your
  best matching gear and books on it, as if placed by hand. A bookshelf gets as many books as it holds, never
  more. Works in any home.
- **Dresses your mannequins:** each wears your best matching armor set, or one you pick from a searchable
  index of every set in your load order, modded sets included.
- **Restocks you:** mark a keep quota *Restock* ("10 health potions", "50 steel arrows") and every Deposit
  tops you back up to it from the home's containers, the most valuable copies first and never more; *Restock*
  is in the hotkey menu too.
- **Materials at the workbench:** at a home's forge, smelter, tanning rack, cooking pot or alchemy lab, you
  craft from what the home's containers hold, without moving anything when you sit down (no lag with big
  stores); each craft takes only what you lack. Enchanters bring your soul gems. Modded recipes work too.
- **A sell chest for your junk:** make any container the sell chest (Clutter goes there); nothing is sold until
  you choose *Sell now* and confirm the total, at a merchant's price; *Buy back* undoes the last sale; stolen,
  improved, quest, worn or favorited items, soul gems you filled yourself, potions you made and what your keep rules keep never sell.
- **Homes over several cells:** add the cellar or the grounds outside to a home (walk out of it, press the
  hotkey: *Add this area*, give it a name), so the forge outside crafts from what is stored inside and Deposit
  works in every area. The multi-cell homes (Hearthfire, Tundra Homestead, Myrwatch) ship as one template each,
  cellar, gallery and grounds included, as do the mod homes over several cells. A cellar set up by its old template
  joins the home when you apply the new one.
- **Templates:** a ready-made setup per home, as shareable JSON. 32 are included: 19 for every vanilla,
  Hearthfire and Creation Club home (the multi-cell homes as one template each, cellar, gallery and grounds
  included) and 13 for mod homes (Pinegrove Lodge, Bal Ruhn, Wuth Rein, Ebongrove, Hydda, Black Fur Refuge,
  Sicarius' Refuge, Niflholm, Tel Jerdein, Antennaria, Winter Cove, and Unique Vampire Dens' Lost Hall and Secret
  Cellar). Most shipped templates also fill the home's racks, mannequins and bookshelves; turn on any others with K. Save your own from
  inside the game (hotkey menu → Advanced... → Save as template...), learned
  items included, to share it or to get the setup back after a reset. A template can also pre-define the
  home's racks, mannequins and bookshelves, down to the exact weapon or armor set, even from other mods: a missing mod's
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
- **In-game panel:** the SKSE Menu Framework panel for full editing, four pages: This home, Displays, Settings, About; native message boxes as the fallback and on VR.

## Install and use
- **Requires** SKSE and Address Library for SKSE Plugins (VR: SKSEVR and VR Address Library). Optional: SKSE Menu
  Framework (settings page and panel), UIExtensions (in-world lists), QuickLoot IE.
- Install with your mod manager and launch through SKSE. There is no ESP, so it takes no load order slot.
- Walk into a home with a template and it is set up. Anywhere else, look at a large chest, press **K** and make
  it the master. Then press **K** and choose **Deposit**.
- Controller: no button by default; bind one under Settings > Hotkey > Gamepad.
- Settings: SKSE Menu Framework > Dova Stash > Settings, or `Data/SKSE/Plugins/DovaStash.ini`. Every change
  applies without a restart.

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
cmake --preset plugin && cmake --build --preset plugin-release
```
Or in one command: `cmake --workflow --preset plugin` (also `core-tests`: configure, build and test; and `editor`).
See [docs/BUILDING.md](docs/BUILDING.md) for full instructions. You need VS 2022 and vcpkg with `VCPKG_ROOT` set.

## Dova Stash Editor (in development)
A standalone program for making home templates without the game, in one window: a rail lists every player home of
your MO2 load order under its mod (search by name or Nexus mod id); a click opens the home in a Creation Kit style
3D view or as a floor plan, with a draft already made from the home itself. Click a container, rack or mannequin to
make it the master, give it categories, ignore it or use a display, review the draft's suggestions, check, then save
the template into a "Dova Stash Templates" mod. Run it as an MO2 executable. See [docs/EDITOR.md](docs/EDITOR.md) (§9: how to try it); build
it with `cmake --preset editor && cmake --build --preset editor-release`.

## License
GPL-3.0-or-later (see [LICENSE](LICENSE)), as required by CommonLibSSE-NG. The vendored SKSE Menu
Framework API header is LGPL-2.1.
