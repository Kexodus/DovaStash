# Similar mods and where Dova Stash overlaps

Research snapshot, 2026-10-14. Not a spec. Covers Skyrim SE/AE/VR mods (and notable LE ones) that overlap any
part of Dova Stash: sorting into home containers, displays, materials at the workbench, home templates, and
mods that touch the same scripts or menus.

**How this was gathered.** Mod names, authors, versions, dates, endorsements (E) and downloads (D) come from
the public Nexus Mods v2 API (`api.nexusmods.com/v2/graphql`, `legacyMods` / `mods` queries) on 2026-10-14.
The descriptions quoted are the mod pages' own text, read through the same API. Which mods override vanilla
scripts comes from Nexus's archive file index (`modFileContents`), which lists loose files only: a script
packed in a BSA is not seen there. The user's install was read from `E:\modding\Kexodus Skyrim\mods\` and the
`Default` and `Dova Stash Testing` profiles' `modlist.txt` (read only). Reddit could not be reached:
web search returned no reddit.com results and Reddit's JSON search refused the request. Evidence for "users
like this" therefore comes from Nexus endorsement and download counts and from the mod pages, not from Reddit.
**[unverified]** marks claims not checked against the mod's files or in game.

"Maintained" means updated in the last 12 months (after 2025-10-14).

## Summary

