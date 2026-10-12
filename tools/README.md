# Dova Stash tools

`esm_dump.py` reads Skyrim Special Edition plugin files (`.esm` / `.esp` / `.esl`) and gives you the
form IDs you need to write a Dova Stash **home template** in the exact notation the mod uses:

```
"Plugin.esm|0x00ABCDEF"     plugin file name + LOCAL form id (load-order byte stripped;
                            for ESL-flagged plugins only the low 12 bits, e.g. "MyHome.esl|0x00000801")
```

The script uses only the Python standard library and never writes anything. It needs Python 3.10 or newer.
Run it with `python -I` (isolated mode).

## Setup

The script finds `Skyrim Special Edition\Data` in the usual Steam locations. If yours is somewhere else,
set `DOVASTASH_SKYRIM_DATA` or pass `--data "D:\Games\Skyrim Special Edition\Data"`.

By default the load order is Skyrim, Update, Dawnguard, HearthFires and Dragonborn, followed by every
installed Creation Club plugin in `Skyrim.ccc` order. Later plugins override earlier ones, as in the game.
To include your own mod (for example from an MO2 `mods` folder), use `--add`. Its masters must also be
loaded:

```
python -I esm_dump.py --add "E:\MO2\mods\My Home\MyHome.esp" cells MyHome
```

Other global options: `--vanilla-only`, `--plugins A.esm,B.esp` (an explicit load order), `--lang english`
(the Strings language) and `--compact` (single-line JSON). Global options go **before** the subcommand.

Localized names come from `Data\Strings\*.STRINGS` or from the plugin's BSA. That covers `Skyrim - Interface.bsa`
for the base game and `<plugin>.bsa` for Creation Club. When a string cannot be found, the output shows its
string id.

## Subcommands

| Command | What it does |
|---|---|
| `cells <text> [--names]` | Find cells whose EditorID (or in-game name, with `--names`) contains `<text>` |
| `cell <EDID or ref> [--with FURN,ACTI]` | Dump a cell's header and every **container** placed in it. `--with` also lists crafting stations, beds and racks, which helps you see which room a container is in |
| `ref "<Plugin\|0x...>"` | Describe one placed reference and the cell it lives in |
| `keywords [--filter text] [--check A B ...]` | List keyword EditorIDs, or check that the given ones exist |
| `keyword-usage KW ... [--types MISC,INGR]` | Count the items that carry each keyword, with a few sample items |
| `check <files or dirs>` | Validate templates and category files against the plugins. The exit code is 1 if there are errors |
| `plugins` | Show the resolved load order (masters, light and localized flags) |

### Reading a `cell` dump

Cell header:

- `cell`: the cell's form ref. This is the template's `cell` value. Many Creation Club homes are cells
  pre-allocated in `Update.esm`, for example Gallows Hall is `Update.esm|0x00002FD0`.
- `owner`, `location` (with its keywords, such as `LocTypePlayerHouse`), `encounterZone` (with
  `NeverResets`) and `flags`: these are the facts Dova Stash's eligibility checks use.

Each container entry:

- `ref`: the placed reference. Use this in `master` and `containers[].ref`. Never use `base`.
- `baseEditorId` / `baseName`: what the container is.
- `respawns`: the base container has the respawn flag. **Never bind it.** Its contents can be reset.
- `initiallyDisabled`, `enableParent`, `startsDisabled`: the container only appears later, for example
  after a house upgrade or Hearthfire building. That is fine to bind, because Dova Stash skips disabled
  containers until they appear.
  - `enableParent.oppositeOfParent: true` means the container *disappears* when its parent marker is
    enabled. These are usually pre-upgrade clutter. Do not bind them unless `refScripts` contains
    `BYOHDivertItemScript`, which moves their contents out before they disappear.
- `displays` / `displayCounts`: the racks, mannequins, item plaques and display cases Dova Stash can fill
  (DESIGN §8.4), with the rack type or the items each plaque or case is made for. Only the ones a layout
  lists are filled: copy the refs you want into the template's `displays`. In game an item plaque
  also needs a "placed" flag in its script and a case an `OnItemAdded` event; these are rarely visible
  in the plugin, so the list can be a little longer than what the game fills. Template `displays` entries
  are checked by `check` (ref in the cell, kind, items are items).
- `openedBy`: activators in front of this container that open it (a food shelf, a wine rack, a bookshelf
  trigger). Bind the container; Dova Stash shows it on the activator in game.
