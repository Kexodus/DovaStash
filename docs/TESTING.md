# Dova Stash: in-game test script (M1 + M2 + M4)

This script is run by hand in Skyrim. It covers the M1 exit criterion (enchanted, tempered and
renamed items survive round-trips), the M2 exit criterion (the full manual workflow in
Breezehome) and the M4 Menu Framework panel (section 5). Section 8 is the Dova Stash Editor, outside the game.
Unit tests for `src/core` are separate: `ctest --preset core-tests`.

Test rig: Skyrim AE 1.6.1170 through MO2, with SKSE and Address Library installed. Optional mods:
QuickLoot IE, SKSE Menu Framework, KID.

## 0. Setup

1. Build and deploy the plugin (see `docs/BUILDING.md`). The MO2 mod folder must contain:
   - `SKSE/Plugins/DovaStash.dll`
   - `SKSE/Plugins/DovaStash.ini`
   - `SKSE/Plugins/DovaStash/Categories/00_Default.json`
   - `SKSE/Plugins/DovaStash/Templates/...`
2. Set `iLogLevel = 1` (debug) in `DovaStash.ini` to log one line per moved stack, and
   `bAutoEnableTemplates = false` (on by default) so the template notice and *Use template* can be tested;
   §3.6 step 4 turns it back on.
3. Open the log while you play:
   `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash.log`.
4. Player layouts are written to `Data/SKSE/Plugins/DovaStash/Layouts/`. Under MO2 they appear in
   the **overwrite** folder. Delete `Skyrim.esm_000165A8.json` there to start Breezehome from scratch,
   and also its copies in `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups/`
   (`Skyrim.esm_000165A8.json*`): otherwise the home offers *Restore your last setup* (§3.6a) instead of
   the template notice, and the setup prompt lists it first. "A clean Breezehome" below means both.

The default hotkey is **K** (`iHotkey = 37`).

### Expected startup log

```
Dova Stash v0.2.4 loaded
Settings: iLogLevel=1 iHotkey=37 iGamepadHotkey=-1 bAutoEnableTemplates=false bDistributeOnMasterClose=true bLearn=true iMode=0
kDataLoaded: forms are available
FormUtil: <n> full + <m> light plugins loaded
Categories: 37 loaded (0 errors, 0 warnings)
Templates: 22 loaded (0 errors, <w> warnings)
Keep defaults: built-in (no Data/SKSE/Plugins/DovaStash/Settings.json)
Home backup: copies go to C:/Users/<you>/Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups
UI: native message boxes                      (with SKSE Menu Framework: "SKSE Menu Framework: stash window and
                                              "Dova Stash" section registered" + "UI: SKSE Menu Framework panel")
Events: container-changed and cell-loaded sinks registered
Events: menu sink registered
Events: crosshair sink registered
Input: hotkey sink registered (key 37, gamepad -1)
kPostLoadGame (success: true)
Events: player cell sink registered
```

**Fail if** you see any `[error]` line during startup. Template warnings are acceptable, for example
a `requires` plugin that isn't loaded.

## 1. Eligibility and safety (§9)

| # | Steps | Expected |
|---|---|---|
| 1.1 | `coc WhiterunBreezehome` | Log: `Cell Breezehome (Skyrim.esm\|0x000165A8): template available` and `template: Breezehome v1.0.0 by Dova Stash ...`. One HUD line: *Dova Stash: Breezehome setup available (by Dova Stash). Press K at any container to enable.* |
| 1.2 | Leave and re-enter Breezehome in the same session | No second notification. |
| 1.3 | `coc WhiterunBanneredMare` and press K at nothing | HUD: *Dova Stash: not a player home (owned by ...)*. No menu opens. |
| 1.4 | `coc BleakFallsBarrow01` (an unowned interior, so the cell passes). Look at a barrel or an urn and press K | HUD: *can't use Barrel (this container respawns its contents (items would be lost))*. No menu opens. |
| 1.5 | Go outside (`coc WhiterunOrigin` or any exterior) and press K | HUD: *not a player home (exterior cell not owned by you)*. |
| 1.6 | In Breezehome, look at Lydia (or a door) and press K | It acts as if nothing were targeted: the setup prompt with only the template option. |
| 1.7 | Open the console, then the inventory, then a dialogue, and press K in each | Nothing happens. The debug log shows `Hotkey ignored`. |
| 1.8 | Lakeview Manor (`coc BYOHHouse1Exterior`, then go inside) before building any furniture: look at an unbuilt container spot | Unbuilt (disabled) furniture can't be targeted. After the template is applied, the log lists each one: `Template container ... unavailable: container is disabled (not built yet?)`. |
| 1.9 | Severin Manor (Dragonborn), after you buy it | The cell is accepted. It is owned by `DLC2RRSeverinManorFaction`, which you join on purchase. Before the purchase: *not a player home (owned by ...)*. |

## 2. M1: master, Deposit, keep defaults, extra data

Start from a clean Breezehome (no layout file).

### 2.1 Make a master

1. `coc WhiterunBreezehome`. Look at the chest next to the alchemy lab and press **K**.
2. The prompt *Dova Stash: set up Breezehome?* shows these buttons: *Make Chest the master*,
   *Use template "Breezehome"*, *Cancel*. Choose **Make ... the master**.
3. HUD: *Dova Stash: Chest is now the master container*. The log shows `Saved layout .../Skyrim.esm_000165A8.json`.
   That file contains `"master"` and no containers.

### 2.2 Build a test inventory

```
player.additem 0000000F 500      ; gold (kept by default)
player.additem 0000000A 10       ; lockpicks (kept by default)
player.additem 0001D4EC 3        ; torches (kept by default)
player.additem 00012EB7 3        ; Iron Sword x3
player.additem 0001397E 2        ; Iron Dagger x2
player.additem 0001397D 50       ; Iron Arrow x50
player.additem 0003EADD 4        ; Potion of Minor Healing
```

Use `help "<name>" 4` to look up any other form id. Now set up these special stacks:

- **Enchanted:** `help "of embers" 4` lists an enchanted weapon, e.g. *Iron Sword of Embers*.
  Add one with `player.additem <id> 1`.
