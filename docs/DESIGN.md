# Dova Stash — Design Document

> Auto-sorting loot storage for **any** player home in Skyrim — vanilla, Creation Club, or mod-added —
> with shareable per-home templates so most players never have to set anything up.

Status: **Draft v0.1** · Target: SKSE plugin (CommonLibSSE-NG, Address Library) · Runtimes: SE 1.5.97, AE 1.6.x, VR

---

## 1. Goals & principles

**Core goal:** Let any player home have an auto-sorting loot mechanism, and let people author per-home
templates so that, for popular homes, setup is zero-touch.

Design principles, in priority order:

1. **Never lose an item.** Every move is a real `RemoveItem(... moveToRef)` that preserves extra data
   (enchantments, tempering, custom names, stolen flags, soul charge). If anything is ambiguous, the item
   stays where it is.
2. **Zero-config for the common case.** Drop-in install; homes with a template just work.
3. **Works anywhere it's safe.** Not limited to a hardcoded list of homes. Eligibility is decided by
   runtime safety checks (ownership, respawn), not by a whitelist.
4. **Data-driven.** Categories, keep rules, and home layouts are plain JSON. No ESP edits are needed to
   support a new home or a new item category.
5. **Minimal dependencies.** DLL-only. SKSE Menu Framework is a *soft* dependency: we use it when it is
   present and fall back to native message boxes otherwise (which is also the VR path).

### Non-goals (v1)

- Cross-home/central storage (planned for v2, see §12; the data model is designed for it).
- Moving items to or from containers in unloaded cells.
- Managing NPC/follower inventories (except possibly crafting-supply pulls in a later milestone).

---

## 2. Reference: Stash Organizer → Dova Stash mapping

Stash Organizer (FNV/TTW, by Eddoursul) is the model. Its v2 is per-cell; v3 is centralized.

| Stash Organizer                                        | Dova Stash (Skyrim)                                                                 |
|--------------------------------------------------------|-------------------------------------------------------------------------------------|
| Point at container + VATS key → mark **master**        | Look at a container + **Dova Stash hotkey** (VR: configurable activation gesture)  |
| Master container menu: process / retrieve / settings   | **Stash panel** (Menu Framework) or MessageBox menu (fallback/VR)                  |
| Secondary container → assign categories                | Same: any container in the cell can be bound to one or more categories             |
| Items put in a container become its default destination | Same: **learned destinations**, recorded from `TESContainerChangedEvent`           |
| Homeless items stay in master                          | Same                                                                                |
| Ignored containers                                     | Same: an `ignored` flag excludes a container from sorting and learning             |
| Keep / always-remove / quotas, per cell or global      | Same: keep rules with quotas, global defaults plus per-home overrides              |
| Keeps equipped / hotkeyed / pinned gear                | Keeps equipped, favorited, and quest items, plus gold, lockpicks and torches by default |
| Retrieve by category / perk / repair / crafting list   | Retrieve by category; "retrieve for crafting" is a later milestone                 |
| Hotkey into the air → highlight managed containers     | Same: apply a timed effect shader to managed containers                            |
| `Stash Organizer.ini` + `[Cell ID].ini` override       | `Layouts/` (player) + `Templates/` (community), resolved by cell identity (§6)     |
| Settings persist across playthroughs                   | Same: layouts live in JSON, not in the save                                        |
| Crafting pulls from storage                            | Milestone M5 (§13)                                                                 |
| Mojave Express (send items between stashes)            | Superseded by central storage (v2)                                                 |

---

## 3. Locked decisions

| Topic         | Decision                                                                     |
|---------------|------------------------------------------------------------------------------|
| Storage model | Per-home in v1. The data model is designed so central storage can be added in v2 without migration pain |
| UI            | SKSE Menu Framework (QTR-Modding v3 API) as the primary UI, with a native `MessageBox` fallback |
| Plugin file   | **DLL-only** for v1. An ESL-flagged ESP is added later only if central storage requires it |
| Runtimes      | SE 1.5.97 + AE 1.6.x + VR, as one DLL via CommonLibSSE-NG                    |
| Library       | `alandtse/CommonLibVR` branch `ng` (actively maintained CommonLibSSE-NG)     |

---

## 4. Player experience

### 4.1 Zero-touch (template exists)

1. Player enters Breezehome (or any home with a template).
2. Dova Stash finds a matching template and shows a one-time notification:
   *"Dova Stash: Breezehome layout available (by &lt;author&gt;). Press [Hotkey] at any container to enable."*
   Auto-enable on first entry is a config option, off by default.