- `baseScripts` / `refScripts`: attached Papyrus scripts. `PlayerBookShelfContainerScript` is a bookshelf
  that only displays books and gives everything else back. Leave those out or mark them `"ignored": true`.
- `position` / `rotationZDeg`: in-game coordinates. Use them to work out which room a container is in.
- `persistent`, `deleted`, `lockLevel`, `owner`.

Exterior cells also include the worldspace's persistent references that lie inside the cell's grid square.

## Writing a template (workflow)

1. `python -I esm_dump.py cells Breezehome` gives you the cell ref.
2. `python -I esm_dump.py cell WhiterunBreezehome --with FURN > breezehome.json` gives you every container
   plus the nearby crafting stations and beds.
3. Pick a `master`, preferably a large, always-present, non-respawning chest. Bind the other containers to
   categories (see `dist/Data/SKSE/Plugins/DovaStash/Categories/00_Default.json` for the ids; wildcards like
   `weapons.*` are allowed). Mark dangerous containers with `"ignored": true` and a `"note"` that explains
   why; keep `"label"` for the short name players see. List the racks, mannequins and item plaques that
   should be filled in `displays` (the `cell` output's `displays` has their refs); players turn the others
   on with the hotkey. `categories` limits what a rack or mannequin takes, like a container's; `set` gives a
   mannequin one armor set, named by any of its pieces (`check` makes sure it is an ARMO). An entry can
   also change a display's items or name one Dova Stash can't recognise (DESIGN §8.4). Plaques and cases
   get their own `items` written in game when they are turned on, so leaving them out is fine.
4. List every plugin the template references in `requires`, including the plugin that owns the cell.
5. `python -I esm_dump.py check path\to\MyHome.json --categories path\to\Categories\00_Default.json`
   - It fails on refs that are not containers in the declared cell, respawning bound containers, duplicate refs,
     unknown categories, display entries that point at nothing in the cell, and plugins that are missing
     from `requires`.
   - It warns about bookshelves, containers that disappear on upgrade, and a master that starts disabled.
   - `//` and `/* */` comments are allowed, as in game.

Plugin names in form refs use the plugin's own file name, for example `ccBGSSSE001-Fish.esm` as written in
`Skyrim.ccc`. Steam may store the file in lower case. That does not matter, because Windows file names
are case-insensitive.

## `stress_loot.py` (testing)

Writes console batch files (`ds_bulk`, `ds_vanilla`, `ds_cc`, `ds_mods1..N`, `ds_weird`) into the MO2 mod
`dovastash inventory testing/root/`. Root Builder deploys that folder to the game folder, where `bat` looks.
Each one fills the player's inventory with items picked from your real MO2 load order: every type and subtype
the category engine knows, vanilla uniques, and a few items from each mod plugin. `ds_weird` holds the
oddballs (unnamed, no keywords, scripted, vanilla-name clones). `ds_manifest.csv` lists what each line adds.
Console form IDs depend on the load order, so regenerate after changing plugins.

```
python -I tools/stress_loot.py [--mo2 "E:\modding\Kexodus Skyrim"] [--profile Default] [--per-plugin 6]
```

In game: `bat ds_vanilla`, and so on. Use a throwaway save, because scripted items run their scripts when added.

## `datagen/` (maintainers)

The scripts that produce the shipped data:

- `datagen/gen_categories.py` writes `Categories/00_Default.json`. It resolves the explicit Dwemer and
  building-material form lists from the plugins.
- `datagen/gen_templates.py` writes `Templates/Vanilla/*.json` and `Templates/CreationClub/*.json`. The
  container-to-category bindings in it are hand-picked. The `"ignored"` entries are derived from the plugin
  data: respawning containers, scripted bookshelves, and containers removed by upgrades. They are labelled
  with the container's game name, and the reason goes in `"note"`. The output is laid out by
  `datagen/jsonfmt.py` (`align=True`): one line per container, fields lined up in columns, like the files the
  DLL writes.
- `datagen/gen_ocf_categories.py --ocf "<OCF mod folder>"` writes the optional OCF set,
  `dist/Optional/OCF/Data/SKSE/Plugins/DovaStash/Categories/50_OCF.json`, and `tests/data/ocf_keywords.txt`. It
  reads OCF's `*_KID.ini` files and fails if a category uses a keyword that OCF doesn't put on that record type.

Run both scripts again after changing a binding, then run `check` on `dist/Data/SKSE/Plugins/DovaStash`.
