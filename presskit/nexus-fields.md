# Nexus upload form: Dova Stash

Values for every field of the Nexus Mods "Add a mod" form for **Skyrim Special Edition**. Every text file
in `presskit/` is ready to paste as it is: the description loads its images from this repository's
`main` branch. Use the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`) wherever a version is
asked for.

## 1. Mod details

| Field | Value |
|---|---|
| Game | Skyrim Special Edition |
| Mod name | `Dova Stash` |
| Version | the current version, e.g. `0.2.3` |
| Author | your Nexus user name (credited as the creator) |
| Category | **Player Homes** (see below) |
| Language | English |
| Adult content | **No** |
| Brief overview (summary) | contents of `nexus-summary.txt` (the limit is 350 characters) |
| Detailed description | contents of `nexus-description.bbcode.txt` |
| Primary image | `dovastash-thumbnail-1280x720.png` |
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

> **GPL source obligation:** met by the public repository,
> [github.com/Kexodus/DovaStash](https://github.com/Kexodus/DovaStash), which the description links to.

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
| Version | the current version, e.g. `0.2.3` |
| Category | **Main Files** |
| Update to an existing file? | No (first upload) |
| Remove the previous version? | n/a |
| Archive name | the GitHub release zip, `DovaStash-x.y.z.zip` |
| File description | contents of `nexus-file-description.txt` |
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

Ship this one file only: no debug-symbols or optional category downloads. The OCF category set stays
in the repository (`dist/Optional/OCF/`) for anyone who asks.

## 6. Images

Upload order, Images tab (files in `presskit/nexus/`):

| # | File | Where it's used |
|---|---|---|
| 1 | `dovastash-thumbnail-1280x720.png` | **Primary image**: the mod tile in search and browse. Emblem and name only, readable at tile size and safe under the tile's crop |
| 2 | `dovastash-primary-1920x1080.png` | Gallery: emblem, name and tagline |
| 3 | `dovastash-gallery-how-it-works-1920x1080.png` | Gallery |
| 4 | `dovastash-gallery-menu-1920x1080.png` | Gallery: one key, the hotkey lists |
| 5 | `dovastash-gallery-templates-1920x1080.png` | Gallery |
| 6 | `dovastash-gallery-homes-1920x1080.png` | Gallery |
| 7 | `dovastash-gallery-displays-1920x1080.png` | Gallery: displays, mannequins, workbench |
| 8 | `dovastash-gallery-categories-1920x1080.png` | Gallery |
| 9 | `dovastash-features-1200x700.png` | Gallery (optional) |

**Embedded in the description only.** Don't put these in the gallery, because they would clutter it.
The description already loads them from GitHub:

- `dovastash-header-card.png`, `dovastash-how-it-works-card.png` and `dovastash-features-card.png`:
  the banner, the steps and the features as cards (rounded corners, a hairline edge, a soft shadow,
  same sizes as iharvest-ng's), so they sit on the page instead of as hard black boxes. The section
  headers are cards too.
- `dovastash-divider-transparent-1200x40.png`: the closing divider above the GitHub link, the
  frameless mark between two hairline rules.
- `section-01-features.png` to `section-13-support.png`: all 13 section headers.

**Not used on the mod page:**

- `presskit/logo/dovastash-logo-*.png`: your Nexus profile or collection thumbnails and forum
  signatures.
- `icon-*-256.png`: available for a GitHub README, articles or a later description revision.
- `dovastash-social-1280x640.png`: the GitHub repository's social preview (Settings → Social preview).

If the form offers a page header or banner image, use `dovastash-header-1920x480.png`.

## 7. Posts tab: pinned post

Post the contents of `nexus-sticky-post.bbcode.txt` as the first comment, then **Pin** it ("Sticky").
Title, if one is asked for: `Bug reports: please read before posting`.

## 8. Articles / Docs (optional)

The description already contains the template-authoring guide. If you'd like a separate article,
the "Template authoring" spoiler from the description works as one, word for word.