3. Player presses the hotkey: **Deposit** runs and loot flows into the right containers.

### 4.2 Manual setup (no template)

1. Look at a container and press the hotkey → *"Make this the Dova Stash master container?"*
2. The master's menu has these actions:
   - **Deposit**: move eligible inventory items using the keep rules
   - **Sort**: redistribute everything in the master
   - **Retrieve…**: by category, or everything
   - **Containers**: highlight managed containers
   - **Keep rules**
   - **Export as template**
   - **Reset**
3. Look at any other container and press the hotkey to open its binding menu: assign categories, set
   it as ignored, rename it, or promote it to master.
4. Or skip binding entirely: just **put items into containers by hand**. Each placement teaches Dova Stash
   where that item belongs (learned destinations).

### 4.3 Day-to-day loop

- Come home and press the hotkey (from anywhere in the cell, or at the master) → **Deposit**.
- Dump things into the master and close it → items with a known destination move automatically.
- Need arrows? Look at the master → **Retrieve → Ammo**.

---

## 5. Architecture

```
┌─────────────────────────────── DovaStash.dll ───────────────────────────────┐
│                                                                             │
│  Plugin entry (SKSEPluginLoad) ── Logging (spdlog) ── Settings (INI/JSON)   │
│                                                                             │
│  ┌──────────── Input ────────────┐   ┌──────────── Events ───────────────┐  │
│  │ Hotkey sink (BSInputDevice)   │   │ CrosshairRefEvent (SKSE)          │  │
│  │ VR: activation gesture        │   │ MenuOpenCloseEvent (ContainerMenu)│  │
│  └──────────────┬────────────────┘   │ TESContainerChangedEvent          │  │
│                 │                    │ TESCellFullyLoadedEvent / PlayerCell│ │
│                 ▼                    └───────────────┬───────────────────┘  │
│        ┌──────────────── Controller ─────────────────┴──┐                   │
│        │ "what does the hotkey mean here?" state machine│                   │
│        └───────┬───────────────────┬────────────────────┘                   │
│                ▼                   ▼                                        │
│   ┌──── HomeRegistry ────┐   ┌──── SortEngine ─────────────┐                │
│   │ eligibility checks   │   │ deposit / distribute /      │                │
│   │ layout resolution    │   │ retrieve / learn            │                │
│   │ (Layouts>Templates)  │   │ KeepRules, ItemMover        │                │
│   └──────────┬───────────┘   └──────────┬──────────────────┘                │
│              ▼                          ▼                                   │
│   ┌──── Store (JSON I/O) ────┐   ┌──── CategoryEngine ─────┐                │
│   │ Layouts/, Templates/,    │   │ compiled predicates from│                │
│   │ Categories/, settings    │   │ Categories/*.json       │                │
│   └──────────────────────────┘   └─────────────────────────┘                │
│                                                                             │
│  UI facade ──► MenuFrameworkUI (ImGui, if installed and not VR)             │
│            └─► MessageBoxUI   (native fallback, VR)                         │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Threading

All inventory mutation happens on the **main thread** through `SKSE::GetTaskInterface()->AddTask`.
Event sinks and ImGui callbacks only enqueue work. Sorting is batched: there is one task per operation,
and very large inventories are chunked across frames to avoid hitches.

---

## 6. Data model & files

Settings live in `Data/SKSE/Plugins/DovaStash.ini` (hotkey, auto-enable, logging level, UI mode).
All data paths are under `Data/SKSE/Plugins/DovaStash/`.

```
Categories/
  00_Default.json             shipped category definitions
  50_<anything>.json          add-on packs (e.g. a Survival Mode or Requiem pack); loaded in name order
Templates/                    community/shipped layouts — read-only from the mod's perspective
  Vanilla/Breezehome.json
  <AnyAuthor>/<AnyName>.json  file names are free-form; identity is inside the file
Layouts/                      player-owned layouts — written by the mod
  Skyrim.esm_000165A7.json    one file per home cell