- **Tempered:** temper one Iron Sword at a grindstone (Warmaiden's, outside). It shows as *Iron Sword (Fine)*.
  Add materials with `player.additem 0005ACE4 5` (iron ingot).
- **Player-enchanted + renamed:** enchant an item at the Arcane Enchanter in Dragonsreach (Farengar's study)
  and give it a custom name. Add a filled soul gem with `help "grand soul gem" 4` and `player.additem`.
- **Filled soul gem with captured soul:** use Soul Trap on a creature with an empty gem. That makes an
  `ExtraSoul` stack, which differs from a pre-filled base form.
- **Stolen:** in Belethor's shop (`coc WhiterunBelethorsGeneralGoods`), take an item from the counter
  without paying, so it gets the red hand. Or `player.additem` an item and then steal one.
- **Equipped:** equip one Iron Sword and the Iron Arrows.
- **Favorited:** favorite one Potion of Minor Healing.
- **Quest item:** use a save that carries a quest item (a quest-icon item in the inventory, e.g. the
  Golden Claw during *Bleak Falls Barrow*).

Write down the inventory list, including names, *(Fine)* suffixes, enchantments and the stolen flag.

### 2.3 Deposit (everything goes to the master: no categories bound yet)

1. In Breezehome, press K at nothing. The master menu shows these buttons: *Deposit*, *Sort all*,
   *Retrieve...*, *Browse containers...*, *Use template "Breezehome"*, *More...*, *Cancel* (message
   boxes; *More...* has *Save as template...* and *Reset home...*). The UIExtensions list shows them
   all in one list (§6.2.1).
2. Choose **Deposit**.
3. HUD (one line, no per-item spam): *Dova Stash: deposited N items into 1 container*.
4. The log shows `Deposit in Breezehome (Skyrim.esm|0x000165A8)` and
   `ItemMover: moved N items in M stacks from 1 to 1 containers (0 skipped, 0 errors)`.

**Expected outcome:**

- These stay in the inventory: gold, lockpicks, torches, the **equipped** sword, the equipped arrows,
  the **favorited** potion, and the **quest item**. The spare plain Iron Swords go to the master while
  the equipped one stays equipped (see 2.6). The log shows
  `Moved Iron Sword x2 (plain, beside a special copy) <player> -> ...`.
- These are now in the master: the daggers, the other potions, the enchanted sword, the *(Fine)*
  sword, the renamed player-enchanted item, the soul gems and the stolen item.
- **Fail if** any of these happen:
  - an item vanished, so the inventory plus master counts differ from your list
  - the *(Fine)* suffix, enchantment, custom name, soul or stolen flag is gone
  - the log has a `[error]` line, especially `Move mismatch` or `disturbed a worn/favorited/quest stack`

### 2.4 Round-trip (M1 exit criterion)

1. Master menu → **Retrieve...** → **Everything (N)**. HUD: *retrieved N items from 1 container*.
2. Check the inventory against your list. Every special stack is still special: tempered, enchanted,
   renamed, soul, stolen.
3. Deposit again, then retrieve again. Nothing changes after a second round-trip.
4. Open the master by hand and take/store a few items. The vanilla UI still works, and nothing gets
   duplicated or desynced.

### 2.5 Container-menu guard

1. Open the master (activate it) and keep the ContainerMenu open.
2. You can't press K: the menu uses the cursor, so the hotkey is ignored.
3. With **QuickLoot IE**, look at the master without opening it and press K. The master menu opens
   because QuickLoot's overlay doesn't block the hotkey. Deposit works, and no operation runs while a
   real ContainerMenu is open. If one is open, the log shows
   `Operation dropped: a ContainerMenu stayed open` after about 5 s.

### 2.6 Plain copies next to a special copy

Moving the plain (no extra data) remainder of an item uses `RemoveItem` with no extra list. When a copy
with extra data stays in the source (worn, favorited, quest, tempered, enchanted, renamed or stolen), the
engine takes plain copies first, as the container menu does. Dova Stash checks every such move: if a
special copy left anyway, it is moved back, the log has an `[error]` line
`Move of plain ... took a tempered, enchanted, renamed or worn copy; put N back`, and from then on plain
copies next to a special copy stay where they are (`Move skipped: ... left in ...`) until the game restarts.

1. `player.additem 0001397E 3` (Iron Dagger x3) and equip one dagger.
2. Deposit.
3. **Expected:** 2 daggers go to their destination, the equipped one stays equipped. The log has
   `Moved Iron Dagger x2 (plain, beside a special copy) <player> -> ...` and no `[error]` line.
4. Temper one of two Iron Swords at a grindstone, put a keep quota of 1 on swords, Deposit. **Expected:**
   the tempered sword stays with you (quota) and keeps its tempering; the plain one goes to its destination.
5. Put a tempered and a plain Iron Sword into the master: both are sorted (the tempered one moves first).
6. **Fail if** the `Move of plain ...` error appears: send the log (the engine chose a special copy).

### 2.7 Items that come back when stored

1. Get an item whose script hands it back when stored (for example the Curators Storage Token from
   Legacy of the Dragonborn) and Deposit.
2. **Expected:** its prompt shows once. The log has
   `... came back from <container> right after it was stored (a script returns it); it stays where it is from now on`.
3. Deposit again: the item stays on you (`stays: comes back when stored`), and no prompt appears.

## 3. M2: categories, binding, Distribute, Learn, Retrieve

### 3.1 Bind containers

1. Look at the cupboard in the loft (by the weapon plaque) and press K. The container menu shows
   *Dova Stash: Cupboard* / *Not managed yet ...*.
2. **Assign categories...** lists category roots with `[ ]` / `[~]` / `[x]` marks.
   - *Weapons...* → **[ ] All Weapons**. Selecting it toggles the item to `[x]`. The menu reopens and every child shows `[+]`.
3. Back → Back → **Cancel**. The layout file now has the cupboard with `"categories": ["weapons.*"]`.
4. Bind the housecarl's dresser to *Armor → Heavy* and *Armor → Light* (individual children show `[x]`).
5. Press K at the dresser again: the body lists *Heavy armor, Light armor* (category names from `00_Default.json`).
6. Pagination: *Weapons...* has 10 children + *All*. The menu shows *More...* and the body shows
   *(page 1/3)*. *Back* on page 2 returns to page 1.

### 3.2 Distribute on master close

1. `player.additem 00012EB7 2`, `player.additem 00012E49 1` (Iron Armor), `player.additem 0003EADD 2`.
2. Open the master, store all of them, and close it.
3. HUD: *Dova Stash: sorted N items into 2 containers*. The swords go to the cupboard, the armor goes
   to the dresser, and the potions stay in the master because they are homeless.
4. Close the master with nothing sortable in it. No HUD line appears.

### 3.3 Learn (hand placement)

1. Open the **bedroom end table** (not bound). Store a Dwarven Gyro or another misc item, then close it.
2. The log shows `Learn: <item> -> <table>: recorded` and `Learn: ... is now a managed container`.
   After you close the menu: `Saved layout ...`. The layout file now has
   `"learned": { "<item ref>": "<table ref>" }`.
3. Put the same item type into the master and close it. The item moves to the end table (*sorted 1 item into 1 container*).
4. Store an Iron Sword in the cupboard by hand. The log shows `Learn: ... unchanged`, because the
   cupboard already receives it via `weapons.*` and no learned entry is written.
5. Store something in an **ignored** container. The log shows `ignored (container is ignored)`.
6. Store something in the **master** itself. Nothing is learned (`ignored (master)` or no log line).
7. Our own moves are never learned. During Deposit, Distribute and Retrieve, no `Learn:` lines appear.

### 3.3a Undo, item families, category suggestion (DESIGN §8.6)

Defaults: `iSuggestCategoryAfter = 3`, `iLearnUndoSeconds = 5` (both on the settings page under *Learn
where you put things by hand*, live).

1. **Undo.** Put a Dwarven Gyro into the end table by hand and close the menu. HUD: *Dova Stash: Dwarven
   Gyro now goes to End Table (K: undo)*. Press **K** within 5 s (aimed at anything, or at nothing).
   HUD: *Dova Stash: undone, Dwarven Gyro is not learned*; the log has `Learn: 1 learn(s) undone in
   Breezehome`; the layout file has no `learned` entry for it, and the end table is gone from
   `containers` again (it only entered the setup with that learn). Repeat and wait 6 s before pressing K:
   the hotkey opens the usual menu, nothing is undone.
2. Put three different items in at once and close. HUD: *3 items now go to End Table (Gyro, ...) (K:
   undo)*. K within 5 s undoes all three.
3. **Families.** `help "Iron Sword of" 4 WEAP` lists the enchanted iron swords (made from the Iron Sword,
   `00012EB7`); add one ("of Burning") and one more ("of Scorching"). Put the first into the end table by
   hand. Log: `Learn: Iron Sword of Burning ... -> End Table: recorded (as Iron Sword ...)`; the layout's
   `learned` has the **Iron Sword's** ref (`Skyrim.esm|0x00012EB7`), not the enchanted one's. Then put a
   plain Iron Sword and the second enchanted one into the master: both move to the end table (*sorted 2
   items into 1 container*). Put the plain Iron Sword into the cupboard (`weapons.*`) by hand: `Learn: ...
   removed (container is the natural destination)`, the entry is gone, and the enchanted ones go to the
   cupboard again.
4. **Suggestion.** Clean slate; three different ordinary books (`help book 4 BOOK`, or take them off
   Breezehome's shelves). Put the first two into the end table by hand and close
   it: no question (2 < 3). Put the third in and close. ~5.5 s after closing (the undo window must pass
   first; with `iLearnUndoSeconds = 0` right after the close animation) a message box asks *Dova Stash:
   send all Books to End Table?* with **Yes** / **No**. Not while any menu is open; never while a sort
   runs. Log: `Learn: asking whether all books.other go to End Table (3 learned)`.
   - **Yes.** HUD: *Dova Stash: End Table now takes Books*. The layout: the end table has
     `"categories": ["books.other"]` and the three books are gone from `learned` (log: `... (3 learned
     item(s) now covered by it dropped)`). A book put into the master goes to the end table by category.
   - **No** (or Esc). Nothing on the HUD; the layout has `"declined": {"<end table ref>": ["books.other"]}`.
     Put a fourth book in: no question. Panel → Containers → End Table: *Not asked again for: Books*
     **Ask again** removes it; the next book put in asks again.
   - A fourth kind: put three potions into a container another container already takes with the exact
     `alchemy.potions`: no question (an exact binding elsewhere). With `alchemy.*` elsewhere instead: the
     question comes (the exact binding wins by specificity, 3.4 shows the potions then go to the end table).
   - The panel's container page shows the same question (*Dova Stash: send all Books to End Table? (3
     learned items)*) with **Yes** / **No, don't ask again** while it is pending.
5. **Save as template** with learned items: the template has no `declined` (and `esm_dump.py check` warns
   if one is put in by hand). **Use template** over the layout keeps the `declined` entries of containers
   still in the setup.
6. **QuickLoot IE.** Deposit from the master with a weapon that goes to the cupboard, then within 5 s open
   the cupboard with QuickLoot (no E) and take the weapon. The log has **no** `... came back from ... (a
   script returns it)` line, and the next Deposit moves it again. (Before: it was marked as returning and
   never moved again that session.)

### 3.4 Sort all and Retrieve by category

1. Unbind the cupboard (Assign → Weapons → All Weapons toggles off), and bind it to *Weapons → Swords* only.
2. Master menu → **Sort all**. Daggers now sitting in the cupboard move back to the master
   (homeless), and swords stay. HUD: *re-sorted ...*.
3. Master menu → **Retrieve...**. Only roots with items appear, with counts, e.g. *Weapons... (5)*.
   Choose **Weapons... → Swords (3)**. Only swords come back (*retrieved 3 items from 1 container*).
4. Retrieve a root with one category, e.g. *Keys*. It retrieves directly, with no submenu.

### 3.5 Ignore / Remove / Make master / Reset

1. Container menu → **Ignore**. HUD: *... is now ignored*. Deposit and Sort all never touch it, and
   learned items no longer go there.
2. **Stop ignoring** restores it.
2a. Container menu on a container with categories and a learned item → **Remove...** → *Yes*. HUD:
   *<name> removed (what's inside stays)*. Its entry and its learned items are gone from the layout
   JSON; its items are still inside. The master menu has no **Remove...**.
2b. Put a book into a container with no categories by hand (learning on), close it and look at it.
   **Expected:** the activate prompt reads e.g. *End Table (Books, learned)*. Then **Categories...**: the
   book's group reads *[ ] Books... (1 learned)* and, inside it, *[ ] Books (1 learned)*.
3. Container menu on another chest → **Make master**. The old master becomes unmanaged and the new
   one is the master. The old master's items are not moved.
4. Master menu → **Reset home...** → *Yes*. HUD: *Breezehome reset (setup backed up)*. The layout
   file is renamed to `<file>.json.<YYYYMMDD-HHMMSS>.bak`; only the newest 5 backups are kept.
   **No items are moved.**

### 3.6 Templates (zero-touch preview of M3)

1. After a reset, press K at any container and choose **Use template "Breezehome"**.
   HUD: *Breezehome setup enabled*. The layout file has `"basedOn": "Templates/Vanilla/Breezehome.json@1.0.0"`.
2. Deposit. Loot is spread over the bound containers.
3. Master menu → **Re-apply template** → *Yes* (the label says Re-apply only for the template the
   layout is based on; another template reads **Use template**). A timestamped `.bak` copy of the
   layout is written first.
   Category bindings come from the template; learned destinations, keep rules, ignored containers,
   explicit forms and containers you added yourself (now without categories) are kept.
4. Set `bAutoEnableTemplates = true` (the default), start a new session and enter Honeyside (`coc RiftenHoneyside`).
   HUD: *Dova Stash: Honeyside is set up. Press K anywhere here to put your loot away.* Press K without
   aiming at anything: the master menu opens; Deposit puts the loot away.
5. **Save as template (hotkey).** In Breezehome with a setup of your own (master, a few containers with
   categories, one ignored container, a rack with categories, a mannequin with an armor set, a keep
   rule, some learned items): master menu → **Save as template...**. The text box shows *Breezehome*;
   type *My Breezehome* and confirm. HUD: *Dova Stash: saved as template "My Breezehome" (incl. N learned
   items)*. The file `Templates/User/My Breezehome.json` (MO2: `overwrite/SKSE/Plugins/DovaStash/`) has
   your character's name as `author`, `"version": "1.0.0"`, the Breezehome `cell`, no `learned` and no
   `basedOn`, the master, containers, displays (with `set`) and `keep`; each learned item is in the
   `forms` of the container it was learned for (none for the master or an ignored container), and
   `requires` lists every plugin the refs and those items point into. Log: *Exported Breezehome as
   template "My Breezehome" v1.0.0 ... N learned item(s) kept as item bindings ...*, then
   *Templates: N loaded* and the home's template line.
6. **Offered right away.** Press K again (no restart): the master menu now has
   **Use template "My Breezehome"** (the player's own templates rank before the shipped one). The
   panel's **Actions → Templates for this home** lists it as *(recommended)*, and **Dova Stash → Templates**
   lists it.
7. **Same name again.** **Save as template...** → *My Breezehome* → a box asks *Replace the template
   "My Breezehome"? Templates/User/My Breezehome.json (v1.0.0) is saved as v1.0.1; the old file is
   backed up.* **No** writes nothing (check the file time). **Yes** writes v1.0.1 and leaves
   `My Breezehome.json.<YYYYMMDD-HHMMSS>.bak` next to it. Save a different home under the same name: the
   box adds *It is the template of another home.*
8. **Round trip.** Master menu → **Reset home...** → *Yes*, then press K at a container →
   **Use template "My Breezehome"**. The layout file has the same master, categories, ignored
   containers, displays, mannequin set and keep rules as before, an empty `learned`, the learned items
   as `forms` of their containers, and `"basedOn": "Templates/User/My Breezehome.json@1.0.1"`. Deposit
   sorts as before, learned items included. (Exported from the panel with *Keep learned items* unticked,
   the template has no such `forms`, and learned-only items go to the master until you teach them again.)
9. Esc in the text box saves nothing (debug log: *Save as template: cancelled*). With `iMode = 1`
   (message boxes) the master menu offers the same entry; without UIExtensions it saves under the
   home's name directly.

### 3.6a Setups backed up outside Data (DESIGN §6.6)

The folder is `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups/` (VR: `Skyrim VR`), next
to `DovaStash.log`; MO2 never puts it in `overwrite`.

1. **Startup.** Start the game with existing layouts. Log: *Home backup: copies go to ...*, and (first
   time only) *Home backup: N setup file(s) that had no copy yet copied to ...*. Each
   `Layouts/<file>.json` now has a `Backups/<file>.json` with the same content and file time.
2. **Every save.** In Breezehome, bind a category (or teach an item and close the container). Debug log:
   *Home backup: wrote .../Backups/Skyrim.esm_000165A8.json*; the file matches the layout. The first save
   of the session first logs *Home backup: Skyrim.esm_000165A8.json backed up to ...json.<stamp>.bak*
   (history; never more than 5). No hitch when closing the container.
3. **Lost setup (the MO2 case).** Quit, delete `overwrite/SKSE/Plugins/DovaStash/` in MO2 (or just the
   home's file in `Layouts/`), start the game and enter Breezehome. Log: *a backup of its layout can be
   restored: ... (date)*. HUD once: *Dova Stash: a saved setup for Breezehome can be restored (press K)*
   (not the template notice; with `bAutoEnableTemplates = true` no template is turned on).
4. Press K at a container (or at nothing). The setup list starts with *Restore your last setup
   (2026-10-12 14:03)* (the copy's time), then *Make ... the master container*, *Use template ...*,
   *Advanced...*. Choose it. HUD: *Dova Stash: Breezehome restored*; the layout file is back with the
   same master, categories, learned items and displays; Deposit sorts as before. Same with `iMode = 1`
   (message boxes, *Restore your last setup (...)* first).
5. **Reset keeps it.** Master menu → **Reset home...** → *Yes*. The layout goes to a `.bak` as before;
   the copy in `Backups/` stays, and the hotkey offers *Restore your last setup* again. Set the home up
   anew (Make master): the copy from before the reset moves to history first (log line as in 2).
6. **Broken copy.** Break `Backups/Skyrim.esm_000165A8.json` (delete a brace) and delete the layout file.
   Choosing *Restore your last setup* gives *Dova Stash: that saved setup is broken (see DovaStash.log)*;
   the log says *could not be parsed; it is not used*. Nothing is written; the home stays not set up.
7. **No Documents access** (make `Backups` read-only): saving still works; the log warns once
   (*Home backup: could not write ...*), then only at debug.
8. Panel: see 5.2 step 11.

### 3.7 Displays (DESIGN §8.4)

Whitepeak Tower (77 racks, 6 mannequins, 50 item plaques) or any vanilla home (Breezehome has 9 racks).
`iLogLevel = 0`. Load-order prefixes below assume Dawnguard is `02`; check with `help "Elder Scroll" 4`.

1. Enter the home. Give yourself display gear:
   `player.additem 000139B9 1` (Daedric Sword), `player.additem 000139A6 1` (Glass Dagger),
   `player.additem 000139AC 1` (Ebony Battleaxe), `player.additem 0001399D 1` (Elven Bow),
   `player.additem 0001396E 1` (Daedric Shield), `player.additem 0001396B 1`, `0001396D`, `0001396C`,
   `0001396A` (Daedric armor set), `player.additem 00013952 1` and `00013954` (Steel armor and helmet),
   `player.additem 00061CD6 1` (Konahrik), `player.additem 020118F9 1` (Elder Scroll (Blood)).
   Equip one more sword and keep it equipped.
2. Deposit before turning anything on. **Expected:** no `Displays in` line; everything is stored, no rack
   gets anything.
3. Take the gear back out (Retrieve), look at an empty weapon rack, press the hotkey. **Expected:** a menu
   with *Use this rack*. Choose it. **Expected:** HUD *Weapon Rack is used now*; nothing goes on the rack yet.
4. Its menu opens again by itself: *Categories... (any weapon)*, *Fill now*, *Stop using this rack*. Open
   *Categories...*, tick *Bows*, go back: the entry reads *Categories... (Bows)*.
5. Use a second rack without categories, a dagger display, the middle of a coat-of-arms rack, a mannequin
   (aim at the mannequin: Whitepeak's have no name and no prompt, the hotkey still opens *Mannequin* with
   *Use this mannequin*, then *Categories...* and *Armor set... (Auto)*; set its categories to *Light Armor*) and, in Whitepeak, a glass display case (aim
   inside it, at *Weapon Rack*), Konahrik's and the Elder Scroll's plaques. Then Deposit. **Expected:**
   - the elven bow on the bow rack (not the more valuable sword), the daedric sword or battleaxe on the
     second rack, the glass dagger on the dagger display, a weapon lying in the glass case,
     the shield on the coat-of-arms rack, Konahrik and the Elder Scroll on their plaques; the mannequin
     stays empty (no light armor) and the daedric set is stored;
   - the layout file's entries for the two plaques list their own item under `items` (Konahrik:
     `Skyrim.esm|0x00061CD6`), and nothing else ever goes on them;
   - each weapon sits on its rack like a hand-placed weapon (not on the floor, not floating, not turned
     sideways: a mace hangs upright, as when you place it yourself); the log has
     `Display: <item> from <player> on <rack> (rack)`;
   - watch the racks while it fills (third person helps): no weapon is ever seen falling in front of you,
     lying on the floor or hanging in the air near a rack; each one just appears on its rack, one after the
     other (plaques: the model appears, nothing moves);
   - the rack's activate prompt is gone: activating the weapon picks it up (no "you need a weapon equipped");
     the weapon stays up (it does not drop into your inventory 2 s later), and the log has
     `<rack>: holds <weapon> (activator disabled, trigger state ...)`.
5a. Same node as by hand (the Unofficial Patch picks special nodes; find form ids with `help "<name>" 4`).
   Store everything, empty the racks you use, and give yourself a warhammer, a mace, a Dawnguard
   crossbow and Dwarven crossbow, the Skull of Corruption, the Wabbajack, a Forsworn staff, a Falmer staff,
   the Shield of Ysgramor and a dwarven shield. Turn on plain racks (no categories), a coat-of-arms rack's
   sides and middle, and a glass display case; Deposit, a few at a time. **Expected:**
   - each item hangs exactly as when you place it yourself on the same rack (warhammer on the warhammer
     spot, not the battleaxe's; staves and shields on their own spots); the log has `Display: <item> goes on
     <node> of <rack> (the Unofficial Patch's choice)` for crossbows, staves and those shields;
   - crossbows only go on racks whose trigger has a crossbow spot (never on a coat-of-arms rack); elsewhere
     they are stored;
   - a Falmer staff (too long) never stays in a glass display case: if one is tried, the log has
     `<case> does not take <staff>: the staff is too long for the display case`, the staff is in your
     inventory, and the next Deposit puts another weapon in that case and never that staff.
6. Take the weapon off the bow rack (activate it), then Deposit. **Expected:** it goes back up on
   that rack; nothing else goes on racks you did not turn on; items still on display are not put up twice.
   Then take it down again and use *Fill now* on that rack: it goes up again; the hotkey at it then offers
   no *Fill now*, and leaving the cell and coming back keeps the weapon on the rack (no activate prompt).
   *Fill now* on a display case that holds everything it is made for is not offered either; on one that
   still lacks items you don't have it says *nothing you have or store fits <case>*.
   With a dragon claw (or another plaque item) in your inventory and an empty plain rack in use, *Fill now*
   on that rack never puts the claw up: it waits for its plaque, as in a Deposit. Deposit, then take the
   weapon off the rack and Deposit again: it goes up again (the log never says it "came back ... it stays
   where it is from now on" for an item a display took).
7. Look at the weapon on the rack, press the hotkey. **Expected:** the rack's menu (*Stop using this rack*).
   Choose it: the weapon stays; after you take it down, Deposit stores it instead.
8. Temper a second Konahrik copy (`player.additem 00061CD6 1`, grindstone) and empty its plaque: Deposit puts
   only plain copies on item plaques (the tempered one goes to storage).
8a. Mannequins, Auto. Clear the mannequin's categories (*Categories...*, untick *Light Armor*), keep the
   Daedric set and the steel armor and helmet stored, *Fill now*. **Expected:** it wears the whole Daedric set
   (four pieces beat the steel pair); the steel stays stored; the log has `Display plan: ... (mannequin)`
   lines for the four pieces. With an armor mod whose pieces have no vanilla material keyword (most
   standalone armor mods), store its pieces, use a second mannequin, *Fill now*: it wears that mod's pieces,
   never mixed with vanilla ones.
8b. Mannequins, a chosen set. Use a third (empty) mannequin. Carry only the Ebony Armor
   (`player.additem 00013961 1`; other pieces: `help "Ebony" 4`). Hotkey at it: **Expected:** *Armor set...
   (Auto)* under *Categories...*; open it: `[x] Auto` and the sets you have, most complete first, e.g.
   `[ ] Ebony (1/4)`. Choose Ebony: HUD *Mannequin wears Ebony*, back in its menu: *Armor set... (Ebony)* and
   *Fill now*; the layout entry has `"set": "Skyrim.esm|0x00013961"`. *Fill now*: only the cuirass goes on
   (the Daedric set stays stored). Give yourself Ebony Boots, Deposit: the boots go on too; *Fill now* is
   offered until all four slots are taken. *Armor set...* → `Auto` removes `set` (HUD *... picks its own
   set*); nothing is taken off.
8c. Choose a set for a mannequin that wears another one (the Daedric mannequin → Ebony). **Expected:** HUD
   *... wears Ebony once you take its armor off*; Deposit puts nothing on it; take the Daedric pieces off by
   hand, Deposit: the Ebony pieces you have go on.
8d. Armor sets page (SKSE Menu Framework → Dova Stash → *Armor sets*). **Expected:** the first open takes a
   moment once; the log has one `Armor sets: N sets from M armor pieces in the load order` line. Searching
   `ebony` lists `Ebony` (Skyrim.esm) and modded Ebony-tagged sets separately; a mod's name or a piece's
   name finds its set; *Only sets I have* narrows to sets with pieces you carry or store (the *You have*
   column, e.g. `1/4`); hovering a set lists its pieces, `(have)` on yours. Pick a mannequin, select a set
   you have no piece of (e.g. Dragonplate), *Assign*: the mannequin's row reads `: Dragonplate`, Deposit
   puts nothing on it; add a Dragonplate piece, Deposit: it goes on. *Auto* returns it to Auto.
8e. Displays page (SKSE Menu Framework → Dova Stash → *Displays*), in the home of the steps above.
   **Expected:** one row per rack, mannequin, plaque and case of the cell (the log has one `Displays page: N
   display(s) in 1 loaded cell(s)` debug line when the page first opens, and again only after **Refresh** or a
   cell change, never every few seconds): name, kind (*Rack*, *Mannequin*, *Plaque*, *Display case*), status
   (green *used*, *not used*, *ignored* for one you stopped using), what it takes (*Bows*, *any weapon*, *set:
   Ebony*, a plaque's mask) and what it holds (a mannequin's pieces, *full* / *empty* for racks, the plaque's
   item once placed). The kind, status and contents filters, the search box (try `mask`, `ebony`, `bows`) and
   the sort combo narrow and order the list; *N of M* follows. Select an unused rack: **Use** → its status
   reads *used* within a second and the hotkey at it offers *Stop using this rack*. *Categories (any weapon)*:
   tick *Bows*, the hotkey picker shows `[x] Bows`; tick *All Weapons*, *Bows* reads *(via a wider choice)*.
   **Fill now** on an empty used rack puts a weapon up (greyed with a reason on a full one). **Stop using**:
   *ignored*, the weapon stays. On a mannequin, *Armor set (Auto)*: the list shows the sets you have, most
   complete first; search `dragonplate`, pick it: *set: Dragonplate*; **Auto** returns it. Under *Use all*,
   *Use all 12 racks...* asks first (*... 1 ignored stay off.*); Yes: HUD *12 displays used now*, the ignored
   rack stays *ignored*, nothing is moved until Deposit. In a home with areas (§3.9), the *Area*
   column names each display's area; displays in the other loaded area show, with **Fill now** greyed
   (*Only displays in the area you are in are filled.*); a used display in an unloaded area reads *used (not
   loaded)*. A display case used as a storage container is not listed. Outside a home: *Go to one of your
   homes to see its displays.*
9. Settings → turn off *Put gear on the home's displays*, Deposit: no `Displays in` line, everything is stored.
10. If a rack ever does not take a weapon, the log has `<rack> did not take <item>` and the HUD says the item
   is in your inventory; Deposit and Sort all skip that rack for the session, *Fill now* tries it again. Send
   the log. Leave the home while racks are still filling: the weapon not up yet is in your inventory, and
   when you come back every weapon on a rack is visible.
11. **Fail if** an item ends up on the floor, disappears (check `Inventory:` journal lines), is seen
   floating or falling on its way to a rack, stays invisible (a rack without its activate prompt and no
   weapon to see, or an invisible weapon you can still pick up), a rack shows two
   weapons, the game asks for an equipped weapon at a rack Dova Stash filled, a weapon comes back off a rack
   by itself, *Fill now* says *nothing ... fits* for a display that just took an item, a mannequin loses
   armor it was already wearing, or a mannequin with a chosen set gets a piece of another set. Send the log.
12. **Displays pre-defined by a template** (DESIGN §8.4, *Displays in templates*). Copy the shipped
   `Templates/Vanilla/Breezehome.json` to `Templates/User/Breezehome displays.json`, set `"priority": 1` and add
   (`NotInstalled.esp` must not exist):
   ```json
   "displays": [
     {"ref": "Skyrim.esm|0x00102A41", "label": "Sword rack",  "items": ["NotInstalled.esp|0x00000D62", "Skyrim.esm|0x0002ACD2"], "only": true},
     {"ref": "Skyrim.esm|0x00102A42", "label": "Modded rack", "items": ["NotInstalled.esp|0x00000D63"], "only": true},
     {"ref": "Skyrim.esm|0x00102A9E", "label": "Bow plaque",  "categories": ["weapons.bows"]}
   ]
   ```
   Start the game, Reset home in Breezehome, then **Use template**. **Expected:** the template is offered (no
   `not offered: missing NotInstalled.esp` line); the log has, once each at info,
   `Template Breezehome: Sword rack: 1 item doesn't exist in this load order (NotInstalled.esp|0x00000D62); skipped`
   and `Template Breezehome: Modded rack: 1 item doesn't exist in this load order (NotInstalled.esp|0x00000D63);
   skipped, nothing else goes on it until it does`; no warning or error. Carry Dawnbreaker
   (`player.additem 0002ACD2 1`), a Daedric Sword (`player.additem 000139B9 1`) and a Hunting Bow
   (`player.additem 00013985 1`), Deposit. **Expected:** Dawnbreaker on the sword rack, the bow on the plaque,
   the modded rack empty, the Daedric Sword stored. Leave and come back, Deposit again: the two lines are not
   logged again. The layout file still lists both `NotInstalled.esp` items.
13. **Plaques keep their own items.** In Whitepeak Tower, save a template (Save as template...) with a mask
   plaque in use, then edit that file: give the plaque `"items": ["Skyrim.esm|0x0001397E", "NotInstalled.esp|0x00000D64"]` (an Iron Dagger and a missing item).
   Reset home, Use template. **Expected:** the layout entry's `items` is the plaque's own mask again (debug:
   `Display ...: made for 1 item(s) by its script; the template's list (2) is not used`), no `doesn't exist`
   line for it, and Deposit puts the mask (never the dagger) on it. `esm_dump.py check` on that file warns that a
   template's `items` for the plaque are not used.
14. **Bookshelves.** Breezehome with the living room decorated (two shelves, 18 books each) and, with Hearthfire,
   the alchemy laboratory (two shelves, 11 books each); Whitepeak Tower for a home mod's shelves. Store
   everything, then give yourself books: `player.additem 0001B01C 1` (Advances in Lockpicking, a skill book),
   `0001B276` (Three Thieves, skill), `000A270E` (Spell Tome: Lightning Storm), `000F68AC` (The Nightingales
   Vol. 1, a plain book worth 500), `000F5CB2` (Paralysis Poison Recipe, a note), `0002D513` (the Elder Scroll
   (Dragon)), a second Three Thieves, and a dozen cheap books (`help "Book" 4`).
   - Aim at a living-room bookshelf (the shelf itself, where the game says *Book Shelf*), press the hotkey.
     **Expected:** *Use this bookshelf*. Choose it: HUD *Book Shelf is used now*, nothing moves yet. The
     hotkey again: *Categories... (any book)*, *Fill now*, *Stop using this bookshelf* (same in the message
     boxes with UIExtensions off). The log has `Hotkey -> display menu: ... (bookshelf, used, empty)`.
   - Deposit. **Expected:** the books appear on the shelf right away (no need to open it): the two skill
     books' titles first, the spell tome, The Nightingales, then the cheap books by value; never the note,
     the Elder Scroll, or the second Three Thieves; the rest is stored as usual. The log has
     `Display: <shelf> takes 18 more books (its script counts 0 of 18; 18 markers, 0 inside)`, one
     `Display: <book> from <player> on <shelf> (bookshelf)` per book and `<shelf>: its UpdateBooks placed
     the books on their markers`. Activate the shelf: the game says how much room is left (18 minus what went
     up), the menu lists exactly those books; close it: the books stay where they are.
   - Take one book off the shelf by hand (activate it on the shelf). **Expected:** it is in your inventory and
     no longer in the shelf's menu (the shelf's own trigger noticed, as without Dova Stash). Deposit: it goes
     back up. *Fill now* is not offered on a full shelf.
   - Use an alchemy-lab shelf (11 markers) with more books than fit stored or carried, *Fill now*. **Expected:**
     exactly 11 books go up, no *There is no more room* message, nothing handed back; the shelf's menu lists 11.
   - *Categories...* on the second living-room shelf: tick *Notes & recipes* only, *Fill now*: the recipe goes
     up (and nothing else). Untick it, tick *Spell tomes*: nothing more goes up there (the spell tome is on the
     first shelf: one copy of each title in the home).
   - A shelf the layout stores items in (before loading the save, remove `"ignored": true` from the other
     alchemy-lab shelf's entry in the home's layout file and give it `"categories": ["books.*"]`): the hotkey
     at it opens the container menu, not the display menu, and nothing fills it as a display.
   - Leave and come back: the books are still on the shelves, the shelves' menus list them; the books on the
     shelf can be taken as usual.
   - **Fail if** a shelf shows a book its menu doesn't list (or the other way around after reopening it), the
     game says there is no more room, a book is handed back, the Elder Scroll or a note goes up uninvited, a
     title appears twice in the home, or a book disappears (check the `Inventory:` journal lines). Send the log.

### 3.8 Materials at the workbench (DESIGN §8.5)

Breezehome (forge, tanning rack, alchemy lab, enchanter, cooking pot) or any home with stations, set up with a
master and a chest. `iLogLevel = 0`.

1. Store materials: `player.additem 0005ACE4 10` (Iron Ingot), `player.additem 000800E4 10` (Leather Strips),
   `player.additem 00071CF3 6` (Iron Ore), `player.additem 000DB5D2 4` (Leather), `player.additem 00034CDF 5`
   (Salt Pile), `player.additem 0002E4FF 2` (Grand Soul Gem, filled). Deposit, then keep **2 Iron Ingots** on
   you (`player.additem 0005ACE4 2`).
2. Sit at the forge. **Expected:** before the menu shows, HUD *Dova Stash: N materials at hand*; the log has
   `Recipes: ... recipes at ... bench keywords` once (first station of the session), `Crafting supply: brought
   ...` per stack and `brought N items (... kinds) to <forge>` at info. The forge's lists show the recipes the
   stored ingots and strips allow. Nothing that isn't a forge material came (no salt, no soul gems).
3. Craft one Iron Dagger (1 ingot, 1 strip), leave the menu. **Expected:** HUD *Dova Stash: put 22 back* (9 ingots,
   9 strips, 4 leather, back where Deposit had put them); you still have your **2** ingots and the dagger. Log:
   `Iron Ingot ...: had 2, brought 10, crafted 0, now 11 -> 9 back` and `+1 Iron Dagger ... (crafted; stays with you)`.
4. Smelter: sit, smelt 2 Iron Ore into 2 Iron Ingots, leave. **Expected:** the 2 new ingots stay with you (the
   log line for Iron Ingot shows `crafted 2`); the 4 unused ore go back. Tanning rack: make strips from leather;
   the new strips stay with you, unused leather goes back.
5. Alchemy lab: every stored ingredient comes (not potions), the unused rest goes back; the potion you make stays.
   Enchanter: filled soul gems come; the one you use is gone, the rest go back; the enchanted item stays.
6. Grindstone / workbench: improve a weapon you carry. **Expected:** the tempered weapon never moves; the
   unused ingots go back.
7. Sit at a station and get up before the menu shows (move away quickly): everything brought goes back
   (`(got up)` in the log).
8. Sit at a station, wait for the HUD line, then quickload an older save before leaving the menu (or quit to
   the main menu and load). **Expected:** `Crafting supply: loading a game; the N items brought ... stay with
   you`; the loaded game is exactly as it was saved, and nothing is moved after it loads.
9. Settings → turn off *Materials at the workbench*: sitting down brings nothing. Turn on *Weapons and armor used
   as materials too*: with Dawnguard, a stored Crossbow comes to the forge (Enhanced Crossbow recipe); a tempered
   or enchanted one never does.
10. A station outside a home (Warmaiden's forge in Whiterun): nothing comes. A modded station (any mod adding
   its own bench keyword and recipes): its materials come without a patch.
11. **Fail if** anything you carried before leaves your inventory, anything you crafted goes into a container,
   an item disappears (check `Inventory:` journal lines), a favorited, worn, tempered or enchanted copy moves,
   or the forge's list misses recipes for materials that came (then the menu built its list before the pull:
   send the log).

### 3.8a Restock (DESIGN §8.7)

Breezehome (or any home) with a master and a satchel bound to *Alchemy* and a quiver chest bound to *Ammo*.
`iLogLevel = 0`. Start with no keep quota of your own.

1. **Nothing for people who did not ask.** Master menu (hotkey at nothing): there is **no** *Restock* entry
   (lists and message boxes). Panel → This home → Actions: *Restock* is greyed out; its tooltip says to tick
   Restock on a quota. Deposit behaves exactly as before (no *restocked* in the HUD line).
2. Stock the home: `player.additem 0003EADE 20` (Potion of Minor Healing), a few better healing potions
   (`help "Potion of Healing" 4` gives an ID), `player.additem 0001397F 120` (Steel Arrow),
   `player.additem 0001397D 30` (Iron Arrow). Deposit: everything goes into the satchel and the quiver chest.
3. Panel → Keep rules → *Add quota*: Potions, 10, and another: Arrows, 50. **Expected:** the new rows have
   *Restock* ticked; Save. `Layouts/<cell>.json` has `"restock": true` on both rules.
4. Master menu → **Restock** (now listed right after Deposit). **Expected:** HUD *Dova Stash: restocked 60
   items*; you carry 10 potions, the most valuable first (the better healing potions before minor ones), and
   50 arrows: the 30 iron arrows are worth less than steel, so 50 steel arrows come (value order), taken from the
   quiver chest. Log: `Restock: quota alchemy.potions 10 (layout): carried 0, taking 10` and one
   `Restock: <item> x<n> from <container>` line per stack at debug.
5. Restock again. **Expected:** *Dova Stash: nothing to restock*; nothing moves.
6. Drink 3 potions and fire a few arrows (or `player.removeitem 0001397F 12`). **Deposit.** **Expected:** one HUD
   line *Dova Stash: deposited ... , restocked 15* (3 potions + 12 arrows), or *Dova Stash: restocked 15 items*
   when there was nothing else to deposit. You carry exactly 10 potions and 50 arrows again; nothing restocked
   went back into a container (log: the potions and arrows are `kept by quota ...`).
7. Carry more than the quota (`player.additem 0001397F 30`) and Deposit: the 30 extra steel arrows go to the
   quiver chest (the quota keeps 50 on you); nothing is restocked.
8. Settings → turn off *Restock after Deposit* (`[Sorting] bRestockAfterDeposit`). Drop to 5 potions and
   Deposit: nothing comes back. Master menu → Restock still works. Turn it on again.
9. Untick *Restock* on the Arrows quota, Save; the JSON loses `"restock"` on that rule. Restock tops potions only.
10. Global quota on one item (lockpicks have no category of their own, so edit the file): in
   `Data/SKSE/Plugins/DovaStash/Settings.json` add to `keep` `"quotas": [{"forms": ["Skyrim.esm|0x0000000A"],
   "quota": 5, "restock": true}]`, restart, store 20 lockpicks in a container, carry 2. **Expected:** Restock
   brings 3 (the default *always keep lockpicks* does not block it); Deposit leaves all 5 on you. The panel's
   global keep rules show the quota with Restock ticked.
11. Ignored containers and displays: put potions in an ignored barrel and on nothing else; Restock takes none
   (*nothing to restock* if storage has no other potions). Quest items in storage never come.
12. Multi-cell home (§3.9): from the grounds, Restock takes only from containers reachable there, as Retrieve does.
13. **Fail if** you ever end up with more than a quota's number because of Restock, an item disappears (check
   the `Inventory:` journal), something comes from an ignored container, a display or outside the home, or a
   Deposit right after a Restock moves restocked items back.

### 3.9 Homes that span several cells (DESIGN §6.7)

Lakeview Manor with the main hall built and its smithing area outside (anvil, smelter, tanning rack, grindstone,
workbench in the exterior cell `Skyrim.esm|0x00009BF7`, `BYOHHouse1Exterior`; the garden in `0x00009BD6`).
`coc BYOHHouse1Falkreath`, apply the *Lakeview Manor* template (or make the main hall chest the master),
`iLogLevel = 0`. Lakeview's chests, cupboards, wardrobes, safes and end tables are persistent; its sacks,
satchels, barrels and strong boxes are not (`esm_dump.py cell "HearthFires.esm|0x00002FEC"`,
`reachableFromOtherCells`).

1. **Single cell unchanged.** Before adding anything, play 3.1–3.4 in Breezehome and in Lakeview: same
   results and log lines as before this feature (no `area` lines, no `Multi-cell homes:` line at startup).
2. **Add the grounds.** Deposit some iron ingots, leather strips, ore and leather (they go to the main hall
   chest (east) and the display wardrobe). Walk out of the front door and press the hotkey at nothing.
   **Expected:** a message box *Dova Stash: add this area to Lakeview Manor?* with *Add this area to Lakeview
   Manor* / *Cancel*; choosing it opens the UIExtensions text box pre-filled with *BYOHHouse1Exterior* (the
   cell's own name). Type *Grounds* and confirm: HUD *Dova Stash: Grounds is now part of Lakeview Manor*; the
   layout file `Layouts/HearthFires.esm_00002FEC.json` now has
   `"cells": [{"cell": "Skyrim.esm|0x00009BF7", "label": "Grounds"}]` right after `cell`; the log has
   `Add area: ... named "Grounds"` and, on the next hotkey, `Cell ...: part of Lakeview Manor` with the areas
   and, per container, `reachable from every area (persistent)` or `reachable only in Lakeview Manor (not
   persistent)`. Pressing the hotkey again opens the master menu (UIExtensions list or message boxes) with
   *Rename Grounds...* (lists only) and *Remove this area from Lakeview Manor...*.
   **Variants:** cancel the text box, or confirm it unchanged: the area is still added (HUD *this area is now
   part of Lakeview Manor*, `"cells": ["Skyrim.esm|0x00009BF7"]`, no `label`). Without UIExtensions (or with
   `iMode = 1`, which still uses the text box when UIExtensions is installed) the add happens at once.
   *Rename Grounds...* → type *Yard*: HUD *Dova Stash: area renamed to "Yard"*; the panel's header and Areas
   list and the remove prompt say *Yard*; cancelling the box changes nothing.
3. **Forge outside, supplies inside.** Sit at the outside anvil. **Expected:** *Dova Stash: N materials at hand*;
   the ingots and strips came from the main hall containers (log: `brought ... from Chest [HearthFires.esm|...]`).
   Craft an iron dagger, leave: *put N back*, and inside, the unused ingots and strips are back in the same
   containers. Smelter and tanning rack the same.
4. **Deposit / Retrieve outside.** Outside, hotkey → Deposit: items go into the persistent containers inside
   (log lines name them), anything bound to a sack or satchel stays with you or goes to the master
   (`Container ... unavailable: only reachable in Lakeview Manor (not a persistent reference)` at debug, no
   warning). Retrieve → Ingots: they come from inside. Browse containers → a main hall chest: its container menu
   opens (unverified: report if it closes at once or shows nothing).
5. **Panel.** Outside, open the panel: header *Lakeview Manor in Grounds* (the name you gave it; the cell's own
   name, *BYOHHouse1Exterior*, without one); Actions → **Areas** lists *Lakeview Manor (main)* and *Grounds (you
   are here)* with *Rename* and *Remove*. *Rename* opens a box under the row with the current name; type
   *Yard*, Enter (or *Save*): status *Dova Stash: area renamed to "Yard"*, the row and the header follow, the
   file's `label` is *Yard*; an empty box brings the cell's own name back (the entry is written as a plain
   string again). Containers: rows inside show *(Lakeview Manor)* next to *available*, sacks show
   *unavailable: only reachable in Lakeview Manor (not a persistent reference)* (debug log only, no warning).
   When the panel is opened in a cell that can join a home, the *Add this area to ...* button has a name box
   next to it (hint: the cell's own name); the name typed there becomes the label.
6. **Cellar with its own setup.** `coc BYOHHouse1FalkreathBasement` with the *Lakeview Manor Cellar* template
   enabled: the cellar stays its own home (header *Lakeview Manor, Cellar*, its own master). Add
   `"HearthFires.esm|0x00004849"` to the main hall's `cells` by hand: the cellar still stays its own home, and the
   main hall's panel says *HearthFires.esm|0x00004849 is listed but has a setup of its own, so it is not part
   of this home*. **Reset home** in the cellar: on the next hotkey there it is part of Lakeview Manor.
7. **Remove.** Outside, master menu → *Remove this area from Lakeview Manor...* → Yes: HUD *Dova Stash: Grounds
   is no longer part of Lakeview Manor*; the hotkey there says *not a player home (exterior cell not owned by
   you)* or offers the area again right after coming out of the door. `cells` is gone from the file (no empty
   list).
7a. **One home per cell.** With the grounds part of Lakeview Manor, set the cellar up as its own home (step 6)
   and walk from the cellar out into the grounds: no *Add this area to Lakeview Manor, Cellar* is offered there
   (the cell already belongs to Lakeview Manor; the hotkey opens Lakeview's master menu), and no template
   notice or auto-enabled template appears in the grounds.
8. **Persistent exterior container.** Add the grounds again, bind the outside chest `HearthFires.esm|0x00003068`
   to *Building materials* (from the panel, outside), go inside, Deposit lumber. **Expected:** it goes to the
   outside chest while you are inside (it is persistent). If it does not (log: `container is not in this home`),
   the exterior cell's grid was not known: report it with the log.
9. **Loading a game** outside right after leaving the house: no area is offered until you come out of a home
   again.
10. **Two exterior areas.** Add the garden's cell too (`Skyrim.esm|0x00009BD6`, walk into it from the grounds
   and press the hotkey). Standing in the grounds, aim the hotkey at a container in the garden's cell (both
   loaded): its container menu opens, as for one in your own cell. A rack or mannequin across the border is not
   offered (displays only in your own cell).
11. **Fail if** an item disappears, a move touches a non-persistent container in another cell, a display in
   another cell is filled, a single-cell home behaves differently from before, or a file without `cells` gains
   one.

## 4. Robustness

| # | Steps | Expected |
|---|---|---|
| 4.1 | Corrupt the layout JSON by hand (delete a brace) and re-enter the home | Log: `Layout ... could not be parsed; leaving it untouched`. The hotkey gives *this home's setup file is broken (see DovaStash.log)*. The file is never overwritten. **Reset home** moves it to a timestamped `.bak`. Same if `"containers"` is not an array or `"learned"` is not an object. |
| 4.2 | Change the load order (move a plugin up or down) and reload | Bindings still resolve, because FormRefs are local ids. |
| 4.3 | Disable a plugin a template binding refers to | Log: `Container <ref> unavailable: reference not found (plugin missing?)`. Items for it stay in the master. The JSON entry is kept. |
| 4.4 | Large inventory: `player.additem` about 200 different items, then Deposit | One HUD line. No hitch over about 0.5 s; if there is one, report it, because chunking across frames is a TODO. |
| 4.5 | Save and reload right after a learn (without closing the container) | `kSaveGame` flushes the layout. The learned entry is in the file. |
| 4.6 | Player-crafted potion (dynamic `FF` form), then Deposit/Retrieve | It moves like other potions but is never written to `learned`. |

## 5. M4: Menu Framework panel

Requires **SKSE Menu Framework** (QTR-Modding, v3) in the load order, on SE or AE (not VR). Keep
`iMode = 0` under `[UI]` in `DovaStash.ini`. If you omit the key, the default is also 0.

### 5.0 Startup and fallback

| # | Steps | Expected |
|---|---|---|
| 5.0.1 | Start the game with the framework installed | Log: `SKSE Menu Framework: installed (v...)`, `SKSE Menu Framework: stash window and "Dova Stash" section registered`, `UI: SKSE Menu Framework panel`. The `Settings:` line ends with `iMode=0`. |
| 5.0.2 | Set `iMode = 1` and restart | Log: `UI: native message boxes (iMode = 1)`. Every M1/M2 step works with message boxes, as before. |
| 5.0.3 | Disable SKSE Menu Framework in MO2 and set `iMode = 2` | Log: `UI: iMode = 2 but SKSE Menu Framework is not loaded; using native message boxes`. Message boxes work. |
| 5.0.4 | Set `iMode = 0` again with the framework enabled | The panel is used again (5.0.1). |

### 5.1 Opening the stash window (contextual)

Start from a clean Breezehome: delete `Skyrim.esm_000165A8.json` from MO2 `overwrite` and its copies in
`Documents/.../SKSE/DovaStash/Backups/` (§0).

1. In Breezehome, look at the chest near the alchemy lab and press **K**. The window **Dova Stash**
   opens and the game pauses. The header shows *Breezehome* and its cell id. The body shows
   *This home is not set up yet.*, a **Make Chest the master** button, and *Templates for this home*,
   which lists *Breezehome v1.0.0 by Dova Stash (recommended)* with a **Use** button.
2. Click **Make Chest the master**. The view switches to the tabs (Actions, Containers, Learned,
   Keep rules, Template). The header now shows *Setup: Breezehome v1.0.0 by Player*,
   *Custom setup (not from a template)* and *Master: Chest*. The status line at the bottom reads
   *Dova Stash: Chest is now the master container*.
3. Close the window with the title-bar **X**, the **Close** button, or Esc. The game unpauses, and the
   HUD shows any queued notifications.
4. Press K at nothing. The window opens on the **Actions** tab.
5. Press K at another container (for example the cupboard in the loft). The window opens on the
   **Containers** tab with that container selected. If it is not managed yet, it appears in the table
   as *not managed yet*.
6. Press K at a respawning container in a dungeon (`coc BleakFallsBarrow01`). No window opens. The HUD
   shows *can't use ...* (same as M2).
7. Outside a home: open the SKSE Menu Framework menu (its own hotkey) → **Dova Stash → Current home**.
   It shows *You are not in a player home (...)*.

### 5.2 Actions tab

1. Build the test inventory from 2.2. Click **Deposit**. The status line and, after you close the
   window, the HUD show *deposited N items into ...*. The **Retrieve** tree refreshes with counts.
2. **Retrieve**: *Everything (N)* plus one row per root that has items, such as *Weapons (5)*. A root
   with populated children expands (for example *Swords (3)*, *Daggers (2)*), and each row has a
   **Retrieve** button. Retrieve *Swords*: only swords come back, and the counts drop.
3. Click **Refresh counts** after moving items by hand. The counts match the containers.
4. **Sort all** behaves like the M2 menu action (3.4).
5. **Templates for this home** (the second section, above Retrieve): *Breezehome v1.0.0 by Dova Stash
   (recommended)* with **Use**. The setup from 5.1 is your own (*Custom setup*), so **Use** asks first: a
   yellow *Use the template "Breezehome"? Your changes to this setup are replaced: ... Your setup is backed
   up first (Saved setups); no items are moved.* with **Yes**/**No**. **No** leaves things unchanged.
   **Yes** applies the template: the header shows *Based on template:
   Templates/Vanilla/Breezehome.json@1.0.0*, the row shows *(in use)*, the button becomes **Re-apply**, and
   the status line reads *Dova Stash: Breezehome setup enabled*.
6. **Switching templates** (DESIGN §6.2 step 4). Copy `Templates/Vanilla/Breezehome.json` to
   `Templates/Test/Breezehome.json`, set its `name` to *Test* and `"priority": -1`, give one container other
   categories and pick another master; restart (debug log level). Both rows are listed, *Breezehome* first
   with *(recommended)*, then *Test*. Note what each container and display holds. Right after step 5 the
   setup has no changes of your own, so **Use** on *Test* switches at once, with no question: the header
   says *Based on template: Templates/Test/Breezehome.json@1.0.0* and *(in use)* moves to *Test*. Log:
   *Breezehome: template switched from Templates/Vanilla/Breezehome.json@1.0.0 to
   Templates/Test/Breezehome.json@1.0.0*. Nothing moved: every container and display holds what it held;
   learned items, keep rules and ignored containers are still there (Containers, Learned, Keep rules tabs).
   **Sort all** then follows *Test*. **Use** on *Breezehome* switches back the same way.
7. **Asks when you changed something.** Tick one more category on a container (Containers tab), back to
   Actions: **Use** on the other template now asks first. **Yes** switches; `Layouts/` has a new timestamped
   `.bak`, and **Saved setups** → *Older copies* has a new entry whose **Restore** brings back the setup with
   your extra category. **Re-apply** on the template in use, after no change of yours, does not ask. Leave the
   panel open a minute: no repeated log lines (the check runs only when the setup changes).
8. **Drafts come last.** Add an auto-draft for the cell: the editor's *Save N drafts...*, or a copy of the
   Breezehome template under `Templates/Breezehome/Unknown.json` with `"autoDraft": true` and
   `"priority": 10`. Restart. It is listed last with *(draft)* (hover: drafted by the editor for many homes
   at once), never *(recommended)*, priority 10 or not; its **Use** works like the others. Reset the home
   with `bAutoEnableTemplates = true` and walk in again: the template someone made is enabled, not the draft,
   and with `false` the notice and the hotkey's *Use template* name it. Remove `Templates/Test/` and the
   Vanilla Breezehome (or add a missing plugin to their `requires`) and restart: the draft is now
   *(recommended)*, the notice and the hotkey offer it, and a first visit enables it.
9. **Templates page.** In Breezehome, **Dova Stash → Templates** shows *For Breezehome* above the table, with
   the same rows and buttons (and the same question when the setup has changes of your own); the table marks
   the auto-draft *(draft)*. Outside a home the section is not shown.
10. **Reset home...** → **Yes**. The layout is backed up to `.json.bak` and the window returns to the
   setup view. No items are moved. The setup view shows *Saved setups* with a **Restore your last setup
   (date)** button (no question: there is nothing to replace); it brings the setup back.
11. **Saved setups** (below Reset; hover for where the copies are): **Restore last setup (date)...** →
   *Restore the setup saved (date)? This home's current setup is backed up first (.bak); no items are
   moved.* **No** changes nothing; **Yes**: status *Dova Stash: Breezehome restored*, and `Layouts/` has a
   new timestamped `.bak` of the setup that was there. *Older copies (N)* lists the history copies (one per
   earlier session, newest first), each with **Restore the setup from (date)...**. With a broken layout
   file (4.1) the setup view offers the same buttons next to **Reset home...**.
12. **Only meaningful restores.** Right after a save (bind a category, close the panel, open it again; or
   after step 11's restore) the latest copy equals the setup, so **Saved setups** shows *The latest copy
   (date) is this setup as it is now.* instead of the **Restore last setup** button; *Older copies* stay.
   Change something (tick another category) and refresh: the button is back. The comparison ignores
   formatting: reformat `Backups/<file>.json` by hand (one line, spaces instead of tabs) and it still counts
   as the same. Nothing is compared per frame (no log lines, no hitch while the panel is open).

### 5.3 Containers tab

1. The table lists the master first (*[Master] Chest*, *(items without a destination)*), then every
   managed container with its categories, learned count and status. A container that is
   unavailable, such as unbuilt Hearthfire furniture, shows *unavailable: container is disabled
   (not built yet?)* in red. Hover the name to see the FormRef. A container with learned items only
   (2b in §3.5) shows *Books, learned* in grey under Categories; selected, its tree shows
   *[ ] Books (1 learned)* and a grey *Learned here: Books. ...* line.
2. Select the loft cupboard. In the details:
   - Type *Weapons* into **Label** and click **Rename**. The table shows *Weapons*, and the layout JSON
     has `"label": "Weapons"`.
   - **Categories sent here** shows a tree. Expand *[ ] Weapons* and tick **All Weapons (weapons.*)**.
     The node shows *[x]*, and every child shows *(via a wider pattern)*. The JSON has `"weapons.*"`.
   - Untick it, then tick only *Swords*. The root shows *[~]*.
   - Tick **Everything (*)**. It is added as `"*"`. Untick it.
   - Add `"weapons.swords.*"` by hand in the JSON and re-enter the cell. The pattern is listed under
     *Other patterns (unknown categories)* with a **Remove** button, which works.
3. **Ignore** → the status shows *(ignored)* and the category tree shows the ignored note.
   **Stop ignoring** restores it. **Remove...** → **Yes** takes the container out of the list.
   **Remove...** is not offered on the master or on a container that is not managed yet.
4. **Clear categories** empties the list (status line: *categories cleared for ...*).
5. **Make master...** → **Yes**. The selected container becomes the master (header updates). The old
   master is no longer listed. No items are moved.
6. With learned entries pointing at a container (3.3), **Forget learned (n)...** → **Yes** removes
   them. The Learned column drops to 0.
7. An unavailable container (for example an unbuilt Hearthfire chest bound by a template) shows its
   reason in red, and its category tree is greyed out.

### 5.4 Learned tab

1. Teach a destination by hand (3.3), then open the window. The tab lists *Item → Container*, sorted
   by item name. Hover a cell to see its FormRef.
2. **Remove** drops one entry. The layout JSON no longer has it, and the next Distribute uses
   categories again for that item.
3. With nothing learned, the tab shows the explanation text.
4. **Stale entries.** Ignore a container with learned items (3.5): its rows read *<container>
   (ignored)* in yellow, and the tab says *N of these point at a container that is no longer used, so
   they do nothing.* with **Forget those (N)...** → **Yes** removes exactly those (HUD: *forgot N learned
   items*); the others stay. A learned entry for an unbuilt Hearthfire chest reads its reason in grey
   (*not built yet*) and is not counted or removed. In a multi-cell home, an entry for a container out
   of reach from this area is not marked.

### 5.5 Keep rules tab

1. A home without a `keep` block shows *This home uses the global defaults only...*. The read-only
   *Global defaults* list shows equipped, favorited, quest, gold, lockpick and torch, or your
   `Settings.json`.
2. Add `misc.clutter` under **Never keep** with the *Category...* combo, add `quest` with the text
   field and **Add**, and add a quota: **Add quota**, choose *Potions (alchemy.potions)*, set the
   quota to 3 with the +/- buttons. *Unsaved changes* appears. **Save** is disabled while a quota has
   no category.
3. **Save** → status *keep rules saved*. The layout JSON has
   `"keep": { "inherit": true, "always": ["quest"], "never": ["misc.clutter"], "quotas": [ { "category": "alchemy.potions", "quota": 3 } ] }`
   (key order may differ).
4. Deposit with 5 Potions of Minor Healing and some clutter. 3 potions stay in the inventory and the
   clutter goes, even if a global rule would keep it.
5. Untick **Also apply the global defaults (inherit)** and save. Gold and lockpicks are no longer
   kept, apart from equipped, favorited and quest items, which are always kept.
6. **Revert** discards unsaved edits. **Use global defaults only...** → **Yes** removes the `keep`
   block.
7. A layout whose keep block came from JSON with extra keys or `"rules"` instead of `"quotas"` keeps
   them after saving (round-trip).

### 5.6 Template tab (export)

1. The name defaults to the home's name; author and version are empty (hints *your character's name*
   and *automatic*); **Keep learned items (as fixed item bindings)** is ticked (hover: the items go to
   the same containers for anyone using the template). Set the name to *My Breezehome*, then click
   **Export**.
2. The status reads *Saved to Data/SKSE/Plugins/DovaStash/Templates/User/My Breezehome.json. This home
   offers it now. N learned items kept as fixed item bindings.* and the HUD/status line *Dova Stash:
   saved as template "My Breezehome" (incl. N learned items)*. Under MO2
   the file is in `overwrite/SKSE/Plugins/DovaStash/Templates/User/`. It has your character's name as
   author, version 1.0.0, no `learned` (its items are `forms` of their containers) and no `basedOn`,
   `"cell"` is the Breezehome cell, and
   `"requires"` lists every plugin the refs point into (at least `Skyrim.esm`, and the plugins of
   display refs, plaque items and mannequin sets). Any template lint (for example *no "master"
   container*) is listed in yellow.
3. Click **Export** again with the same name. *... already exists (v1.0.0). Overwrite saves v1.0.1 and
   backs up the old file.* appears with **Overwrite** and **Cancel**. **Cancel** writes nothing (check
   the file timestamp). **Overwrite** writes v1.0.1 and a timestamped `.bak` of the old file. A version
   typed into the field is used as it is.
4. A name with `/:*?` characters is written with `_` in their place (the grey line shows the file
   name). An empty name gives *Enter a template name.*
4b. Untick **Keep learned items** and export under another name: no `forms` from learned items, and the
   HUD has no *incl. ... learned items*. The box is ticked again in another home.
5. No restart: **Dova Stash → Templates** lists *My Breezehome*, and in Breezehome the
   **Actions → Templates for this home** section lists it as *(recommended)* (the player's own templates
   rank before the shipped ones of the same priority). The hotkey's **Save as template...** (§3.6) writes the same
   file.

### 5.7 Global pages (SKSE Menu Framework menu → Dova Stash)

| # | Page | Expected |
|---|---|---|
| 5.7.1 | **Current home** | The same panel as the window, for the current cell. Actions work from here too. |
| 5.7.2 | **Settings** | Hotkey *K (scan code 37)*, gamepad *disabled*, and the INI booleans, log level and UI mode, all read-only, with the note to edit `DovaStash.ini` and restart. |
| 5.7.3 | **Templates** | In a home: *For &lt;home&gt;* with that home's templates to switch to (5.2 steps 6 to 9). Then every loaded template with name and version (*(draft)* on an auto-draft), author, home name (hover: cell FormRef), requires (green *ok*, or red *missing: ...* when a required plugin is not loaded) and an issue count (hover: the warnings). Templates for DLC or CC homes you don't have show *missing*. |
| 5.7.4 | **Diagnostics** | Category, template and Settings.json load problems, matching the `[warning]`/`[error]` lines in the log (each listed once), plus *This home's setup* warnings, such as a container pattern that matches no category. Break a category file on purpose (`"formTypes": ["XXXX"]`) and restart: the error is listed in red. |
| 5.7.5 | **About** | Version, runtime, Menu Framework version, category and template counts. *Dova Stash Editor: installed at ...\Tools\DovaStashEditor.exe* when the build deployed the editor next to the plugin (under MO2 the path may read as the game's `Data\Tools\...`); rename the exe and restart: *Dova Stash Editor: not installed*. |
| 5.7.6 | **Displays** | See §3.7 Displays, step 8e. |

### 5.8 Threading and robustness

| # | Steps | Expected |
|---|---|---|
| 5.8.1 | Keep the window open for a minute on each tab | No hitch. The log shows no repeated lines. A light refresh runs every ~2 s, and item counts are only recomputed on open, after actions, and on **Refresh counts**. |
| 5.8.2 | Click **Deposit**, **Sort all** and **Retrieve** several times quickly | Each runs once, in order. No crash, no duplicated or lost items (check the totals). |
| 5.8.3 | Open the window in Breezehome, close it, travel to Honeyside, and open the framework menu → **Current home** | It shows Honeyside, not Breezehome. Nothing from Breezehome (selection, keep draft) carries over. |
| 5.8.4 | Open the master by hand (ContainerMenu), close it, then press K | The window shows the distributed result. |
| 5.8.5 | Save and reload with the window closed | Nothing in the panel survives the reload except the data in the layout JSON. |

## 6. Borrowed-UI integration (M4b)

These features draw nothing of their own: what you see should look like the rest of your UI. Test with
your normal skin, then repeat 6.2 with UIExtensions' stock `listmenu.swf` (disable any UIExtensions
skin in MO2). Dova Stash must work the same with both.

### 6.1 Startup log

| # | Steps | Expected |
|---|---|---|
| 6.1.1 | Start the game, read `DovaStash.log` | Lines for: QuickLoot IE (`API V21, info bar + button bar` with QuickLoot IE 4.1+), `UIExtensions: installed, list menus available`, `Activate prompt: ... hooked`, `Overlay: HUDMenu::AdvanceMovie hooked`, the I4 rule count, and `UI: in-world menus are UIExtensions list menus`. |
| 6.1.2 | Disable QuickLoot IE, UIExtensions and I4 in MO2, start the game | The game starts and the log says each one is not installed. The hotkey opens native message boxes; the overlay uses SkyUI's default icons. |

### 6.2 UIExtensions lists (hotkey menus)

| # | Steps | Expected |
|---|---|---|
| 6.2.1 | In an enabled home, press K at nothing | A top-down list headed `-- <home name> --`: Deposit, Sort all, Retrieve..., Browse containers..., template entry, Save as template..., Advanced..., Reset home... It is drawn by your UIExtensions list menu skin. |
| 6.2.2 | Press K at a managed container | A list headed `-- <container name> --`: Categories... (with a summary), Open \<name\>, Clear categories, Ignore, Remove..., Make master, Advanced... |
| 6.2.3 | Categories... -> pick a group -> toggle two categories | The menu stays open: each toggle flips its `[x]` in place, with the selection and scroll position where they were and no close/reopen flash. Debug log: `entry N chosen, list instance M stays open`, then `updated in place`. **< Back** returns to the groups, which show `[x]` / `[~]` / `[ ]`. Esc also goes back. |
| 6.2.4 | Retrieve... -> a group -> a category | The items move to you. Counts match what was stored. |
| 6.2.5 | Browse containers... -> pick a container across the room | That container's own menu opens (SkyUI, with your I4 icons). Items you put in are learned like a normal visit. |
| 6.2.6 | Advanced... | The SKSE Menu Framework panel opens on the same container (or message boxes without the framework). |
| 6.2.7 | Open the list, press Esc; open it again right away | It reopens. No "close the open menu first" message. |
| 6.2.8 | Set `iMode = 1`, restart, press K | Native message boxes, as before. |
| 6.2.9 | Walk through every list: Categories... and a group, Learned items..., Retrieve... and a group, Browse containers..., a display and its Categories... | Each starts with a plain header naming it, then **< Back** on sub-lists: `-- Chest: Categories --`, `-- Chest: Weapons --`, `-- Chest: Learned items --`, `-- Retrieve --`, `-- Retrieve: Weapons --`, `-- Browse containers --`, `-- Weapon Rack --`, `-- Weapon Rack: Categories --`. Choosing a header does nothing (the menu stays open). |
| 6.2.10 | With "Open the lists in the middle of the screen" on (default), open lists of different lengths and the rename box | No jump: each menu appears already centred, never at the skin's own position first. With the debug log, one `centred (... -> ...) after N tries` line per menu. |
| 6.2.11 | Turn that setting off in the panel, press K | The list opens where the skin puts it, shown at once. |
| 6.2.12 | Press K at nothing, then at a container, then open a sub-list | The Dova Stash logo sits centred above every list, clear of the entries, in the same colour as the list text, and the whole menu (logo included) is centred. Debug log: `logo shown`. |
| 6.2.13 | Categories..., a group, Retrieve..., Browse containers..., a display's Categories... | Each category entry has its icon left of the text (your SkyUI/I4 icons, same as the floating icons and QuickLoot); Browse shows each container's icon; the container menu's Categories... shows the container's icon. Text lines up in one column; the header and `< Back` have a blank icon slot. The master menu's actions have no icons. |
| 6.2.14 | In a long list (more rows than fit), scroll down and up with the mouse wheel and the keys | Every row keeps the icon of its own entry (rows are reused while scrolling). No stutter. |
| 6.2.15 | Rename `Interface/DovaStash/logo.dds` in the mod folder, open a list | The list shows after at most ~0.4 s, with an empty space where the logo goes; after ~2 s one warning `did not load` in the log. Restore the file. |

### 6.3 Activate prompt

| # | Steps | Expected |
|---|---|---|
| 6.3.1 | Look at a container bound to Weapons and Armor (QuickLoot disabled) | The prompt reads e.g. *Chest (Weapons, Armor)*. The master reads *(Master)*. Unmanaged and ignored containers are unchanged. |
| 6.3.2 | Save, disable Dova Stash, load | Container names are vanilla again: nothing was written to the save. |

### 6.4 QuickLoot IE

| # | Steps | Expected |
|---|---|---|
| 6.4.1 | Look at a bound container | QuickLoot's info bar shows *Dova Stash: Weapons, Armor*. The button bar shows **Dova Stash** with your hotkey's glyph. |
| 6.4.2 | Scroll the list in the master | The info bar shows *Goes to: \<container\>* for each item, or *Belongs here*. |
| 6.4.3 | Press K while QuickLoot is showing | Dova Stash's container list opens. |
| 6.4.4 | Bind a new category to the container you're looking at, close the list | QuickLoot's info bar updates. |

### 6.5 Floating icons

| # | Steps | Expected |
|---|---|---|
| 6.5.1 | Stand in a set-up home | An icon floats above each managed container in view, using the same icon art as your inventory (I4 icon packs and their colours included). Further icons are smaller and fainter. |
| 6.5.2 | Walk around a wall | Icons behind walls disappear (`bRequireLineOfSight = true`). |
| 6.5.3 | Open any menu, start a conversation, draw a weapon in combat | Icons hide; they come back afterwards. |
| 6.5.4 | Put a stack of swords in an unbound container | Within ~3 s its icon becomes a sword. |
| 6.5.5 | Load a save in a home, then another save elsewhere | No leftover icons, no crash. |

### 6.6 Live settings (SKSE Menu Framework → Dova Stash → Settings)

| # | Steps | Expected |
|---|---|---|
| 6.6.1 | Change the keyboard hotkey to L, close the menu, press L in a home | The Dova Stash menu opens; K does nothing. No restart. `DovaStash.ini` now has `iHotkey = 38`, comments intact. |
| 6.6.2 | Untick "Show icons above containers" | Floating icons disappear at once; tick it again and they return. |
| 6.6.3 | Drag "Icon size", release | Icons resize when you release the slider (one save per release, see the log). |
| 6.6.4 | Switch "What the hotkey opens" between all four modes, pressing the hotkey after each | Each mode opens its menu immediately (log: `UI rebuilt for iMode=...`). |
| 6.6.5 | Untick QuickLoot / activate prompt | QuickLoot's info bar line and the "(Weapons)" prompt suffix disappear immediately. |
| 6.6.6 | Set log detail to Warnings, then back to Trace | The log goes quiet, then detailed again, without a restart. |
| 6.6.7 | Restart the game | Every change from above is still set. |

### 6.7 Feedback round 1 (icons, sort on arrival, rename, learning, QuickLoot, Skyrim Souls)

| # | Steps | Expected |
|---|---|---|
| 6.7.1 | Bind a container to Weapons, put spears, bows, then daggers in it | Its floating icon stays the Weapons category icon the whole time. The master shows SkyUI's "All" icon. |
| 6.7.2 | Open the master (Skyrim Souls: game keeps running), drop three items in, keep the menu open | ~0.4 s after the last item, they leave the master for their containers; the open menu updates; one notification. |
| 6.7.3 | Hotkey at a container -> Rename... -> type "Potions" | The activate prompt, the container menu title, QuickLoot's title and the lists all say "Potions". Rename to an empty name: the original name comes back. |
| 6.7.4 | Put an Iron Sword by hand into a managed container that is not for weapons | Notification "Iron Sword now goes to <container> (K: undo)". Hotkey at it (after the 5 s undo window) -> Learned items (1)... -> Forget Iron Sword removes it. |
| 6.7.5 | QuickLoot on a container holding items that belong elsewhere | Those items read "Iron Sword  >  Weapon Rack" in QuickLoot's list; items that belong there are unchanged. |
| 6.7.6 | Open Advanced... (Menu Framework window), press Esc | The window closes. It always opens centred and fully on screen, with Close at the top. Floating icons hide while it is open. |
| 6.7.7 | QuickLoot on the master, then on a container bound to Weapons, Armor and Alchemy | The master shows "Master" bottom-right. The bound container shows three category icons (your SkyUI/I4 art) bottom-right in binding order, no text, clear of the weight readout; with more than 5 categories the rest show as "+N". The button reads "Stash". |
| 6.7.8 | SKSE Menu Framework -> Dova Stash -> Settings (all homes) -> Keep rules for every home: add "category:alchemy.potions" to Always keep, Save | Settings.json is written; Deposit now keeps potions in every home. Hotkey -> Advanced... -> Keep rules shows them under "Defaults for every home". |
| 6.7.9 | Compare SKSE menu "This home" with hotkey -> Advanced... | Same page, same tabs (Actions, Containers, Learned, Keep rules, Template). |


## 7. What to send back

- `DovaStash.log` from the session, at `iLogLevel = 1`.
- The layout file(s) from MO2 `overwrite/SKSE/Plugins/DovaStash/Layouts/` (for a restore problem, also
  the home's files in `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups/`).
- Each failed step: the table row number, what you saw, and what was in the inventory or container before and after.

## 8. Dova Stash Editor (no game needed)

The editor's one-workspace window (docs/EDITOR.md §2, docs/EDITOR-UX.md). Build it with the `editor` preset
(docs/BUILDING.md), add it to MO2 (EDITOR.md §9) and run it from there; for repeatable runs use a state file of its
own: `$env:DOVASTASH_EDITOR_STATE = "$env:TEMP\ds-test-state.json"`. Every row also works from a terminal with
`--mo2 "<instance>" --gui` and the flags shown; add `--screenshot out.png` to save the window once it has loaded.

| # | Steps | Expected |
|---|---|---|
| 8.1 | Start with no arguments (no state file) | Start panel in the middle: the setup in one line, "N player homes, M with templates", three cards. The rail on the left lists the homes by mod; *Game Data folder* holds the base game and DLC homes; *Rescan* and the scan summary at its bottom. Status row: `N homes`, `Ctrl+K commands`. |
| 8.2 | Rail search: `92177`, then `Whitepeak`, then `Eli_Druid Home.esp` | Each finds Whitepeak Tower or Aldamar under its mod (Nexus id muted). The filter (funnel) with *Only homes without a template* on shows a count badge and hides homes that have one. |
| 8.3 | Click *Whitepeak Tower* (or `--home "Whitepeak Tower"`) | The tower in 3D from above; `archives` / `meshes` / `textures n/m` progress in the status row, not over the view. Title row: the home's name, chip *shipped* / *yours* / *no template*. Drawer tabs with counts. |
| 8.4 | Press 2, then 1 (or `--tab plan`) | Plan, then 3D; the selection is the same in both. The floating toolbar stays: *Select*, *Paint*, *Layers*, *Colours*, *Camera*. |
| 8.5 | Click a chest; Ctrl+click two more; drag a box | Selection panel: its mesh picture, the reference block, *Role*. With three: the bulk editor. A box selects the containers and displays inside. Esc clears. |
| 8.6 | *Make master*, add a category with *+ add*, type a label | The title chip reads *draft*, a dot follows the name (title row, status row). Ctrl+Z / Ctrl+Y undo and redo each step. |
| 8.7 | Press P, *Pick another* ▾ `alchemy.potions`, click two satchels, press S | The paint bar replaces Layers/Colours/Camera; colours switch to by category; S returns to Select. |
| 8.8 | L; untick *Structure*; *Height cut* > *Only between* | The Layers popover; walls go; one floor left. |
| 8.9 | Ctrl+2 (or `--tab containers`), then *Doors*, double-click the exterior row, *Back* | The drawer opens on Containers; Doors lists the cells behind the doors (a click selects the door); the exterior opens with its terrain, the template comes along; *Back* (title row) returns. |
| 8.10 | Drawer *Where items go* (or `--tab preview`), V, click a chest | The table per container; containers coloured by fill in the view above; the Selection panel lists the chest's items (*Where its items go*). R runs it again. |
| 8.11 | *Draft* panel of a home without a template (or `--home Aldamar --inspector draft`) | Accept all / Reject the weak ones / Draft again; the master with its reason; chips per container (green, yellow, orange). A click on a chip adds or removes the category in the Selection panel and the preview at once; Ctrl+Z takes it back. |
| 8.12 | Ctrl+D (or `--drafts`) | Progress `Drafting all homes n/m` in the status row; then a *drafted* chip on each home and the *Save N drafts...* banner over the rail. The start panel's *In progress* does not gain every home. |
| 8.13 | Edit a draft, open another home in the rail, come back | The first home keeps its changes and the dot on its row; closing the window asks first (*Quit without saving* / *Keep editing*). |
| 8.14 | Click `0 errors · 0 warnings` in the status row (or `--inspector checks`) | Checks panel: the problems (a click selects the object), then *Still to do* (no master, containers without a role). |
| 8.15 | *Template* panel (or `--inspector template`): *Add keep rules*, add `alchemy.potions` to *Always keep*, a quota; Save (Ctrl+S) | The JSON (folded *JSON* head) has `keep` with them. The file lands in `mods\Dova Stash Templates\SKSE\Plugins\DovaStash\Templates\<Home>\<Author>.json`; the panel's *This home's templates* list shows it as *yours* and marks it open next to any shipped one (click another row to open that file instead); saving again asks first, keeps a `.bak`, raises the version. Save is greyed out while the checks find an error. |
| 8.16 | Ctrl+K (or `--palette`): type `layers off`, Enter; Ctrl+K, a container's name | Only containers and displays shown; the container is selected and framed. Esc closes the palette. |
| 8.17 | `⚙` (or `--popover setup`): change the profile | The rail scans the other profile's load order. |
| 8.18 | Ctrl+1, Ctrl+3, drag the drawer taller; restart | The rail and inspector fold to strips; after the restart the layout and the view are as you left them. |
| 8.19 | `--window 1280x720` | Still usable: the title row shortens the home's name ("..."), the rail ends its summaries in "...". |
| 8.20 | Ctrl+= twice, Ctrl+-, drag `⚙` › *UI size* to 180 %, then its *Auto* link; restart | Every text and control grows 10 % a step, nothing clips; the slider follows and applies when released; Auto reads 125 % on a 1440p monitor at 100 % Windows scaling; the size survives the restart. |
| 8.21 | `⚙` › *Theme*: pick *Windows10 Dark*, *Light*, then *Dova Stash*; Ctrl+K `theme carbon`; *Export...*; change `accent` in the file it shows, open the list again and pick the theme under *Yours*; restart. Then `--theme "Tablet Light"` | Each pick recolours the window before the next frame (fonts and layout unchanged); text, chips, the Save button, the selected rail row, the status row, popovers and the drawer's tables stay readable; the 3D view and the plan stay dark with their data colours. *Export...* writes `%LOCALAPPDATA%\DovaStashEditor\Themes\My Carbon.json` and selects it in Explorer; a second *Export...* does not overwrite it. The edited theme applies when picked; the pick survives the restart; `--theme` lasts one run (the file keeps the remembered theme). |
| 8.22 | `--tab preview`, select a chest; double-click *Where items go*, then *Containers*; double-click a row; click the open tab once; double-click *Displays* on the folded drawer; double-click the drawer's, the rail's and the inspector's chevrons (each twice: fold, unfold) | Double-clicking a tab leaves the drawer open on that tab; a row is selected and framed in 3D; a single click on the open tab folds the drawer about a third of a second later; the folded drawer opens on *Displays*. No double click selects or deselects anything other than the row: the selection stays through the chevrons, whose first click folds or unfolds. Ctrl+2 still folds and opens the drawer at once. |
| 8.23 | Drawer *Containers* (or `--tab containers --sort containers:role --filter chest --quick Ignored`): click *Kind*, Shift+click *Height*, click *Role* until its arrow goes; type `sack` in *Filter rows*, Esc; *Kind* ▾ Barrel; chips *Ignored*, *Respawning*; Ctrl+click two rows, double-click one; the same on *Displays*, *Doors*, *Where items go* | Rows sort by kind, then height; the third click brings the table's order back; *ignored* / *not listed* / *no categories* sort after the categories, the master first. The box and chips hide rows ("N of M" beside them; Esc clears the box); selection, Ctrl+click and double-click (frame; on Doors: open the cell) work on the shown rows. Switching tabs and homes keeps each table's sort and filter until the editor closes. |
| 8.24 | A big home on the plan (`--home "Lakeview Manor" --tab plan --popover layers`, also Heljarchen Hall, Whitepeak Tower): L, tick *Effects*, then *Other*; untick *Structure*, *Furniture*, *People*, *Disabled* one by one; hover and click where a hidden footprint was; Home. Then press 1 and untick *Effects* (or `--layers effects,other`, `--layers -structure`) | The plan opens with *Effects* and *Other* off: no large faint boxes of fog, mist, light beams, Hearthfire's crafting triggers or `defaultBlankTrigger`. *Effects* adds faint blue outlines (dust beams, lights), *Other* grey ones (triggers, markers, room bounds); each untick removes its footprints at once, and nothing hidden shows a tooltip or gets selected (a chest under a trigger is picked first). Before you pan or zoom, each change re-fits the plan to what is shown, never to a fog bank or a trigger. In 3D the *Effects* tick is the 3D view's own (on): unticking it hides the glows and the dust beams; the plan's stays as it was. |
| 8.25 | `--draft-all --save-to <scratch> --author Tester` twice, with a hand-made `Templates/<Home>/Tester.json` (no `autoDraft`) for one home and another author's file for a second home already in `<scratch>`; then save one auto-draft as a person (`--cell <its cell> --draft --save-to <scratch>`, or open it in the window and Ctrl+S) and run it once more; in the window, `--drafts --popover savedrafts` | The new files start with `"autoDraft": true`; both hand-made files are unchanged (same hash, no `.bak`) and each has a *not saved: ... is a template someone made* line; two homes of the same name: the second says *another home's auto-draft*; the second run says *refreshed the earlier auto-draft* (`.bak`, version raised); the person's save has no mark and the last run leaves it alone. The dialog counts new and refreshed files and lists the homes left alone; a home whose only template is an auto-draft shows the *auto-draft* chip. |