| Mod | What it overlaps | Verdict | User has it? |
|---|---|---|---|
| [Home Auto Sort](https://www.nexusmods.com/skyrimspecialedition/mods/183379) | Any-home master chest, per-category containers, deposit, presets (JSON), SKSE Menu Framework | **Competitor** (closest) | No |
| [Crafting Pulls Resources From Containers](https://www.nexusmods.com/skyrimspecialedition/mods/135613) | Materials at the workbench | Competitor (one feature); **conflict risk** | No |
| [Player houses overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/190875) | Sorting, auto-craft pulls, edits all 5 city homes | Competitor; **conflict risk** with our 5 city-home templates | No |
| [Tundra Homestead - LotD Storage](https://www.nexusmods.com/skyrimspecialedition/mods/89836) | Turns Tundra Homestead's containers into LOTD storage with a sorting chest | **Conflict risk** (we ship a Tundra Homestead template) | No |
| [Wynter's Breezehome Redux](https://www.nexusmods.com/skyrimspecialedition/mods/81607) | Breezehome rebuild, optional auto-sort strongroom, own mannequin and rack scripts | **Conflict risk** with our Breezehome template | **Yes** (Default and Testing) |
| [Clockwork](https://www.nexusmods.com/skyrimspecialedition/mods/4155) | Castle with its own sorter | Handled (we leave it alone) | **Yes** (Default) |
| [Legacy of the Dragonborn](https://www.nexusmods.com/skyrimspecialedition/mods/11802) + [Curator's Companion](https://www.nexusmods.com/skyrimspecialedition/mods/38529) + [Craftloot FLM](https://www.nexusmods.com/skyrimspecialedition/mods/74810) | Museum, Safehouse sorting, craftloot, displays | Handled (we leave it alone); LSEU patches extend it to other homes | **Yes** (LOTD, TCC in Default) |
| [Automatic Item Storage](https://www.nexusmods.com/skyrimspecialedition/mods/8224) (+ [Improved Custom Lists](https://www.nexusmods.com/skyrimspecialedition/mods/42387)) | Any container, per-container item lists, keep/take amounts, workstation link | Competitor (old, popular) | No |
| [General Stores](https://www.nexusmods.com/skyrimspecialedition/mods/4322) | Cloud storage with auto-sort; home add-ons | Competitor (different model) | No |
| [Dynamic Autosorting System](https://www.nexusmods.com/skyrimspecialedition/mods/18131) | Any-home sorting chest, crafting auto-retrieve | Competitor (abandoned 2018) | No |
| [AK Auto Sort](https://www.nexusmods.com/skyrimspecialedition/mods/44235) | Keyword sort into any containers, configs outside the save | Competitor (abandoned 2021) | No |
| [Auto Sorting Framework NG](https://www.nexusmods.com/skyrimspecialedition/mods/172982) | SKSE rule-based sorter (framework for authors) | Competitor / possible integration target | No |
| Other sorters: [Simple Storage Management](https://www.nexusmods.com/skyrimspecialedition/mods/37092), [Container Magic SE](https://www.nexusmods.com/skyrimspecialedition/mods/64258), [Super Smart Sorting Storage](https://www.nexusmods.com/skyrimspecialedition/mods/125395), [Dynamic Storage Cube](https://www.nexusmods.com/skyrimspecialedition/mods/98839), [Servants Sort Loot](https://www.nexusmods.com/skyrimspecialedition/mods/235), [Quartermaster](https://www.nexusmods.com/skyrimspecialedition/mods/180327), [Aeolus QOL](https://www.nexusmods.com/skyrimspecialedition/mods/101194), [Storehouse](https://www.nexusmods.com/skyrimspecialedition/mods/81778) | Sorting / storage | Minor competitors | No |
| [Linked Crafting Storage](https://www.nexusmods.com/skyrimspecialedition/mods/64772), [Smart Crafting Storage](https://www.nexusmods.com/skyrimspecialedition/mods/182056), [Workbench Containers](https://www.nexusmods.com/skyrimspecialedition/mods/130090), [Crafting Containers](https://www.nexusmods.com/skyrimspecialedition/mods/92825), [Auto Chests](https://www.nexusmods.com/skyrimspecialedition/mods/68795), [Handy Crafting and Spells](https://www.nexusmods.com/skyrimspecialedition/mods/59258) | Materials at the workbench | Competitors; **conflict risk** (two systems moving the same materials) | No |
| Homes with their own sorter: [Mirele Bismath Reborn](https://www.nexusmods.com/skyrimspecialedition/mods/93817), [LUFTSHIP K747](https://www.nexusmods.com/skyrimspecialedition/mods/53259), [Skyfall Estate](https://www.nexusmods.com/skyrimspecialedition/mods/2185), [Riverside Lodge](https://www.nexusmods.com/skyrimspecialedition/mods/9567), [Elysium Estate](https://www.nexusmods.com/skyrimspecialedition/mods/4119), [Hoarder's Chest - Rebuilt](https://www.nexusmods.com/skyrimspecialedition/mods/60581), [Dovah Den](https://www.nexusmods.com/skyrimspecialedition/mods/38376), [Carpenter Storage Chests](https://www.nexusmods.com/skyrimspecialedition/mods/95040) | Sorting inside one home | **Conflict risk** if both are used in that home | **Yes**: Mirele (both profiles); LUFTSHIP, Skyfall, Riverside, Elysium (Testing) |
| [Self Filling Bookshelves SE](https://www.nexusmods.com/skyrimspecialedition/mods/2050) | Bookshelf filling by type | Competitor (one feature) | No |
| [Unlimited Bookshelves](https://www.nexusmods.com/skyrimspecialedition/mods/2885) | Replaces the bookshelf script (128 items) | **Conflict risk** (bookshelf recognition) | No |
| [Immersive Display Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/70508), [Andrealletius Exploit Fixes](https://www.nexusmods.com/skyrimspecialedition/mods/46753) | Recompiled `WeaponRackActivateSCRIPT` | **Conflict risk** (rack filling) | No |
| [Another Mannequin Script Fix](https://www.nexusmods.com/skyrimspecialedition/mods/56543), [Mannequin Management](https://www.nexusmods.com/skyrimspecialedition/mods/38221) | Mannequin script / placement | Complement; test | **Yes** (AMSF, Default) |
| [Improved Mannequins (DAK)](https://www.nexusmods.com/skyrimspecialedition/mods/100834), [Mannequins Swap Gear](https://www.nexusmods.com/skyrimspecialedition/mods/83575) | Swap outfit with a mannequin | Complement (feature we lack) | No |
| [HearthFires Display Case Fix SE](https://www.nexusmods.com/skyrimspecialedition/mods/15104), [Creation Club Display Case Fix](https://www.nexusmods.com/skyrimspecialedition/mods/43485) | Make display cases usable | Complement (we fill cases only when usable) | USCCCP has the CC fixes: **Yes** |
| [Better Container Controls for SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/25271), [Quick Item Transfer](https://www.nexusmods.com/skyrimspecialedition/mods/125160) | Bulk move by category in the container menu | Complement | **Yes** (BCC, Default) |
| [QuickLoot IE](https://www.nexusmods.com/skyrimspecialedition/mods/120075) | Container menu | Integrated already | **Yes** |

Checked and not overlapping: Kome's Inventory Tweaks, Container Item Distributor, Immersive Equipment Displays
(worn gear on the player), Remote Crafting, bags of holding / portable storage, item-renaming "sorting" mods
(Valdacil's, Bhodi's, etc.; inventory order only).

---

## Any-home sorters

### Home Auto Sort (closest competitor)
[Nexus 183379](https://www.nexusmods.com/skyrimspecialedition/mods/183379) · zfroggyman · SE/AE ("1.5.97 - 1.7+"),
VR not stated · v2.5, created 2026-06-23, updated 2026-10-04 (maintained) · E39, D990 · SKSE DLL
(`HomeAutoSort.dll`, CommonLibVR credited), UI in **SKSE Menu Framework**, hotkeys through **SkyPrompt**.
A Portuguese translation exists (193808), so it has some reach.

How it works (from its page): stand in a cell, open its Menu Framework page, pick one of five "cell" tabs,
"set to home", pick the master chest from a filtered list of the cell's containers (name or ref ID), then a
container per category and subcategory (subcategory wins). Hold a key at the master to stash everything;
"resupply" pulls potions, arrows, food back from the sorted containers. Blacklist (since 1.3). Export a cell's
setup as a JSON preset into `SKSE/Plugins` (MO2 overwrite); presets load automatically, the menu's own setup
wins over a preset for the same cell; import presets to edit. A LoreRim food sub-categorisation option. The
author wrote it because they missed Clockwork's sorter in other homes.

| Feature | Overlap |
|---|---|
| Master chest + category containers, any home, indoors or out | Same |
| Deposit (hold key at master) | Same; we also sort on arrival and from any container's hotkey menu |
| Presets as JSON, shareable | Same idea as templates; **we do it better**: 22 shipped templates, auto-enable, ranking, switching, backups, the editor. HAS ships none and its menu holds 5 cells |
| Resupply (pull consumables back) | **They have it**; our quotas only keep items on Deposit, nothing tops the player up |
| Learning, undo, category suggestions | **We have it**, they don't |
| Displays, mannequin sets, bookshelves, plaques | **We have it**, they don't |
| Materials at the workbench | **We have it**, they don't |
| Multi-cell homes | **We have it** (they use one cell per tab) |
| In-world UI | They: SkyPrompt prompts. We: UIExtensions lists, message boxes, QuickLoot IE |

Compatibility: both are hotkey-at-the-master SKSE plugins. Setting up the same home in both means two
systems moving the same items. HAS's SkyPrompt "takes precedence" over other bindings of the same key
(its page), so if the player gives both the same key only HAS answers. **[unverified]** Action: detect
`HomeAutoSort.dll` and log it; document "set up a home in one of them, not both". A future import of HAS
presets into a Dova Stash template is possible (its JSON format is unknown here: **[unverified]**).

### Automatic Item Storage (+ Improved Custom Lists)
[Nexus 8224](https://www.nexusmods.com/skyrimspecialedition/mods/8224) · Manilla Turtle · SE (LE:
[12468](https://www.nexusmods.com/skyrim/mods/12468)) · v1.21, updated 2017-08-15 (not maintained) · E3077,
D231k · ESP + Papyrus. Put the "fish" in any container (max 50) to make it an auto-storage container with its
own item list; double-activate: Store All / Store Amount (keep N); triple: settings; "Storage Take" takes N of
each; save/load lists; **Workstation Link** brings a container's items to you at a linked station and returns
them. [Improved Custom Lists](https://www.nexusmods.com/skyrimspecialedition/mods/42387) (v2.2, 2021-01-09,
E153) adds JSON export/import, "Learn" from a container's contents, and "Build" lists from keywords.

Overlap: any container (same); per-container item lists (we use categories plus learned items: **we do it
better**, no list building); keep N (same as our quotas); **take N / restock: they have it**; workstation link
(same; ours needs no linking and brings only what recipes use). Compatibility: a container can have both. Low
risk unless both are told to store into the same containers.

### General Stores
[Nexus 4322](https://www.nexusmods.com/skyrimspecialedition/mods/4322) (SE convert) · Harvey2112, Draco1122 ·
updated 2023-07-14 · E5529, D594k · LE original [18340](https://www.nexusmods.com/skyrim/mods/18340) (E11206).
A modder's resource: cloud storage reachable from anywhere, with auto-sorting into category stores and an
optional crafting-bench resource transfer; many home add-ons build on it (Elysium Estate, Hearthfire homes
[LE 26920](https://www.nexusmods.com/skyrim/mods/26920), [Craftable Cloud Storage](https://www.nexusmods.com/skyrimspecialedition/mods/5263)).
The author is "just maintaining" it. Overlap: sorting by category (same), remote access (they have it; our
central storage is v2). Compatibility: separate containers, so low risk; a home whose patch replaced its
containers with GS activators simply has nothing for us to bind.

### Dynamic Autosorting System
[Nexus 18131](https://www.nexusmods.com/skyrimspecialedition/mods/18131) · Sjogga · v1.2, 2018-06-22 (abandoned) ·
E443, D7.9k · LE [49523](https://www.nexusmods.com/skyrim/mods/49523) (E1103). SKSE + SkyUI MCM, Papyrus.
Drop category tokens into chests (renames them), drop loot into the sorting chest; "quickstash"; "Allow Global
Auto Sort" makes every chest push out what does not belong; crafting stations linked with a spell auto-loot
their chest. Overlap: same core loop; **we do it better** (no tokens, templates, learning, displays). Its
global auto-sort and crafting retrieve would fight ours in the same home. A third-party
[QuickStash Disable Patch](https://www.nexusmods.com/skyrimspecialedition/mods/115949) exists.

### AK Auto Sort
[Nexus 44235](https://www.nexusmods.com/skyrimspecialedition/mods/44235) · Iceburg · v1.0, 2021-01-10
(abandoned) · E67 · SKSE + JContainers. A tool item configures a container's keywords; a lesser power sorts
the inventory into the tracked containers of the current cell; configs live outside the save (JContainers).
Its own Papyrus profile of 260 items reports a total of 64766 (as printed on the page) and the author calls it
slow: evidence for the native-speed point below. Overlap: same idea, ours is native and richer.

### Auto Sorting Framework NG (SKSE)
[Nexus 172982](https://www.nexusmods.com/skyrimspecialedition/mods/172982) · LencevioX · v1.0, 2026-02-20 ·
E23, D695 · SKSE DLL + INI profiles, triggered only from Papyrus (`AutoSortingFrameworkNG.SortAll("Profile")`).
Rules by form type, keyword, FormID, name, enchantment; never quest items. A framework for home authors, not
a player tool. Overlap: the rule engine (ours is JSON categories). Not a conflict unless a home mod built on it
sorts the same containers. Possible integration: none needed.

### Player houses overhaul
[Nexus 190875](https://www.nexusmods.com/skyrimspecialedition/mods/190875) · linkoln84 · v1.1, updated
2026-09-19 · E11, D330 · SE/AE, SKSE + SkyUI MCM + `PlayerHouse.esp` (about 370 script instances). Overhauls the
interiors of all five city homes (switchable vanilla / modded), adds "virtual sorting chests" usable anywhere
(NUMPAD7), a merchant chest with a "laziness tax", autosort by category with exclusions and an item blacklist
hotkey (hover an item, NUMPAD5), "Autocraft" pulls at workbenches, teleport. Its page: "Incompatible with: mods
that change the same interiors".
Overlap: sorting (same), autocraft (same as our workbench feature), **sell chest and blacklist-from-inventory
hotkey: they have it**. Compatibility: **conflict risk** for our Breezehome, Honeyside, Hjerim, Proudspire and
Vlindrel templates: the cells are rebuilt, so template refs may be gone or moved, and its autocraft would fight
ours. Action: do not auto-enable a city-home template when `PlayerHouse.esp` is loaded (see actions).

### Smaller sorters
- [Simple Storage Management](https://www.nexusmods.com/skyrimspecialedition/mods/37092) (Voeille, 2020, E48):
  four spells; one category per container, categories built from containers' contents; transfer, retrieve
  leaving N. Overlap small; spell-based.
- [Container Magic SE](https://www.nexusmods.com/skyrimspecialedition/mods/64258) (2022, E18; LE
  [23001](https://www.nexusmods.com/skyrim/mods/23001)): store/take by type in any container via a spell menu.
- [Super Smart Sorting Storage](https://www.nexusmods.com/skyrimspecialedition/mods/125395) (2024, E16): remote
  sorting storage by spell, craft import at stations, claims to avoid `OnItemAdded` for safety.
- [Dynamic Storage Cube](https://www.nexusmods.com/skyrimspecialedition/mods/98839) (2025-08-04, E66): remote
  storage with auto-sort and craft-loot (FLM/KID/OCF aware).
- [Servants Sort Loot](https://www.nexusmods.com/skyrimspecialedition/mods/235) (2017, E611, no SKSE): hire an
  NPC who sorts what you give them into marked containers in any place. A different, immersive take.
- [Quartermaster](https://www.nexusmods.com/skyrimspecialedition/mods/180327) (TechAngel85, 2026-05-25): scripts
  for home authors: auto-sort (22 storage options) and crafting-linked containers. Homes built on it will sort
  themselves (watch for them).
- [Aeolus QOL](https://www.nexusmods.com/skyrimspecialedition/mods/101194) (updated 2026-08-23): "Auto-Sort
  Storage" dumps the inventory into one remote chest; sell cart; portable crafting.
- [Storehouse](https://www.nexusmods.com/skyrimspecialedition/mods/81778) (2023, E50): per-category carry
  limits, excess to a shared stash, **auto-replenish consumables on entering a home**.
- LE only: [Storage Helpers](https://www.nexusmods.com/skyrim/mods/17574) (CDCooley, LE, 2015, E1184; no SE
  port found on Nexus), [Hearthfire Automated Storage](https://www.nexusmods.com/skyrim/mods/25154).
- Not found on Nexus SE under these names: "Simple Item Storage", "Sort and Store", "Lazy Sorter", "Quick Loot
  Sort", "Skyrim Item Sorter", "AutoSort Containers", "Sorting Hat", "Simple Crafting Storage", "Auto Mannequin",
  "Armor Set Display", "Better Mannequins" (name searches, 2026-10-14).

## Materials at the workbench

These overlap Dova Stash §8.5. Two styles: **menu hooks** that let the crafting menu count container contents
(no item moves) and **transfers** that move items to the player and back, like ours.

### Crafting Pulls Resources From Containers (most popular)
[Nexus 135613](https://www.nexusmods.com/skyrimspecialedition/mods/135613) · JerryYOJ · v1.1.1, updated
2025-08-10 (borderline) · E5497, D226k · SKSE DLL `CraftingPullFromContainer.dll` + INI, SE and AE.
"the stations LOOK for them, saving you from item transfer speed messes": scans player-owned containers within
`Range` (500 default) plus "permaLinks" set by console. The Legacy team ships a
[config](https://www.nexusmods.com/skyrimspecialedition/mods/137693) (E237, D76k, updated 2026-06-04) that links
LOTD's storage; that page notes recipes needing an ingredient *in the inventory* (vanilla leather) do not
activate. Inspired by DavidJCobb's LE [Crafting Pulls From Nearby Containers](https://www.nexusmods.com/skyrim/mods/104046).
Overlap: same goal, different method: **they do it better on speed and on homes with huge stores** (no
transfers, no carry weight spike); **we do it better on returning things** (nothing to return), multi-cell
homes (far containers), and recipe-aware selection. Compatibility: both on means our transfers still happen;
theirs then reads inventory first. Likely harmless but redundant. **[unverified]**

### Smart Crafting Storage
[Nexus 182056](https://www.nexusmods.com/skyrimspecialedition/mods/182056) · OriginalZee · v1.1.1, 2026-06-24 ·
E43 · CommonLibSSE-NG DLL `SmartCraftingStorage.dll` + ESL + MCM, AE 1.6.1170. Hook-based like the above, built
on it; player homes only by default (`LocTypePlayerHouse`), owned containers, skips favorited and quest items.
Page says it was "vibe coded". Same verdict as above.

### Transfer-based ones
- [Linked Crafting Storage](https://www.nexusmods.com/skyrimspecialedition/mods/64772) (Arctal, 2022-07-17,
  E888, D112k; `LinkedCraftingStorage.esp`): a Link Stone in up to 10 containers; any station anywhere pulls
  them in, +100000 carry weight, returns after a 7 s grace period.
- [Workbench Containers](https://www.nexusmods.com/skyrimspecialedition/mods/130090) (shazdeh2, 2025-04-01,
  E162, D26k): each station gets its own container (R at the station); its content comes and goes with use.
- [Crafting Containers](https://www.nexusmods.com/skyrimspecialedition/mods/92825) (Dylbill, 2023, E66):
  marked containers with a crafting type and distance; warns that scripted changes to a marked container can
  leave items unreturned.
- [Auto Chests](https://www.nexusmods.com/skyrimspecialedition/mods/68795) (fenix31415, 2022, E143, SKSE): menu
  reads nearby chests; crafted items go to a chest that already holds that type. Its page complains that
  LOTD's craftloot causes "5-8 sec lag" with a big store.
- [Handy Crafting and Spells](https://www.nexusmods.com/skyrimspecialedition/mods/59258) (ItsAlways710, v4.0.0,
  2026-07-10, E687, D167k): craftloot replacement from cloud storage. **v4 disables itself in player homes and
  LOTD locations** "to increase compatibility with ... Player Homes that have scripted auto-sorters". Good
  precedent.

Compatibility for the transfer-based ones: **conflict risk**. Two systems bring materials in at menu open and
take "unused" ones back at menu close. Ours returns what it brought to where it came from; theirs return what
was in their linked container. The same stack can be claimed by both, so items can end up in the other mod's
container. Nothing should be lost (both use real moves), but sorting is undone. **[unverified in game]**

## Homes with their own sorter

Dova Stash already skips cells from `Clockwork.esp` and `LegacyoftheDragonborn.esm` (DESIGN §9,
`game::SortsItself`). Other homes ship their own sorter; using both in one home means two systems moving the
same items (their sorting chest pushes out, our Sort all pulls back; their crafting retrieve plus ours).

| Home | Its system | Plugin | User |
|---|---|---|---|
| [Mirele Bismath Reborn](https://www.nexusmods.com/skyrimspecialedition/mods/93817) (E1974, 2023) | "Autosorting system" (`_rmp_boxofsorting`), own display scripts | `Mirele Bismath Reborn.esp` | Yes, both profiles |
| [Wynter's Breezehome Redux](https://www.nexusmods.com/skyrimspecialedition/mods/81607) (E658, 2023) | Optional strongroom: "Dump your loot into the strongroom's safe" (`Wynter_ItemSorter`); custom mannequins and racks, "uses none of the vanilla game mannequin behavior, nor any of its weapon racks" | `WynterBreezehomeRedux.esp` (overrides Breezehome) | Yes, both profiles |
| [LUFTSHIP K747](https://www.nexusmods.com/skyrimspecialedition/mods/53259) (E433, 2025-11-09) | Auto-sort master and lever (`_da_sgaautosort*`) | `DA_Skyship.esp` | Testing |
| [Skyfall Estate](https://www.nexusmods.com/skyrimspecialedition/mods/2185) (E7946, 2016) | "Auto Access/Store" for cooking, ingredient, smithing, soul storage; [extra autosort patch](https://www.nexusmods.com/skyrimspecialedition/mods/93165) | `SkyfallEstate.esp` | Testing |
| [Riverside Lodge](https://www.nexusmods.com/skyrimspecialedition/mods/9567) (E6861, 2017) | Category storage scripts (`rl_*storagescript`) | `RiversideLodge.esp` | Testing |
| [Elysium Estate](https://www.nexusmods.com/skyrimspecialedition/mods/4119) (E28475, 2018) | Linked storage (General Stores patches replace it) | `ElysiumEstate.esp` | Testing |
| [The Hoarder's Chest - Rebuilt](https://www.nexusmods.com/skyrimspecialedition/mods/60581) (E263, 2022) | One-button sort of the whole inventory, auto craft access | - | No |
| [The Dovah Den](https://www.nexusmods.com/skyrimspecialedition/mods/38376) (E696, 2020) | Auto-sort + crafting auto-loot (switchable), LOTD version | - | No |
| [Carpenter Storage Chests for Hearthfire](https://www.nexusmods.com/skyrimspecialedition/mods/95040) (kryptopyr, E186, 2023) | Auto-sort script on the three `BYOHHouseCraftingChest` building-supply chests, optional shared inventory | `Carpenter Chests.esp` | No |
| [Tundra Homestead - LotD Storage](https://www.nexusmods.com/skyrimspecialedition/mods/89836) (E82, D38k) | Converts Tundra Homestead's containers to LOTD storage: sorting chest, one-click sort trigger, sell pile, display drop-off | `LSESD_TundraHomestead.esp` (cell is from the CC plugin) | No |
| [Legacy Storage Extension Util](https://www.nexusmods.com/skyrimspecialedition/mods/25236) (E426, D90k) | Tool for patches that hook any home into LOTD storage and displays | many patches | No |

The Tundra patch matters most: our `SortsItself` check looks at the cell's own plugin (the Creation Club
file), so a player with that patch gets our shipped Tundra Homestead template auto-enabled on top of LOTD's
sorting. Same for any LSEU-based patch of a home we have a template for.

## Displays

### Bookshelves
- [Self Filling Bookshelves SE](https://www.nexusmods.com/skyrimspecialedition/mods/2050) (berticus0001, 2016,
  E988; LE [39314](https://www.nexusmods.com/skyrim/mods/39314)): sneak-activate a vanilla bookshelf, pick a type
  (standard / spell tomes / skill books, size, colour, alphabetical) and the shelf is emptied into the inventory
  and refilled from it, one of each title first. Overlap: **same feature**; **they have ordering options**
  (colour, alphabetical, large first); **we do it better** on automation (fills from storage on Deposit, never
  empties a shelf, respects room). Compatibility: low risk; their refill empties the shelf to the player first.
- [Unlimited Bookshelves](https://www.nexusmods.com/skyrimspecialedition/mods/2885) (E9023, D342k, 2016;
  `UnlimitedBookshelves.esp` + BSA) replaces the bookshelf script: up to 128 books or any item, placed by
  object bounds. Known duplication bug when picking an item off a shelf outside the menu
  ([Synergy patch page](https://www.nexusmods.com/skyrimspecialedition/mods/84987)). Compatibility: **conflict
  risk**. Dova Stash recognises a bookshelf by `MaxBooksAllowed`, `CurrentBookAmount`, `PlacedBook01` and
  `UpdateBooks` (DESIGN §8.4). If UB's script dropped those, its shelves are not displays to us (safe, but the
  feature silently does nothing); if it kept them with new meanings, our room count is wrong. **[unverified:
  script in BSA]** Must be tested.

### Weapon racks and display cases
- [Immersive Display Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/70508) (Ashen, v1.31,
  2024-02-13, E2085, D203k; Xbox and VR): recompiles `WeaponRackActivateSCRIPT` and `WeaponRackTriggerSCRIPT`
  (loose files, "should win any conflicts"): activating a rack opens a Gift Menu filtered to what fits; new
  display keywords. Overlap: a different way to place by hand; complement. **Conflict risk**: our rack fill
  relies on the scripts' `RackType`, `PlayersDroppedWeapon` and the trigger disabling the activator (DESIGN
  §8.4). It "preserves all fixes ... from the USSEP scripts", so the properties likely remain. **[unverified]**
- [Andrealletius Exploit Fixes](https://www.nexusmods.com/skyrimspecialedition/mods/46753) (v1.2.25,
  2026-09-27, E1746, D3.56M) also ships a `WeaponRackActivateSCRIPT` (Nexus file index). Very common in
  modlists; test rack filling with it. **[unverified]**
- [HearthFires Display Case Fix SE](https://www.nexusmods.com/skyrimspecialedition/mods/15104) (E7313, D1.82M)
  and [Creation Club Display Case Fix](https://www.nexusmods.com/skyrimspecialedition/mods/43485) (now in the
  Unofficial CC Content Patch, which the user runs) add activators to cases that were unusable. Complement:
  with them, more cases are displays for us.
- [Placeable Display Cases](https://www.nexusmods.com/skyrimspecialedition/mods/131107) (Icecreamassassin,
  2025-01-04, E312): craftable pedestals that show any item. Different (placed, managed through its own menu).

### Mannequins
- [Another Mannequin Script Fix](https://www.nexusmods.com/skyrimspecialedition/mods/56543) (E1212, D388k; the
  user runs it) replaces `MannequinActivatorSCRIPT` on the vanilla and DLC mannequins so they re-dress on load
  and stay put. [Mannequin Management](https://www.nexusmods.com/skyrimspecialedition/mods/38221)
  (Icecreamassassin, 2024-08-22, E4018, D1.46M) also ships a `MannequinActivatorSCRIPT`. Complements (they fix
  what our dressing relies on). Test that our dressing survives a reload with each.
- [Improved Mannequins (DAK)](https://www.nexusmods.com/skyrimspecialedition/mods/100834) (2023, E110) and
  [Mannequins Swap Gear](https://www.nexusmods.com/skyrimspecialedition/mods/83575) (2023, E35): equip
  everything from a mannequin or swap outfits with it. We never take anything off. **They have it.**
- [Wynter's Breezehome Redux](https://www.nexusmods.com/skyrimspecialedition/mods/81607) uses its own
  `Wynter_MannequinScriptExtended` and `Wynter_WeaponRackActivatorScript`. Whether we recognise them depends on
  whether they extend the vanilla scripts. **[unverified]**

### Legacy of the Dragonborn's own display and sorting tools
LOTD v6 has its own museum display system ([Rack O Matic](https://www.nexusmods.com/skyrimspecialedition/mods/31368)
says so), the Safehouse "household planner" sorts the Safehouse
([Kitchen Helper page](https://www.nexusmods.com/skyrimspecialedition/mods/63087)), and the
[Craftloot Sort and Storage FLM patches](https://www.nexusmods.com/skyrimspecialedition/mods/74810)
(E5619, SE/AE/VR, updated 2026-09-23) feed modded items into its craftloot, sorting and stash spell. The
[Curator's Companion](https://www.nexusmods.com/skyrimspecialedition/mods/38529) (E13327, updated 2026-10-08)
tracks found/displayed items. All handled by leaving `LegacyoftheDragonborn.esm` cells alone. The LOTD "Stash
Supplies" spell moves items to the Safehouse from anywhere; no conflict with our homes.

## Container-menu helpers (complements)
- [Better Container Controls for SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/25271) (CDCooley,
  2021, E5885, D897k; **user runs it**): Store Most / Take Most of the current view, never gold, lockpicks,
  keys, favorites. Fits our sort-on-arrival: Store Most into the master, we sort it out.
- [Quick Item Transfer](https://www.nexusmods.com/skyrimspecialedition/mods/125160) (shazdeh2, Quantumyilmaz,
  2025-12-16, E671, D556k; `QuickItemTransfer.dll`): take or drop everything of the current category ("T").
  Same fit. Both are good "recommended with" mentions.

---

## Gaps we could fill

Ranked by how many players use the mods that have them (Nexus counts; Reddit evidence unavailable).

1. **Craft from storage without moving items.** The hook approach is the most popular crafting mod here:
   [Crafting Pulls Resources From Containers](https://www.nexusmods.com/skyrimspecialedition/mods/135613)
   (E5497, D226k), endorsed by the Legacy team's [config](https://www.nexusmods.com/skyrimspecialedition/mods/137693);
   [Auto Chests](https://www.nexusmods.com/skyrimspecialedition/mods/68795) was written against transfer lag
   ("5-8 sec lag"). An option to make the crafting menu count the home's containers instead of moving items
   would remove our biggest weakness for hoarders. Note the leather-recipe limitation reported for the hook
   method.
2. **Resupply / restock to a quota.** [Home Auto Sort](https://www.nexusmods.com/skyrimspecialedition/mods/183379)
   (resupply key), [Automatic Item Storage](https://www.nexusmods.com/skyrimspecialedition/mods/8224) (E3077,
   "Storage Take"), [Storehouse](https://www.nexusmods.com/skyrimspecialedition/mods/81778) (refill on entering a
   home). Our quotas already say "keep 5 health potions"; "Retrieve to quota" on Deposit is a small step.
3. **Sell / junk chest.** [Player houses overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/190875),
   [Handy Crafting](https://www.nexusmods.com/skyrimspecialedition/mods/59258) (D167k),
   [I.M.P.S. Dynamic Autosale Chests](https://www.nexusmods.com/skyrimspecialedition/mods/88757) (E135, D59k),
   [Aeolus QOL](https://www.nexusmods.com/skyrimspecialedition/mods/101194). Out of our "never lose an item"
   comfort zone; a "junk" category container is the safe version.
4. **Remote / central storage.** [General Stores](https://www.nexusmods.com/skyrimspecialedition/mods/4322)
   (D594k), [Simple Cloud Storage](https://www.nexusmods.com/skyrimspecialedition/mods/59502) (E4989). Already
   planned as v2 (DESIGN §12).
5. **Mannequin outfit swap** ([Improved Mannequins](https://www.nexusmods.com/skyrimspecialedition/mods/100834),
   [Mannequins Swap Gear](https://www.nexusmods.com/skyrimspecialedition/mods/83575)): "wear this set" /
   "put my outfit on it" from the mannequin's hotkey menu.
6. **Bookshelf order** ([Self Filling Bookshelves](https://www.nexusmods.com/skyrimspecialedition/mods/2050)):
   alphabetical / by colour / large first.
7. **Blacklist from the inventory with a key** ([Player houses overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/190875));
   [Home Auto Sort](https://www.nexusmods.com/skyrimspecialedition/mods/183379) recommends
   [Favorite Misc Items](https://www.nexusmods.com/skyrimspecialedition/mods/42750) for the same job. Our
   "favorited" keep rule already covers it; worth saying on the Nexus page.

## Where we're clearly ahead (for the Nexus page)

- **Zero-touch homes.** 22 shipped templates, auto-enabled; no other any-home sorter ships setups (Home Auto
  Sort has export/import only, five cells in its menu).
- **Learning.** Put an item away once; no tokens ([Dynamic Autosorting](https://www.nexusmods.com/skyrimspecialedition/mods/18131)),
  fish ([Automatic Item Storage](https://www.nexusmods.com/skyrimspecialedition/mods/8224)), tool items
  ([AK Auto Sort](https://www.nexusmods.com/skyrimspecialedition/mods/44235)) or spells.
- **Displays in one mod.** Racks, mannequins with matched armor sets from the whole load order, plaques,
  cases and bookshelves, filled from storage. Others do one display type by hand
  ([Self Filling Bookshelves](https://www.nexusmods.com/skyrimspecialedition/mods/2050),
  [Immersive Display Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/70508)).
- **Sorting, displays and crafting together**, in any home, multi-cell homes included. Competitors do one or
  two of the three, or only inside their own home.
- **Native speed and safety.** SKSE moves with extra data kept, respawning containers refused, equipped,
  favorited and quest items kept. Papyrus sorters report slowness ([AK Auto Sort](https://www.nexusmods.com/skyrimspecialedition/mods/44235)
  profile) and item-loss risks ([Super Smart Sorting Storage](https://www.nexusmods.com/skyrimspecialedition/mods/125395)
  avoids `OnItemAdded` "resulting in lost items and CTDs").
- **No ESP, SE + AE + VR in one DLL.** Most competitors need an ESP; VR is rarely stated.
- **Leaves self-sorting homes alone** (Clockwork, LOTD), which [Handy Crafting](https://www.nexusmods.com/skyrimspecialedition/mods/59258)
  had to add in v4.
- **The player's own UI** (UIExtensions, QuickLoot IE, Menu Framework) and backups outside Data.

## Compatibility actions (prioritised)

1. **Generalise "sorts itself" into data and add the known self-sorting homes.** Ship a JSON list (plugins,
   optionally cells) read at startup instead of the two hard-coded names. Seed it with
   `LSESD_TundraHomestead.esp` (overrides our shipped Tundra Homestead template; key the check on "plugin loaded"
   because the cell belongs to the CC file), `PlayerHouse.esp`, `Mirele Bismath Reborn.esp`, `DA_Skyship.esp`,
   `SkyfallEstate.esp`, `RiversideLodge.esp`, plus Hoarder's Chest and Dovah Den once their plugin names are
   checked. For homes where only some containers sort themselves (`Carpenter Chests.esp` on
   `BYOHHouseCraftingChest`), mark those containers ignored instead of skipping the home. Mirele Bismath is in
   the user's active profiles: test it first.
2. **Don't auto-enable a template in a rebuilt cell.** Our Breezehome template meets
   [Wynter's Breezehome Redux](https://www.nexusmods.com/skyrimspecialedition/mods/81607) in the user's own
   Default profile, and all city-home templates meet [Player houses overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/190875).
   Either add a negative `requires` (for example `"conflicts": ["WynterBreezehomeRedux.esp"]`) or skip
   auto-enable when the cell record is overridden by a non-patch plugin and a share of the template's refs are
   missing or disabled; offer the template instead of enabling it.
3. **Workbench: detect the other crafting-supply mods and step aside.** On `CraftingPullFromContainer.dll`,
   `SmartCraftingStorage.dll`, `LinkedCraftingStorage.esp`, `Workbench Containers.esp` (and Dynamic
   Autosorting, Crafting Containers, `IA710_HandyCraftingAndSpells.esp` if its home switch is off), turn
   "Materials at the workbench" off by default with one log line and a note in the panel; the player can turn
   it back on. Test CPRFC with ours on (expected harmless) and LCS with ours on (expected items ending in the
   wrong container).
4. **Test displays against the common script replacers** and document results:
   [Immersive Display Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/70508) and
   [Andrealletius Exploit Fixes](https://www.nexusmods.com/skyrimspecialedition/mods/46753) (racks),
   [Unlimited Bookshelves](https://www.nexusmods.com/skyrimspecialedition/mods/2885) (bookshelves; if its
   script lacks our markers, say so; if it keeps them with a 128 limit, check room counting),
   [Mannequin Management](https://www.nexusmods.com/skyrimspecialedition/mods/38221) and Another Mannequin
   Script Fix (dressing after reload). Add rows to `docs/TESTING.md`.
5. **Home Auto Sort.** Detect `HomeAutoSort.dll`, log it, and document "set a home up in one of them". Later:
   import its JSON presets as templates (format to be checked from its source, which ships in its optional
   files).
6. **Nexus compatibility section.** Add: crafting-supply mods (what we do when they are present), Unlimited
   Bookshelves and display overhauls (test results), homes with their own sorter (left alone, list), and
   "works well with" Better Container Controls and Quick Item Transfer.