```

### 6.1 Identity: how we refer to forms across load orders

Every form reference in JSON is written as **`"<PluginFile>|0x<LocalFormID>"`**, for example
`"Skyrim.esm|0x000165A7"`. This is stable across load-order changes and ESL compaction. It is resolved at
runtime via `TESDataHandler::LookupForm(localID, plugin)`.

- Keywords may also be referenced by **EditorID** (`"VendorItemGem"`), because keyword EditorIDs are kept in
  memory at runtime.
- An unresolvable reference (plugin removed) is logged and **skipped, never deleted**. User data
  survives a temporarily disabled mod.

### 6.2 Layout resolution (per cell)

On entering an eligible cell:

1. `Layouts/<cell>.json` exists → use it (the player's own setup always wins).
2. Otherwise, use every template in `Templates/**` whose `cell` matches:
   - With one match, offer it.
   - With several, pick the one with the highest `priority`, then the newest `version`. The UI lists the
     alternatives.
3. Applying a template **copies** it into `Layouts/`, with a `"basedOn"` back-reference. Later edits
   never touch the template, and updating a template pack never clobbers player tweaks. A
   "Re-apply template" action exists.

### 6.3 Layout / template schema (v1)

```jsonc
{
  "$schema": "dovastash-layout-1",
  "name": "Breezehome",
  "author": "Dova Stash",
  "version": "1.0.0",
  "priority": 0,
  "requires": ["Skyrim.esm"],             // all must be loaded for the template to be offered
  "cell": "Skyrim.esm|0x000165A7",        // illustrative ID — real IDs get verified in xEdit
  "master": "Skyrim.esm|0x000AAAAA",      // container reference (REFR), not base CONT
  "containers": [
    {
      "ref": "Skyrim.esm|0x000BBBBB",
      "label": "Armory chest",
      "categories": ["weapons.*", "armor.heavy", "armor.light", "armor.shields"]
    },
    {
      "ref": "Skyrim.esm|0x000CCCCC",
      "label": "Alchemy satchel",
      "categories": ["alchemy.ingredients", "alchemy.potions", "alchemy.poisons"]
    },
    { "ref": "Skyrim.esm|0x000DDDDD", "label": "Display case", "ignored": true }
  ],
  "keep": { "inherit": true, "rules": [ { "category": "ammo.arrows", "quota": 50 } ] },

  // Player layouts only (never in shipped templates):
  "learned": { "Skyrim.esm|0x0001397E": "Skyrim.esm|0x000BBBBB" },
  "basedOn": "Templates/Vanilla/Breezehome.json@1.0.0"
}
```

Version-2 forward compatibility: a layout describes **bindings of categories to storage endpoints**.
In v2 an endpoint can also be `{"remote": "central"}`, so v1 files remain valid.

### 6.4 Save game vs. JSON

Layouts are global (cross-playthrough), as in Stash Organizer. v1 needs **no SKSE co-save**. The only
per-save state we might add later is "dismissed template notifications", which can also live in
JSON keyed by character name and ID.

---

## 7. Category engine

Categories are hierarchical IDs (`weapons.swords`, `alchemy.potions`), matched by compiled predicates.
Bindings accept wildcards (`weapons.*`). Evaluation order for a given item:

1. Per-item **explicit binding** in the layout (rare; the escape hatch)
2. **Learned** destination for that exact base form
3. First matching **category** by priority, then specificity (deeper IDs win over shallower ones)
4. No match → item stays in / goes to **master** ("homeless")

### 7.1 Category definition schema

```jsonc
{
  "$schema": "dovastash-categories-1",
  "categories": [
    {
      "id": "misc.gems",
      "name": "Gems",
      "priority": 100,
      "match": {
        "formTypes": ["MISC"],
        "keywordsAny": ["VendorItemGem"]
      }
    },
    {
      "id": "weapons.bows",
      "name": "Bows",
      "match": { "formTypes": ["WEAP"], "weaponTypes": ["Bow"] }
    },
    {
      "id": "books.skill",
      "name": "Skill books",
      "match": { "formTypes": ["BOOK"], "bookTeaches": "skill" }
    }
  ]
}
```

Predicate vocabulary (v1): `formTypes`, `keywordsAny`, `keywordsAll`, `keywordsNone`, `forms` (explicit
list), `formLists`, `plugins` (source mod), `weaponTypes`, `armorTypes` (heavy/light/clothing),
`bookTeaches` (skill/spell/none), `alchemy` (potion/poison/food), `soulGem` (empty/filled),
`enchanted` (bool), and `ammo` (arrow/bolt). Predicates combine with AND, and lists combine with OR.

The keyword-based approach automatically benefits from **KID (Keyword Item Distributor)**. Item packs
that are distributed with proper vendor keywords sort correctly with no Dova Stash patch.

### 7.2 Default category set (shipped)

`weapons.{swords, greatswords, daggers, axes, waraxes, maces, warhammers, bows, crossbows, staves}`,
`armor.{heavy, light, clothing, shields, jewelry}`, `ammo.{arrows, bolts}`,
`alchemy.{potions, poisons, food, ingredients}`, `books.{skill, spelltomes, notes, other}`, `scrolls`,
`soulgems.{empty, filled}`,
`misc.{ores_ingots, gems, hides_pelts, animal_parts, dwemer, clutter, building_materials, tools}`, `keys`.

---

## 8. Sort engine

### 8.1 Operations

| Op             | Source → Target                       | Trigger                                                           |
|----------------|---------------------------------------|-------------------------------------------------------------------|
| **Deposit**    | Player → destinations (or master)     | Hotkey menu, or optional auto-deposit on cell entry               |
| **Distribute** | Master → destinations                 | `ContainerMenu` closes on the master; also a "Sort" button        |
| **Sort all**   | Master + bound containers → correct one (see §16) | Manual (e.g. after changing bindings)                             |
| **Retrieve**   | Destinations → player                 | Menu: by category, or everything                                  |
| **Learn**      | Records item → container              | `TESContainerChangedEvent` (player → non-master managed container) while `ContainerMenu` is open on that container |

### 8.2 Moving items safely (`ItemMover`)

- Iterate `GetInventory()`. For each `InventoryEntryData`, move every `ExtraDataList` stack separately
  via `RemoveItem(obj, count, kStoreInContainer, extraList, target)`, then move the plain remainder.
  This preserves tempering, player enchantments, names, stolen ownership, and soul charge.
- **Always skip:** quest items (`IsQuestObject`), non-playable forms, equipped (`ExtraWorn`/`ExtraWornLeft`),
  favorited (`ExtraHotkey`), and anything matched by keep rules. Gold, lockpicks and torches are kept by
  default (configurable).
- Never sort *while* a `ContainerMenu` is open on an affected container. Defer to the menu close to
  avoid UI desync.
- Every operation logs a summary (`moved 143 items in 9 stacks to 6 containers`). A debug option writes a
  per-item journal.

### 8.3 Keep rules

```jsonc
"keep": {
  "always":  ["equipped", "favorited", "quest", "Skyrim.esm|0x0000000F" /* gold */, "Skyrim.esm|0x0000000A" /* lockpick */],
  "never":   ["misc.clutter"],
  "quotas":  [ { "category": "alchemy.potions", "keywordsAny": ["MagicAlchRestoreHealth"], "quota": 5 } ]
}
```

Global defaults live in `DovaStash.ini`/`Settings.json`. A layout may override them or set `"inherit": true`.

---

## 9. Home eligibility & safety

A cell can host a Dova Stash layout when **all** of the following hold:

- It is an interior cell, or an exterior cell that is player-owned.
- It is player-owned (`TESObjectCELL::GetOwner()` is the player or the player faction), **or** unowned, **or**
  its location has the `LocTypePlayerHouse` keyword. That keyword covers most vanilla, CC, and modded homes.

Per-container checks when binding:

- Reject containers whose base `TESObjectCONT` has the **`kRespawn`** flag, unless the cell's encounter zone
  is flagged *never resets* or the location is a player house. Rejected containers show a clear warning.
  Respawning containers are the #1 way to lose items in Skyrim.
- Reject owned containers whose owner is not the player.
- Skip disabled references (for example, unbuilt Hearthfire furniture). They become valid automatically
  once built.

Templates bypass none of these checks. A template binding to a now-respawning container (because of a
mod conflict) is disabled with a warning.

---

## 10. UI

### 10.1 Facade

```cpp
struct IStashUI {
    virtual void OpenMasterMenu(Home&, RE::TESObjectREFR* master) = 0;
    virtual void OpenContainerMenu(Home&, RE::TESObjectREFR* container) = 0;
    virtual void OpenSetupPrompt(RE::TESObjectREFR* container) = 0;
    virtual void Notify(std::string_view msg) = 0;
};
```

- **MenuFrameworkUI:** chosen when `SKSEMenuFramework::IsInstalled()` returns true and the runtime is not VR. Uses
  `AddWindow` for the contextual stash panel and `SetSection`/`AddSectionItem("Dova Stash/...")` for a
  global settings page and a template browser. This is the rich editor: category trees, multi-select
  binding, keep-rule quotas, and template export.
- **MessageBoxUI:** native `RE::MessageBoxData` chains with pagination. It covers every *essential* action
  (enable, bind categories, deposit, retrieve, keep defaults). Advanced editing on VR is done by editing
  JSON or by using a flat-screen session.

Header: [QTR-Modding/SKSE-Menu-Framework-3-API](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API).
It loads the framework dynamically, so it is a soft dependency. The header is LGPL-2.1 (compatible with
our GPL-3.0-or-later, inherited from CommonLibSSE-NG).

### 10.2 Container highlight

A hotkey press with no crosshair target applies a timed `ApplyEffectShader` to managed containers in the
cell, using distinct shaders for the master, bound, and ignored containers. The shaders reuse vanilla
`EFSH` forms (no ESP).

---

## 11. Input

- **Flat:** one configurable hotkey (keyboard and gamepad), read via a `BSInputDeviceManager` event sink.
  The crosshair target comes from `SKSE::CrosshairRefEvent`.
  - Tap on a container opens the contextual menu.
  - Tap on nothing in an enabled home opens the master menu or Deposit (configurable).
  - Hold briefly to highlight containers.
- **VR:** no keyboard. The candidate approach is "activate a container **while sneaking**" to open the
  Dova Stash menu instead of the container (vtable hook on container activation), configurable. To be
  validated by a VR tester.

---

## 12. Road to central storage (v2 preview)

- Introduce `DovaStash.esl` (ESL-flagged) holding one persistent storage container in a holding cell.
  Persistence is required because non-persistent refs in unloaded cells are not in memory.
- Layout endpoints gain `{"remote": "central"}`. Homes become *access points*, as in Stash Organizer v3.
- Deposit from any enabled home routes overflow or selected categories to central storage. Retrieve can
  pull from central storage anywhere a home is enabled.
- No change is needed to v1 layout or template files.

---

## 13. Milestones

| #      | Scope                                                                                                         | Exit criteria |
|--------|---------------------------------------------------------------------------------------------------------------|---------------|
| **M0** | Project scaffold: CMake + vcpkg + CommonLibSSE-NG submodule, SE/AE/VR DLL builds, logging, deploy to MO2      | DLL loads in 1.6.1170 and logs "Dova Stash loaded" |
| **M1** | Hotkey + crosshair → MessageBox: mark master, Deposit (all to master), keep defaults, ItemMover with extra data | Enchanted, tempered and renamed items survive round-trips |
| **M2** | Category engine + default categories; bind containers; Distribute on master close; Learn; Retrieve by category; JSON layouts | Full manual workflow in Breezehome |
| **M3** | Templates: loader, resolution, apply/re-apply, export; vanilla home template pack                               | All 9 vanilla homes zero-touch |
| **M4** | Menu Framework UI: panel, category tree, keep-rule editor, template browser; container highlight               | Feature parity plus the editor |
| **M5** | Crafting supply pulls (forge/alchemy/enchanting while in an enabled home), quotas, polish, Nexus release        | Release candidate |
| **v2** | Central storage (ESL), cross-home access                                                                      | — |

---

## 14. Build & repository layout

```
DovaStash/
  CMakeLists.txt
  CMakePresets.json            presets: release / debug; ENABLE_SKYRIM_SE, _AE, _VR = ON
  vcpkg.json                   spdlog, nlohmann-json (or glaze), simpleini
  extern/CommonLibSSE-NG/      git submodule → alandtse/CommonLibVR @ ng
  extern/SKSEMenuFramework/    vendored API header (LGPL-2.1)
  src/
    main.cpp                   SKSEPluginLoad, messaging listener
    Input/  Events/  Home/  Sort/  Categories/  Store/  UI/
  dist/Data/SKSE/Plugins/DovaStash/{Categories,Templates}/   shipped data
  docs/DESIGN.md  docs/TEMPLATE_AUTHORING.md
  tests/                       pure-logic unit tests (category matching, JSON round-trip, ID parsing)
