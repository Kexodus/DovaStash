# Nexus upload form: Dova Stash 0.1.0

Values for every field of the Nexus Mods "Add a mod" form for **Skyrim Special Edition**. Paste text
from `nexus/out/` (after running `fill_urls.py`), not from the templates in `nexus/`, because the
`out/` copies have the real image and source URLs filled in.

## 1. Mod details

| Field | Value |
|---|---|
| Game | Skyrim Special Edition |
| Mod name | `Dova Stash` |
| Version | `0.1.0` |
| Author | your Nexus user name (credited as the creator) |
| Category | **Player Homes** (see below) |
| Language | English |
| Adult content | **No** |
| Brief overview (summary) | contents of `out/summary.txt` (309 characters; the limit is 350) |
| Detailed description | contents of `out/description.bbcode` |
| Primary image | `keyart_1920x1080.png` |
| Is this a translation? | No |
| Uses assets from other mods? | No. The bundled SKSE Menu Framework API header is a code header, not an asset; it is credited and LGPL-2.1. |

**Optional longer name for search:** `Dova Stash - Auto-Sorting Storage for Any Home`. Search Nexus for
"Dova Stash" before you publish, in case the name is already taken (DESIGN §15 #8).

### Category choice: Player Homes

- Everything the mod does happens in a player home. It works in any home, and it ships 22 home
  templates. People who want better storage in their home browse **Player Homes**.
- Community template packs for individual homes will naturally go in the same category, so the main
  mod and its add-ons sit next to each other.
- Alternatives considered:
  - **Utilities** on SSE is mostly external tools such as xEdit and BodySlide. A player-facing
    in-game feature gets lost there.
  - **Gameplay** fits, but it is very broad and gives poor visibility.
  - **User Interface** is wrong: the mod sorts items, and its UI is secondary.
- Fallback: if a moderator decides Player Homes should hold only actual houses and moves the mod,
  choose **Gameplay**.

## 2. Tags

Nexus offers a fixed tag vocabulary, so pick the closest available match for each of these. In order
of importance:

1. SKSE (SKSE plugin)
2. Player Home
3. Storage / Containers
4. Sorting / Inventory
5. Quality of Life
6. Hearthfire
7. Creation Club
8. VR
9. ESP-less / No plugin (if offered)
10. Open source (if offered)
11. Utility

Leave out: Lore-friendly (the mod adds no content), Immersion, Adult.

## 3. Requirements tab

**Nexus requirements**

| Mod | Note |
|---|---|
| Address Library for SKSE Plugins (32444) | SE and AE. Required. |
| VR Address Library for SKSEVR (58101) | VR only. Required on VR instead of Address Library. |
| SKSE Menu Framework (120352) | Optional. SE and AE only. Adds the full panel. Without it, native message boxes are used. |
| ImGui Icons (114790) | Only needed by SKSE Menu Framework. Optional for Dova Stash itself. |

**Off-site requirements**

| Name | URL | Note |
|---|---|---|
| SKSE64 / SKSEVR | https://skse.silverlock.org/ | Required. Use the build that matches your game version. |

**DLC requirements:** none. The base game is enough. Templates are only offered when their plugins are
loaded. Every SSE/AE install includes Hearthfire and Dragonborn, and Creation Club templates appear
only for content the player owns.

**Mods requiring this:** none at release; Nexus fills this list in automatically. Community template
packs should list Dova Stash as a Nexus requirement with the note *"Templates for Dova Stash"*.

## 4. Permissions and credits

Dova Stash is **GPL-3.0-or-later**. The GPL grants these permissions and does not allow extra
restrictions to be added (GPL-3 §10), so every Nexus toggle maps to **allowed, with the GPL conditions**:

| Nexus permission | Setting | Why |
|---|---|---|
| Upload permission | Allowed to upload to other sites, with credit | GPL §4–6: redistribution is allowed if the license and the source travel with it |
| Modification permission | Allowed to modify and release, with credit | GPL §5: modified versions must stay GPL-3.0-or-later |
| Conversion permission | Allowed to convert to other games, with credit | Counts as a modification under the GPL |
| Asset use permission | Allowed to use in other mods, with credit | Any derivative must be GPL-3.0-or-later |
| Asset use in mods/files being sold | Allowed | The GPL permits selling, provided source and license come along. Choosing "No" would add a restriction the GPL forbids. |
| Asset use in mods/files earning Donation Points | Allowed | Same reason |
| Donation Points for this mod | Your choice | |

**Custom permission instructions (paste into the form):**

```
Dova Stash is free software, licensed GPL-3.0-or-later (https://www.gnu.org/licenses/gpl-3.0.html).
You may re-upload, modify, translate, port and include it in other works, provided that:
- your version is also licensed GPL-3.0-or-later,
- you provide the complete corresponding source code (or a link to it) with any binary you distribute,
- you keep the copyright and license notices and credit the original author.
Source code: {SOURCE_URL}
The vendored SKSE Menu Framework API header (QTR-Modding) is LGPL-2.1.
Templates and category files you make yourself are your own work and you may license them however you like.
```

> **GPL source obligation:** while the GitHub repository is private, publishing the DLL on Nexus
> means you must still make the source available to downloaders. Either make the repository public
> before release, or upload a source archive as an extra file (category *Miscellaneous*, name
> `Dova Stash 0.1.0 - Source`). `fill_urls.py` refuses to leave the source-URL placeholder unfilled unless
> you pass `--allow-placeholders`.

**Credits (form field):**

```
SKSE team - Skyrim Script Extender
CommonLibSSE-NG: Ryan McKenzie (CommonLibSSE), CharmedBaryon (NG), alandtse and contributors (CommonLibVR ng branch)
meh321 - Address Library for SKSE Plugins; the VR Address Library maintainers
Thiago099 / QTR-Modding - SKSE Menu Framework and its API
powerofthree - ImGui Icons
Eddoursul - Stash Organizer (Fallout: New Vegas), the inspiration for this mod
spdlog, nlohmann/json, SimpleIni, Catch2
Page fonts: Cinzel, Jost, Noto Sans Runic (SIL Open Font License)
```

## 5. Files tab: main file

| Field | Value |
|---|---|
| File name | `Dova Stash` |
| Version | `0.1.0` |
| Category | **Main Files** |
| Update to an existing file? | No (first upload) |
| Remove the previous version? | n/a |
| Archive name | `Dova Stash 0.1.0.7z` (or `.zip`) |
| File description | contents of `out/file_description.txt` |
| Requirements popup | On (it shows the requirements tab at download) |
| Mod manager download | On |

**Archive layout.** It must map onto `Data/`. Don't wrap it in an extra top-level folder.

```
SKSE/Plugins/DovaStash.dll
SKSE/Plugins/DovaStash.ini                     (copy of src/plugin/DovaStash.ini.default)
SKSE/Plugins/DovaStash/Categories/00_Default.json
SKSE/Plugins/DovaStash/Templates/Vanilla/*.json        (12 files)
SKSE/Plugins/DovaStash/Templates/CreationClub/*.json   (10 files)
LICENSE                                         (optional, but good practice for GPL)
```

Leave out `DovaStash.pdb`, unless you want readable crash logs from users. In that case ship it as a
separate *Optional* file named `Dova Stash 0.1.0 - Debug symbols`.

**Optional extra file** (if the repository is still private): `Dova Stash 0.1.0 - Source`, category
*Miscellaneous*. Use a `git archive` of the release commit plus the CommonLibSSE-NG submodule
revision note.

## 6. Images

Upload order, Images tab:

| # | File | Where it's used |
|---|---|---|
| 1 | `keyart_1920x1080.png` | **Primary image** (thumbnail and the first gallery image) |
| 2 | `gallery_how_it_works.png` | Gallery; also embedded in the description |
| 3 | `gallery_templates.png` | Gallery |
| 4 | `gallery_homes.png` | Gallery |
| 5 | `gallery_categories.png` | Gallery |
| 6 | `gallery_safety.png` | Gallery |
| 7 | `features_grid_1200x700.png` | Gallery (optional); embedded in the description |

**Embedded in the description only.** Don't put these in the gallery, because they would clutter it.
Host them where `fill_urls.py` can point to them (see `nexus/README.md`):

- `banner_1920x480.png`
- `divider_1200x40.png`
- `features_grid_1200x700.png`
- `gallery_how_it_works.png`
- `header_<name>_1200x140.png` for features, how_it_works, templates, requirements, installation,
  configuration, compatibility, template_authoring, faq, permissions, credits and changelog. That is
  12 headers, and the description uses all of them.

**Not used on the mod page:**

- `logo_1024.png`, `logo_1024_transparent.png`, `logo_512.png`, `logo_256.png`: your Nexus profile or
  collection thumbnails and forum signatures.
- `icon_<name>_256.png` (any_home, auto_sort, templates, learns, safe, menu, vr, export, hotkey):
  available for a GitHub README, articles or a later description revision. The description
  currently relies on `features_grid` instead.
- `github_social_1280x640.png`: the GitHub repository's social preview (Settings → Social preview)
  once the repository is public.

If the form offers a page header or banner image, use `banner_1920x480.png`.

## 7. Posts tab: pinned post

Post the contents of `out/sticky_post.bbcode` as the first comment, then **Pin** it ("Sticky").
Title, if one is asked for: `Bug reports: please read before posting`.

## 8. Articles / Docs (optional)

The description already contains the template-authoring guide. If you'd like a separate article,
the "Template authoring" spoiler from the description works as one, word for word.