```

Toolchain on this machine: VS 2022 Build Tools (MSVC v143) and CMake are present. **vcpkg needs to be
installed** (clone, bootstrap, and set `VCPKG_ROOT`). Post-build copies the DLL and PDB into the MO2 instance
`E:\modding\Kexodus Skyrim\mods\Dova Stash\`.

Engine-independent logic (category predicates operating on a thin `ItemView` interface, JSON schemas, ID
parsing, keep-rule math) is kept separate from CommonLib calls so it can be unit-tested without the game.

---

## 15. Open questions & risks

| #  | Question / risk                                                                     | Proposed handling |
|----|-------------------------------------------------------------------------------------|-------------------|
| 1  | Default hotkey choice (avoid vanilla and common-mod conflicts)                      | Pick during M1; always rebindable |
| 2  | VR input gesture (sneak+activate hook) is untested by us                            | Ship as experimental; recruit a VR tester |
| 3  | `RemoveItem` HUD spam / sounds during large deposits                                | Test in M1; suppress or batch notifications if needed |
| 4  | Interaction with QuickLoot IE / other container-UI mods                             | Our hotkey is independent of activation; test the learn path with QuickLoot transfers |
| 5  | Multi-cell homes (e.g. Hearthfire homes + cellar)                                   | v1: one layout per cell. v2 central storage bridges them |
| 6  | Template trust: templates are data only (no code), but bad IDs could bind wrong refs | Validate that the ref's base form is a `CONT` and lives in the declared cell; reject otherwise |
| 7  | JSON library: nlohmann-json (ubiquitous) vs glaze (fast, compile-time)              | nlohmann-json for readability; layouts are small |
| 8  | Mod name collisions on Nexus                                                        | Check before release |

---

## 16. Decisions resolved during implementation (v0.1)

- **Category priority:** higher wins. Ordering is priority (desc), then id depth (desc), then definition
  order. A later category file that redefines an id overrides it in place.
- **Patterns:** only `*` and a trailing `.*` are allowed. `a.*` matches `a` and its descendants. Items
  with no category are *not* caught by a `*` binding; they go to the master.
- **Same category on several containers:** this acts as an ordered fallback chain. The most specific
  pattern wins, then list order. Unavailable containers are skipped, for example unbuilt Hearthfire wings
  (disabled refs).
- **Explicit per-item bindings:** `"forms": [FormRef…]` on a container entry.
- **Keep:** both `quotas` and `rules` are accepted. `inherit` defaults to true. Quotas keep the most
  valuable stacks first. Quest and non-playable items are always kept.
- **Learn:** an unknown container that passes the safety checks is added to the layout with no
  categories. Learning is skipped when the container is already the item's rule-based destination.
- **Eligibility (§9) addition:** a cell owned by a faction the player belongs to is eligible. Severin
  Manor is owned by `DLC2RRSeverinManorFaction` and has no `LocTypePlayerHouse` keyword.
- **Plugin names** are compared case-insensitively everywhere. Steam stores CC files in lower case.
- **Vanilla data quirks:** firewood's keyword is `VendorItemFireword` (a vanilla typo). Hearthfire
  building materials and Dwemer parts have no shared keyword, so they use explicit `forms` lists. Letters
  carry `VendorItemBook`, so `books.notes` is weak until OCF keywords are supported.
- **Template authoring tool:** `tools/esm_dump.py` (see `tools/README.md`).
- **Sort all sources:** only the master and containers with at least one category binding or explicit
  `forms` entry are emptied by Sort all. Containers without bindings (typically added by Learn:
  "keepsake chests") are targets only, for learned items, and are never emptied.
- **Nameless items** (empty base name, usually script tokens that mods track state with) are never
  moved, learned or counted: they are invisible in container menus.
- **Applying a template over a player layout** first writes a timestamped backup
  (`<file>.json.<YYYYMMDD-HHMMSS>.bak`, newest 5 kept; Reset uses the same scheme). Only category
  bindings come from the template; learned destinations, keep rules, ignored flags, explicit forms,
  labels, unknown keys, unparsed entries and containers the template does not list (kept without
  categories) are preserved (see `Template.h`).
- **Layout file identity:** the file name decides the cell. A layout whose `cell` differs from its
  file name is used for (and saved to) the file's cell, with a warning. A layout whose `containers`
  is not an array or whose `learned` is not an object is treated as broken and never overwritten.
- **Scripted containers:** containers with attached Papyrus scripts (bookshelves, display cases...)
  are never added by Learn, and binding one shows the warning "This container runs a script and may
  reject or return items". Detection reads the VM's attached-script table for the reference's handle.
- **Container ownership:** a container without its own owner inherits the cell's owner. A foreign
  cell owner rejects the container, except in a `LocTypePlayerHouse` cell (accepted by §9), where it is
  a warning.
- **Plain remainder next to a protected copy:** the plain (no extra data) remainder of an item is not
  moved out of the player's inventory while another copy is worn, favorited or a quest item, because
  `RemoveItem` without an extra list lets the engine choose the copies. To be revisited after in-game
  verification (TESTING §2.6).
