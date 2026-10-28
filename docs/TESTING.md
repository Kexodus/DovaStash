# Dova Stash: in-game test script (M1 + M2 + M4)

This script is run by hand in Skyrim. It covers the M1 exit criterion (enchanted, tempered and
renamed items survive round-trips), the M2 exit criterion (the full manual workflow in
Breezehome) and the M4 Menu Framework panel (section 5). Section 8 is the Dova Stash Editor, outside the game.
Section 9 runs much of sections 1-4 by itself, unattended (`tools/autotest/run.ps1`).
Unit tests for `src/core` are separate: `ctest --preset core-tests`.

Test rig: Skyrim AE 1.6.1170 through MO2, with SKSE and Address Library installed. Optional mods:
QuickLoot IE, SKSE Menu Framework, KID.

## 0. Setup

1. Build and deploy the plugin (see `docs/BUILDING.md`). The MO2 mod folder must contain:
   - `SKSE/Plugins/DovaStash.dll`
   - `SKSE/Plugins/DovaStash.ini`
   - `SKSE/Plugins/DovaStash/Categories/00_Default.json`
   - `SKSE/Plugins/DovaStash/Templates/...`

   Validate the shipped data against the test profile, which has every home mod the `Templates/Mods` files need:
   `python -I tools/esm_dump.py --profile "Dova Stash AutoTest" check dist/Data/SKSE/Plugins/DovaStash` (the MO2
   instance comes from `--mo2 <instance folder>`, else `DOVASTASH_MO2`, else the developer's instance,
   `E:\modding\Kexodus Skyrim`, so it works in a fresh shell there).
   **Expected:** `"ok": true`, no errors, and 5 warnings of two kinds: *starts disabled* (the masters of Nchuanthumz
   and Hjerim) and *respawns; accepted because its cell never resets* (the Gallows Hall StrongBox and two Black Fur
   Refuge StrongBoxes). The Hearthfire cellar chests (`cells[0].master` HearthFires.esm|0x0000BE8F / 0x00015189 /
   0x00017489), which start disabled while the hall's persistent master covers the cellar, are the info
   `areaMastersBuiltLater`, not a warning. Without `--profile` the check reads
   the plain Data folder: each of the 13 `Templates/Mods` files then also warns *required plugins not in the load order,
   refs not verified*, and their refs are not checked. The optional
   OCF set too: `python -I tools/esm_dump.py check dist/Optional/OCF/Data/SKSE/Plugins/DovaStash` (`"ok": true`; its
   KID keywords come from `tests/data/ocf_keywords.txt`, or `--kid <folder>` for OCF's own `*_KID.ini`).
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
Settings: iLogLevel=1 bAutoEnableTemplates=false iHotkey=37 iGamepadHotkey=-1 bDistributeOnMasterClose=true bSortOnArrival=true bLearn=true ...   (every INI key)
Dova Stash v0.2.4 loaded
Runtime: Skyrim AE 1-6-1170-0 (SKSE 0x2020080, plugin built against CommonLibSSE-NG SE+AE+VR)
Dova Stash loaded
SKSE Menu Framework: installed (v3.80)                                   (when installed)
QuickLoot IE: API V21, item destinations + info bar + button bar         (when installed)
kDataLoaded: forms are available
FormUtil: <n> full + <m> light plugins loaded
Icons: SkyUI item icons '...', category icons '...', item icon colours on
Categories: 46 loaded (0 errors, 0 warnings)
Templates: 32 loaded (0 errors, <w> warnings)
Keep defaults: built-in (no Data/SKSE/Plugins/DovaStash/Settings.json)
Homes that sort themselves: 14 listed, <k> apply in this game (0 errors, 0 warnings)
  Clockwork (Clockwork.esp): its cells are left alone                    (one line per entry whose plugin is loaded)
Home backup: copies go to C:/Users/<you>/Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups
SKSE Menu Framework: stash window and "Dova Stash" section registered    (when installed)
UI: in-world menus are UIExtensions list menus                           (or "native message boxes (UIExtensions not installed)")
UI: editor is the SKSE Menu Framework panel                              (or "native message boxes")
Events: container-changed and cell-loaded sinks registered
Events: menu sink registered
Events: crosshair sink registered
Events: crafting station sinks registered
Events: sell chest sinks registered
Crafting hooks: installed (...)
Materials at the workbench: craft from storage (auto, game data loaded)
UIExtensions: installed, list and text-entry menus available
Input: hotkey sink registered (key 37, gamepad -1)
Activate prompt: TESObjectCONT and TESObjectACTI GetActivateText hooked
Overlay: HUDMenu::AdvanceMovie hooked
Environment:
  QuickLootIE.dll                  4.1.3.0      (info bar + button bar)  (one line per DLL and plugin Dova Stash works with)
  ...
  In-world menus: UIExtensions lists (iMode = 0)
  Runtime: AE
kPostLoadGame (success: true)
Events: player cell sink registered
```

**Fail if** you see any `[error]` line during startup. Template warnings are acceptable, for example
a `requires` plugin that isn't loaded.

## 1. Eligibility and safety (§9)

| # | Steps | Expected |
|---|---|---|
| 1.1 | `coc WhiterunBreezehome` | Log: `Cell Breezehome (Skyrim.esm\|0x000165A8): template available` and `template: Breezehome v1.0.2 by Dova Stash ...`. One HUD line: *Dova Stash: Breezehome setup available (press K)*. |
| 1.2 | Leave and re-enter Breezehome in the same session | No second notification. |
| 1.3 | `coc WhiterunBanneredMare` and press K at nothing | HUD: *Dova Stash: not a player home (owned by ...)*. No menu opens. |
| 1.4 | `coc BleakFallsBarrow01` (an unowned interior, so the cell passes). Look at a barrel or an urn and press K | HUD: *can't use Barrel (respawns, items would be lost)*. No menu opens. The log gives the long form. |
| 1.5 | Go outside (`coc WhiterunOrigin` or any exterior) and press K | HUD: *not a player home (exterior cell not owned by you)*. |
| 1.6 | In Breezehome, look at Lydia (or a door) and press K | It acts as if nothing were targeted: the setup prompt with only the template option. |
| 1.7 | Open the console, then the inventory, then a dialogue, and press K in each | Nothing happens. The debug log shows `Hotkey ignored`. |
| 1.8 | Lakeview Manor (`coc BYOHHouse1Exterior`, then go inside) before building any furniture: look at an unbuilt container spot | Unbuilt (disabled) furniture can't be targeted. After the template is applied, the log lists each one: `Template container ... unavailable: container is disabled (not built yet?)`, and the HUD reads *Dova Stash: &lt;home&gt; is set up (N containers unavailable for now)* (*1 container* for one; on the first visit *(press K; N containers unavailable for now)*), counting only disabled, missing or refused containers; one in another area that is not loaded yet is logged at debug (*waits for its area*) and not counted (*Dova Stash: Tundra Homestead is set up*, no count). A Deposit afterwards logs unbuilt furniture at debug, never as a warning. A disabled container reads *not built yet* only when something can enable it (an enable parent); one with none reads *disabled* (log: *container is disabled (no enable parent)*). |
| 1.9 | Severin Manor (Dragonborn), before and after you buy it | The cell is accepted both times: it is a player house (`LocTypePlayerHouse`). It is owned by `DLC2RRSeverinManorFaction`, which you join on purchase; before that, binding a container logs a warning, *... is in a player house still owned by DLC2RRSeverinManorFaction*. |
| 1.10 | White Lighthouse (`WhiteLighthouseSE.esp`, `coc WLighthouseInterior`) before you own it: press K | HUD *Dova Stash: not a player home (owned by WLightHouseFaction)* with powerofthree's Tweaks (editor IDs), else *(owned by a faction)*; never a form ID. |

## 2. M1: master, Deposit, keep defaults, extra data

Start from a clean Breezehome (no layout file).

### 2.1 Make a master

1. `coc WhiterunBreezehome`. Look at the chest next to the alchemy lab and press **K**.
2. The prompt *Dova Stash: set up Breezehome?* shows these buttons: *Make Chest the master*,
   *Use template "Breezehome"*, *Open panel...* (with the SKSE Menu Framework), *Cancel*. Choose **Make ... the master**.
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

1. In Breezehome, press K at nothing. The master menu shows *Deposit*, *Sort all*, *Retrieve...*,
   *Advanced...* and, with UIExtensions installed, *Rename Chest...* (message boxes add *Cancel*). *Advanced...* has
   *Browse containers...*, *Use template "Breezehome"*, *Save as template...*, *Reset home...* and, with the SKSE
   Menu Framework, *Open panel...*. The UIExtensions list shows the same entries (§6.2.1).
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
special copy left anyway, it is moved back, the log has a line
`Move of plain ... took a tempered, enchanted, renamed or worn copy; put N back` (`[error]`, or `[warning]` when
every special copy is back), and from then on plain copies **of that item** next to a special copy stay where
they are (`Move skipped: ... left in ...`) until the game restarts. Ammo is the exception: with a worn stack of
an arrow or bolt, its plain copies count as equipped too and stay (the game equips ammo as a whole).

1. `player.additem 0001397E 3` (Iron Dagger x3) and equip one dagger.
2. Deposit.
3. **Expected:** 2 daggers go to their destination, the equipped one stays equipped. The log has
   `Moved Iron Dagger x2 (plain, beside a special copy) <player> -> ...` and no `[error]` line.
4. Temper one of two Iron Swords at a grindstone, put a keep quota of 1 on swords, Deposit. **Expected:**
   the tempered sword stays with you (quota) and keeps its tempering; the plain one goes to its destination.
5. Put a tempered and a plain Iron Sword into the master: both are sorted (the tempered one moves first).
6. Arrows picked up after equipping: equip 50 Iron Arrows, then `player.additem 0001397D 20`, Deposit.
   **Expected:** all 70 arrows stay with you, still equipped (the log has `Deposit: Iron Arrow ... stays: kept by
   always equipped`, possibly twice: the worn stack and the plain copies beside it); no `Move of plain ...` line.
7. **Fail if** the `Move of plain ...` line appears: send the log (the engine chose a special copy).

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
2. **Categories...** lists category roots with `[ ]` / `[~]` / `[x]` marks.
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
   Gyro now goes to End Table. K: undo*. Press **K** within 5 s (aimed at anything, or at nothing).
   HUD: *Dova Stash: undone, Dwarven Gyro is not learned*; the log has `Learn: 1 learn(s) undone in
   Breezehome`; the layout file has no `learned` entry for it, and the end table is gone from
   `containers` again (it only entered the setup with that learn). Repeat and wait 6 s before pressing K:
   the hotkey opens the usual menu, nothing is undone.
2. Put three different items in at once and close. HUD: *3 items now go to End Table. K: undo*
   (no item list). K within 5 s undoes all three.
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
   - **Yes.** HUD: *Dova Stash: End Table now takes Other books*. The layout: the end table has
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

1. Unbind the cupboard (container menu → Categories... → Weapons → All Weapons toggles off), and bind it to *Weapons → Swords* only.
2. Master menu → **Sort all**. Daggers now sitting in the cupboard move back to the master
   (homeless), and swords stay. HUD: *re-sorted ...*.
3. Master menu → **Retrieve...**. Only roots with items appear, with counts, e.g. *Weapons... (5)*.
   Choose **Weapons... → Swords (3)**. Only swords come back (*retrieved 3 items from 1 container*).
4. Retrieve a root with one category, e.g. *Keys*. It retrieves directly, with no submenu.
5. With more than 50 items stored, **Retrieve... → Everything (N)**: it asks *Retrieve N items? You may be
   over-encumbered. Deposit puts them back.* No goes back to Retrieve...; Yes retrieves them. With 50 or fewer it
   retrieves at once.

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
3. Container menu on another chest → **Make master...**. It asks first, in the hotkey menus as in the panel (*Make X
   the master? Its categories and learned items are forgotten; the current master becomes an ordinary container. No
   items are moved.*, without the first clause when the chest has no categories or learned items); **No** returns to
   the container menu. **Yes**: the old master becomes unmanaged and the new one is the master. The old master's
   items are not moved.
4. Master menu → **Advanced...** → **Reset home...** → *Yes* (the question: *Reset Breezehome? Its setup is
   backed up (.bak). No items are moved.*). HUD: *Breezehome reset (setup backed up)*. The layout
   file is renamed to `<file>.json.<YYYYMMDD-HHMMSS>.bak`; only the newest 5 backups are kept.
   **No items are moved.**

### 3.6 Templates (zero-touch preview of M3)

1. After a reset, press K at any container and choose **Use template "Breezehome"**.
   HUD: *Dova Stash: Breezehome is set up*. The layout file has `"basedOn": "Templates/Vanilla/Breezehome.json@1.0.2"`.
2. Deposit. Loot is spread over the bound containers.
3. Master menu → **Advanced...** → **Re-apply template** (the label says Re-apply only for the template the
   layout is based on; another template reads **Use template**). With no changes of your own it applies at once;
   bind a category by hand first and it asks *Use the template "Breezehome"? It replaces your changes: ...*
   (*Yes*). A template that may not fit reads *... (may not fit)* and its question adds *It may not fit this home.* A timestamped `.bak` copy of the
   layout is written first.
   Category bindings come from the template; learned destinations, keep rules, ignored containers,
   explicit forms and containers you added yourself (now without categories) are kept.
4. Set `bAutoEnableTemplates = true` (the default), start a new session and enter Honeyside (`coc RiftenHoneyside`).
   HUD: *Dova Stash: Honeyside is set up (press K)*. Press K without
   aiming at anything: the master menu opens; Deposit puts the loot away.
5. **Save as template (hotkey).** In Breezehome with a setup of your own (master, a few containers with
   categories, one ignored container, a rack with categories, a mannequin with an armor set, a keep
   rule, some learned items): master menu → **Advanced...** → **Save as template...**. The text box shows *Breezehome*;
   type *My Breezehome* and confirm. HUD: *Dova Stash: saved as template "My Breezehome" (incl. N learned
   items)*. The file `Templates/User/My Breezehome.json` (MO2: `overwrite/SKSE/Plugins/DovaStash/`) has
   your character's name as `author`, `"version": "1.0.0"`, the Breezehome `cell`, no `learned` and no
   `basedOn`, the master, containers, displays (with `set`) and `keep`; each learned item is in the
   `forms` of the container it was learned for (none for the master or an ignored container), and
   `requires` lists every plugin the refs and those items point into. Log: *Exported Breezehome as
   template "My Breezehome" v1.0.0 ... N learned item(s) kept as item bindings ...*, then
   *Templates: N loaded* and the home's template line.
6. **Offered right away.** Press K again (no restart): the master menu's **Advanced...** now has
   **Use template "My Breezehome"** (the player's own templates rank before the shipped one). The
   panel's **Template → Templates** lists it as *(offered first)*, and **Template → All templates**
   lists it.
7. **Same name again.** **Advanced...** → **Save as template...** → *My Breezehome* → a box asks *Replace the template
   "My Breezehome"? Templates/User/My Breezehome.json (v1.0.0) is saved as v1.0.1; the old file is
   backed up.* **No** writes nothing (check the file time). **Yes** writes v1.0.1 and leaves
   `My Breezehome.json.<YYYYMMDD-HHMMSS>.bak` next to it. Save a different home under the same name: the
   box adds *It is the template of another home.*
8. **Round trip.** Master menu → **Advanced...** → **Reset home...** → *Yes*, then press K at a container →
   **Use template "My Breezehome"**. The layout file has the same master, categories, ignored
   containers, displays, mannequin set and keep rules as before, an empty `learned`, the learned items
   as `forms` of their containers, and `"basedOn": "Templates/User/My Breezehome.json@1.0.1"`. Deposit
   sorts as before, learned items included. (Exported from the panel with *Include where you put things by hand* unticked,
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
   (2026-10-12 14:03)* (the copy's time), then *Make ... the master*, *Use template ...* and, with the SKSE
   Menu Framework, *Open panel...*. Choose it. HUD: *Dova Stash: Breezehome restored*; the layout file is back with the
   same master, categories, learned items and displays; Deposit sorts as before. Same with `iMode = 1`
   (message boxes, *Restore your last setup (...)* first).
5. **Reset keeps it.** Master menu → **Advanced...** → **Reset home...** → *Yes*. The layout goes to a `.bak` as before;
   the copy in `Backups/` stays, and the hotkey offers *Restore your last setup* again. Set the home up
   anew (Make master): the copy from before the reset moves to history first (log line as in 2).
6. **Broken copy.** Break `Backups/Skyrim.esm_000165A8.json` (delete a brace) and delete the layout file.
   Choosing *Restore your last setup* gives *Dova Stash: that saved setup is broken (see DovaStash.log)*;
   the log says *could not be parsed; it is not used*. Nothing is written; the home stays not set up.
7. **No Documents access** (make `Backups` read-only): saving still works; the log warns once
   (*Home backup: could not write ...*), then only at debug.
8. Panel: see 5.2 step 11.

### 3.6b Other home mods: homes that sort themselves, rebuilt homes (DESIGN §9.1)

Use a throwaway save. The `Default` profile has Clockwork, Legacy of the Dragonborn, Mirele Bismath Reborn and
Wynter's Breezehome Redux; `Dova Stash Testing` adds LUFTSHIP K747, Skyfall Estate and Riverside Lodge.

1. **Startup.** The log lists *Homes that sort themselves: 14 listed, k apply in this game* and one line per
   home whose plugin is loaded (Clockwork, Legacy of the Dragonborn, Mirele Bismath Reborn: *its cells are left
   alone*). No `[error]` line.
2. **Left alone.** Enter Mirele Bismath (and Clockwork's castle, the Safehouse) and press K at a container and
   at nothing. HUD: *not a player home (Mirele Bismath Reborn.esp sorts items itself)*; no menu, no template
   notice, no layout file written. Same message as before for Clockwork and Legacy of the Dragonborn.
3. **Conflicts (Wynter's Breezehome Redux enabled).** New session, `coc WhiterunBreezehome` with no Breezehome
   layout file. Log: *template Templates/Vanilla/Breezehome.json not offered: WynterBreezehomeRedux.esp rebuilds
   this home (conflicts)*, then *no layout*. No HUD notice; K at a container offers *Make ... the master
   container* but no *Use template*. Panel → This home → Template → *All templates* (untick **Only installed
   homes** if needed): Breezehome reads *conflicts:
   WynterBreezehomeRedux.esp* (tooltip *Not offered: a mod that rebuilds this home is loaded.*). Disable
   Wynter's: the template is offered again (§1.1).
4. **The auto-enable guard (Wynter's still enabled).** Copy `Templates/Vanilla/Breezehome.json` to
   `Templates/User/Breezehome test.json` and delete its `"conflicts"` line. With `bAutoEnableTemplates = true`,
   start a new session and enter Breezehome (no layout file). **Expected** (if Wynter's took out most vanilla
   containers): no setup is enabled; HUD *Dova Stash: Breezehome setup may not fit (press K)*; log, once: *Breezehome: template Templates/User/Breezehome test.json may not fit this
   home (another mod may have rebuilt it): x of y references there (... disabled or deleted, ... missing ...);
   offered, not enabled automatically*. K at a container: *Use template "Breezehome" (may not fit)* (message
   boxes: *Template available: ..., may not fit this home*). Choosing it still works and reports the
   unavailable containers (*N containers unavailable for now*). Nothing is moved or deleted. Note the counts
   in the log either way: if Wynter's keeps 60% or more of them, the template is enabled and the log line is the
   debug *... fits: ...*. Delete the test file afterwards.
5. **Hearthfire is not "rebuilt".** New game (or a save that never visited it), buy Lakeview Manor, build only
   the small house, `bAutoEnableTemplates = true`, enter. **Expected:** the template is enabled as before
   (*Lakeview Manor is set up...*); debug log *template ... fits: n of n references there (... not built yet
   ...)*. The cellar likewise once built.
6. **A cells entry and a containers entry of your own.** Add
   `Data/SKSE/Plugins/DovaStash/SelfSorting/50_Test.json` (MO2: a new mod or `overwrite`):
   `{"$schema": "dovastash-selfsorting-1", "homes": [{"plugin": "Skyrim.esm", "cells": ["ccEEJSSE001-Hstead.esm|0x0000087F"]},
   {"plugin": "Skyrim.esm", "containers": ["Skyrim.esm|0x000CB437"]}]}`. Restart. Log: two more entries
   (*1 cell(s) left alone, 0 container(s) not used* and the reverse). Tundra Homestead (Creation Club) is left
   alone (*Skyrim.esm sorts items itself*; stands in for Tundra Homestead - LotD Storage). In Breezehome (no
   Wynter's) K at the loft cupboard by the weapon plaque: *can't use Cupboard (Skyrim.esm sorts this container
   itself)*; with the Breezehome template enabled, Deposit sends weapons to the master instead, and the panel's
   Containers tab shows the cupboard grey with that reason; the layout keeps its entry. Delete the file and
   restart: all as before.
7. **Broken files.** Put `{` in `SelfSorting/50_Test.json`: log *[error] Homes that sort themselves: ...
   50_Test.json ...*, the panel's About page lists it under *Diagnostics*, *Homes that sort themselves (SelfSorting)*,
   and the shipped entries still apply. Rename `00_Default.json` away as well: *[warning] Homes that sort
   themselves: no list loaded; leaving Clockwork and Legacy of the Dragonborn alone*, and step 2 still holds
   for those two. Put the files back.

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
   with *Use this rack*. Choose it. **Expected:** HUD *Weapon Rack is used now*; nothing goes on the rack yet. (A rack that already holds a
   weapon or shield says *Weapon Rack is used now (full)*, and Deposit leaves it alone.)
4. Its menu opens again by itself: *Categories... (any weapon)*, *Fill now*, *Stop using this rack*. Open
   *Categories...*, tick *Bows*, go back: the entry reads *Categories... (Bows)*.
4a. Categories that fit. Open *Categories...* on a plain rack or weapon plaque, a dagger display, the middle and a
   side of a coat-of-arms rack, a mannequin and a bookshelf. **Expected:** the plain rack lists *All Weapons* and
   each weapon category (*Crossbows* only on a rack with a crossbow spot, the Unofficial Patch's), never Shields,
   soul gems, potions or misc; the dagger display *All Weapons* and *Daggers*; the coat-of-arms middle *All Armor*,
   *Shields*, *Heavy armor*, *Light armor*; its side every weapon category but Daggers, Bows and Crossbows; the
   mannequin the armor categories but Shields; the bookshelf the book categories. The panel's *Displays* page
   offers the same per display. Then, in this home's layout file, give a used weapon plaque
   `"categories": ["soulgems.*"]` and leave and re-enter the home. **Expected:** the log has, once, `Displays:
   <plaque>: Soul gems can't go on a weapon rack; ignored (nothing goes on it)`; Deposit with a soul gem and a
   sword puts nothing on that plaque; the file still lists `soulgems.*`.
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
     `Display: <item> from <player> on <rack> (rack) in <cell>`;
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
   set*); nothing is taken off. With `iMode = 1` the message box lists at most 15 entries (3 pages): `Auto`, the
   current set, the complete sets, then the most complete others; with more sets its body adds *More sets: Dova
   Stash > Displays* (panel loaded), else *(N more sets not shown)*.
8c. Choose a set for a mannequin that wears another one (the Daedric mannequin → Ebony). **Expected:** HUD
   *... wears Ebony once you take its armor off*; Deposit puts nothing on it; take the Daedric pieces off by
   hand, Deposit: the Ebony pieces you have go on.
8d. Armor set picker (SKSE Menu Framework → Dova Stash → *Displays*, select a mannequin, open *Armor set
   (Auto)*; moved from the former *Armor sets* page). **Expected:** the first open takes a moment once; the log
   has one `Armor sets: N sets from M armor pieces in the load order` line. The table lists every set, the ones
   you have first (complete ones first). Searching `ebony` lists `Ebony` (Skyrim.esm) and modded Ebony-tagged
   sets separately; a mod's name or a piece's name finds its set; *Only sets you have* narrows to sets with
   pieces you carry or store (the *You have* column, e.g. `1/4`); hovering a set lists its pieces, `(have)` on
   yours. Click a set you have no piece of (e.g. Dragonplate): the node reads *Armor set (Dragonplate)* and the
   row *set: Dragonplate*, Deposit puts nothing on it; add a Dragonplate piece, Deposit: it goes on. **Auto**
   returns it to Auto. Select a mannequin not in use: no picker, only *Use it to choose its armor set.*; one turned
   off with *Stop using* refuses a set (*Dova Stash: &lt;name&gt; isn't used*).
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
   *ignored*, the weapon stays. On a mannequin, *Armor set (Auto)*: the set picker (8d). Under *Use all*,
   *Use all 12 racks...* asks first (*... 1 ignored stay off.*); Yes: HUD *12 displays used now*, the ignored
   rack stays *ignored*, nothing is moved until Deposit. In a home with areas (§3.9), the *Area*
   column names each display's area; displays in the other loaded area show, with **Fill now** greyed
   (*Only displays in the area you are in are filled.*); a used display in an unloaded area reads *used (not
   loaded)*. A display case used as a storage container is not listed. Outside a home: *Go to one of your
   homes to see its displays.* *Use all* is automated (plan 14 3.7-useall).
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
     `Display: <book> from <player> on <shelf> (bookshelf) in <cell>` per book and `<shelf>: its UpdateBooks placed
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
   - A book you did not take that goes missing from a shelf (Wuth Rein's `0x13C5`, §9.5) is put back: the
     put-back looks in the cell's containers first and moves that copy back (log `<book> was in <container>; it
     went back to ...`); only if it is found nowhere does a fresh copy go back (log `<book> was found nowhere in
     the cell; a copy goes back`). Deposit and Sort all then leave that shelf alone for the session.
   - **Fail if** a shelf shows a book its menu doesn't list (or the other way around after reopening it), the
     game says there is no more room, a book is handed back, the Elder Scroll or a note goes up uninvited, a
     title appears twice in the home, or a book disappears (check the `Inventory:` journal lines). Send the log.

### 3.8 Materials at the workbench: Move (DESIGN §8.5)

Breezehome (forge, tanning rack, alchemy lab, enchanter, cooking pot) or any home with stations, set up with a
master and a chest. `iLogLevel = 0`. Settings → *Materials at the workbench* → **Move** (`iCraftingSupply = 2`);
3.8b tests the default, Craft from storage.

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
9. Settings → *Materials at the workbench* → **Off**: sitting down brings nothing. Turn on *Weapons and armor used
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
   brings 3 (the default *always keep lockpicks* does not block it); Deposit leaves all 5 on you. The Settings
   page's Default keep rules show the quota with Restock ticked.
11. Ignored containers and displays: put potions in an ignored barrel and on nothing else; Restock takes none
   (*nothing to restock* if storage has no other potions). Quest items in storage never come.
12. Multi-cell home (§3.9): from the grounds, Restock takes only from containers reachable there, as Retrieve does.
13. **Fail if** you ever end up with more than a quota's number because of Restock, an item disappears (check
   the `Inventory:` journal), something comes from an ignored container, a display or outside the home, or a
   Deposit right after a Restock moves restocked items back.

### 3.8b Materials at the workbench: Craft from storage (DESIGN §8.5)

Same home as 3.8, Settings → *Materials at the workbench* → **Auto** (`iCraftingSupply = 0`, the default),
no other crafting mod installed. `iLogLevel = 0`.

1. Startup. **Expected:** `Crafting hooks: installed (item count ..., inventory list .../..., PlayerCharacter::RemoveItem)`
   and `Materials at the workbench: craft from storage (auto, game data loaded)`. The Settings page shows
   *Auto (now: craft from storage)*. On VR instead: `Crafting hooks: not on VR ...` and `move (auto, ...)`.
2. Store materials as in 3.8 step 1 and carry none: Deposit everything, then make sure you carry no Iron Ingot,
   Leather Strips or Leather. Put **one** Iron Ingot into the master and **one** into another container by hand
   (two containers hold ingots), and keep the rest of the ingots where Deposit put them.
3. Sit at the forge. **Expected:** HUD *Dova Stash: N materials at hand*; the log has `Crafting from storage at
   <forge> in <home>: N materials (K kinds) in C containers` and a debug line per material (`Iron Ingot x12 in
   Chest, Master`). **No** `ItemMover: moved` line and nothing in your inventory changes (check with Tab before
   and after). The forge lists the recipes the stored materials allow, with counts like `Iron Ingot (12/1)`.
   When the menu opened before Dova Stash saw the station, the log also has (debug) `... the crafting menu was
   open before the session began; its recipe list was built again`, and the list still shows the stored counts.
4. Craft one Iron Dagger. **Expected:** the dagger is in your inventory; one ingot and one strip are gone from
   the containers (open them), nothing else moved. Log (debug): `Crafting from storage: Iron Ingot ... x1:
   carried 0, brought 1 from ...` and the same for the strip; one `ItemMover: moved 1 items` per material.
   The ingot came from the container ingots are sorted to (bound container first), not from the master.
5. A recipe needing more than one container holds: pick an iron recipe that needs several ingots (the
   requirement line says how many, e.g. an Iron Greatsword). Leave the menu and arrange storage so that the
   ingot container holds one ingot fewer than the recipe needs and the master holds one (put the others in an
   ignored container). Sit down again and craft it. **Expected:** the item is made; the ingot container is
   emptied first, then the master's ingot (`brought N from <container>, Master`); the menu's counts go down by
   what was used.
6. Carry some yourself: `player.additem 0005ACE4 1`, craft an Iron Dagger. **Expected:** your own ingot is used
   (`carried 1, brought 0`, no move), the strip comes from storage.
7. Leave the menu. **Expected:** `Crafting from storage (crafting menu closed): N materials taken from storage
   at <forge> (... crafts; <containers>)`; no HUD *put back* line, nothing moves.
8. Smelter (ore → ingot), tanning rack (leather → strips), cooking pot (any recipe whose ingredients are all
   stored), grindstone and armor workbench (improve gear you carry). **Expected:** each lists what storage
   allows; the gear you improve is your own (stored weapons are never offered for tempering); the new ingots,
   strips, food stay with you; materials leave the containers only when used.
9. Alchemy lab: store `player.additem 00077E1C 5` (Blue Mountain Flower) and `player.additem 0004B0BA 5`
   (Wheat) by Deposit, carry **2** Blue Mountain Flowers yourself. Sit down. **Expected:** the ingredient list
   shows Blue Mountain Flower **once** with 7, and Wheat with 5 (stored only); no duplicate lines. Make a potion
   from both. **Expected:** one of your own flowers is used first (`carried 2`), one Wheat comes from storage;
   the potion stays with you. Make 3 more: your second flower is used, then stored ones. Favorite one of your
   flowers first and sit again: the flower line then shows only what you carry (the favorited copy keeps the
   game's own entry).
10. Enchanter: filled soul gems are **brought** as in Move (log `Crafting supply: brought ...`), the unused ones
    go back when the menu closes. Expected log on sitting down: no `Crafting from storage at` line.
11. Big hoard: a home with thousands of stored items (for example `player.additem 0005ACE4 2000`, the same for
    20 other materials, Deposit). **Expected:** sitting down shows no hitch (compare with Move in 3.8: that
    moves every stack); the log's `Crafting from storage at` line comes within a frame or two of sitting down.
12. Menu closed mid-craft: start a craft and close the menu (Tab) on the confirmation, and once while the craft
    sound plays. **Expected:** the session ends with its summary line; nothing else moves; no material is lost
    (count the containers before and after), and a confirmed craft produced its item.
13. Get up without crafting, sit again at another station, and quickload while seated. **Expected:** `...
    (got up)`, `(another station)` and `(loading a game)` summary lines; nothing moves in any of them.
14. Remove materials behind the menu's back: sit at the forge, open the console and `removeallitems` on the
    container that holds the ingots (`prid <id>` first), then craft something that needs ingots. **Expected:** the
    craft takes what you carry (as without Dova Stash), the log says `... of the ... needed could be brought;
    bringing the materials for the rest of this visit`, and the menu refreshes with the materials brought (Move).
    Nothing is lost.
15. Settings: choose **Move**, sit down: 3.8 behaviour. Choose **Off**: nothing. Back to Auto: craft from storage
    again, without a restart (the log says `Materials at the workbench: ... (..., setting changed)`).
16. An older INI with `bCraftingSupply = false` (and no `iCraftingSupply`): **Expected:** `Settings: bCraftingSupply=false
    (older INI) -> iCraftingSupply=1 (off)`; saving any setting writes `iCraftingSupply = 1` and removes
    `bCraftingSupply`.
17. With another crafting mod (install Crafting Pulls Resources From Containers, Nexus 135613, or Linked
    Crafting Storage): **Expected:** `Materials at the workbench: off (auto, game data loaded): Crafting Pulls
    Resources From Containers is loaded and already does this`, no `Crafting hooks: installed` line, and the
    Settings page shows *Auto (now: off)* with the same note. Sitting at the forge: Dova Stash does nothing, the
    other mod works alone. Then set `iCraftingSupply = 3` (your choice wins): `craft from storage (your setting,
    ...)`. Both mods now answer the menu: counts may include the containers twice. **Fail if** anything is lost
    or the game crashes; report what you see.
18. **Fail if** a count in a crafting menu ignores stored materials, a craft takes from storage what you carry
    yourself, a favorited, worn, tempered, enchanted or quest copy in a container is used, anything moves when
    sitting down or getting up (except at enchanters), an item disappears, or the alchemy list shows the same
    ingredient twice. Send the log.

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
   *Rename Grounds...* (both UIs, with UIExtensions installed: it needs its text box) and *Remove this area from
   Lakeview Manor...*.
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
   warning). With only non-persistent containers taking what you carry, nothing moves and the HUD reads *Dova
   Stash: nothing deposited (N items left: no container here takes them)* (*master not here* when the master
   is inside, *visit Cellar first* when a container they belong in stands in an area not visited this session;
   plan 21 3.9.1-unvisited, then 3.9.1-visit with every area visited), not *nothing to deposit* (manual for Tundra Homestead, plan 38's 3.9-tundra-b2, as its cellar has a
   master of its own). In an area with its own master (`cells[].master`: the Tundra Homestead cellar, the Myrwatch
   gallery) Deposit puts what has no container there in that chest (plan 38, 3.9-tundra-b: a potion into
   `ccEEJSSE001-Hstead.esm|0x00000F85`); the hotkey at nothing there is titled plainly, with no *(master not here)*
   (3.9-tundra-b3), and the hotkey at `0xF85` opens the master menu (3.9-tundra-b5). A sword stored in the cellar
   chest moves on when it is closed, and nothing is learned (3.9-tundra-b4). Retrieve → Ingots: they come from inside. Advanced... → Browse containers → a main hall chest: its container menu
   opens (unverified: report if it closes at once or shows nothing).
5. **Panel.** Outside, open the panel: header *Lakeview Manor in Grounds* (the name you gave it; the cell's own
   name, *BYOHHouse1Exterior*, without one); Actions → **Areas** lists *Lakeview Manor (main)* and *Grounds (you
   are here)* with *Rename* and *Remove*. *Rename* opens a box under the row with the current name; type
   *Yard*, Enter (or *Save*): status *Dova Stash: area renamed to "Yard"*, the row and the header follow, the
   file's `label` is *Yard*; an empty box brings the cell's own name back (the entry is written as a plain
   string again). Containers: rows inside show *(Lakeview Manor)* next to *available*, sacks show
   *unavailable: only reachable in Lakeview Manor* (the debug log says *(not a persistent reference)*; no warning).
   When the panel is opened in a cell that can join a home, the *Add this area to ...* button has a name box
   next to it (hint: the cell's own name); the name typed there becomes the label.
6. **Cellar with its own setup.** `coc BYOHHouse1FalkreathBasement` with a setup of its own (a master made there,
   or the retired *Lakeview Manor Cellar* template: plan 21 installs a copy as `Templates/AutoTest/LakeviewManorCellar.json`):
   the cellar stays its own home (header *Lakeview Manor, Cellar*, its own master). Add
   `"HearthFires.esm|0x00004849"` to the main hall's `cells` by hand: the cellar still stays its own home, and the
   main hall's panel says *&lt;cellar&gt; has its own setup; Reset it there to make it part of this home.* (the
   `cells` entry's label, else the cell's name, never a form ref). **Advanced...** → **Reset home...** in the cellar: on the
   next hotkey there it is part of Lakeview Manor. With a setup made by the old Cellar template and nothing
   changed, applying *Lakeview Manor* in the hall takes the cellar in (its `replaces` names the old file): log *Lakeview Manor: took in Cellar (untouched template setup)*, the cellar's file goes to a `.bak`, and the
   next hotkey in the cellar shows it as part of Lakeview Manor. Reset is only needed when the cellar's setup has
   changes of the player's. Automated in plan 21, test 3.9.6.
7. **Remove.** Outside, master menu → *Remove this area from Lakeview Manor...* → Yes: HUD *Dova Stash: Grounds
   is no longer part of Lakeview Manor*; the hotkey there says *not a player home (exterior cell not owned by
   you)* or offers the area again right after coming out of the door. `cells` is gone from the file (no empty
   list).
7a. **One home per cell.** With the grounds part of Lakeview Manor, set the cellar up as its own home (step 6)
   and walk from the cellar out into the grounds: no *Add this area to Lakeview Manor, Cellar* is offered there
   (the cell already belongs to Lakeview Manor; the hotkey opens Lakeview's master menu), and no template
   notice or auto-enabled template appears in the grounds.
7b. **Part of a home not set up.** With no setup in Tundra Homestead, `coc` into its cellar: the first visit says
   *Dova Stash: part of Tundra Homestead; set it up in its main room*. Press the hotkey: *Dova Stash: part of Tundra
   Homestead* offers only *Use template "Tundra Homestead"* (no separate setup); Cancel changes nothing
   (3.9-tundra-h). Choosing it sets up the whole home from there, and the cellar is an area of it (3.9-tundra-h2).
   When the main room already has a setup of its own, the hotkey in the cellar gives the notice instead.
7c. **Make master of the container's area.** In Heljarchen Hall's Smelter yard, aim the hotkey at the Grounds chest
   across the border: its menu offers *Make master of Grounds...* (the container's area, not yours); Yes makes it
   the Grounds' master, and the Smelter yard and the home's master stay. Plan 22 3.9-heljarchen-border.
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

### 3.10 The sell chest (DESIGN §8.8)

Breezehome set up (a master plus a chest bound to `weapons.*`), `iLogLevel = 0`, `[Selling]` at its defaults
(`iSellPricePercent = 100`, `iAutoSellHours = 0`). Pick a spare container (a sack or an end table) as the sell
chest. Find two clutter items' ids with `help "Tankard" 4` and `help "Silver Goblet" 4`.

1. **Mark it.** Hotkey at the spare container → *Make sell chest...*. **Expected:** a message box explains it
   (sold only on Sell now, after you confirm; stolen, improved, quest and kept items stay; nothing sold now).
   *Yes* → HUD *Dova Stash: Sack is now the sell chest (Clutter)*; the setup file has `"sell": true` and
   `"categories": ["misc.clutter"]` on it; the activate prompt reads *Sack (Sell: Clutter)*. Nothing was sold.
   Startup log once a sale is planned: `Sell chest: barter settings fBarterMin 2 fBarterMax 3.3 fBarterSellMax ...`
   (send the fBarterSellMax value back).
2. **Routing.** `player.additem <tankard> 5`, `player.additem <goblet> 2`, `player.additem 0001397E 2` (Iron
   Dagger), Deposit. **Expected:** the tankards and goblets go to the sell chest, the daggers to the weapons chest.
   Put one Iron Dagger into the sell chest by hand: HUD *Iron Dagger now goes to Sack. K: undo*; no category
   question is ever asked for the sell chest. **Sort all**: the dagger stays in the sell chest. **Retrieve >
   Everything**: nothing comes out of the sell chest. Sit at the forge: nothing comes from the sell chest.
3. **Never sold.** Into the sell chest by hand: a stolen item (take something from a shop while unseen), an
   improved weapon (grindstone), a potion you made, 10 lockpicks, a quest item if you have one (it will not go in:
   fine). Then hotkey at the sell chest. **Expected:** *Sell now (N gold)...* is in its menu (above Rename).
4. **Price.** Note the gold for the tankards and the dagger, then compare with a merchant (Belethor) for the same
   items, Speech unchanged and no Allure: the same per-item price (the vanilla formula: value / (3.3 - 1.3 ×
   Speech / 100), times Haggling and Fortify Barter). `player.setav speechcraft 100` → the sell chest's price
   rises to half the value. `player.addperk 000BE128` (Haggling) → 10% more.
5. **Sell now.** *Sell now (N gold)...* → message box *Sell 8 items for N gold?* with the items (*Silver Goblet
   x2, Tankard x5 +1 more*) and what stays (*13 stay: 1 made by you, 1 customized, 1 stolen, 10 kept by a keep
   rule*). *No*: goes back to the menu it came from, and nothing is sold (the same for Buy back's question;
   automated in plan 18, test 8.8-mb-sell-no). *Yes*: HUD *Dova Stash: sold 8 items for N gold*; your gold rose by N; the
   chest holds only what stays. Log: one `Sell chest: sold <item> xN for G gold (g each) from Sack [...]` per
   stack and `Sell chest (sell now): sold 8 items for N gold in Breezehome` at info.
6. **Buy back.** Hotkey at the sell chest (or the master) → *Buy back (N gold)...* → *Buy back 8 items for N
   gold?* → *Yes*: HUD *bought back 8 items for N gold*, the items are back in the sell chest, your gold is down
   by N. The entry is gone afterwards (one buy-back per sale). Drop your gold below N after a sale (`player.removeitem
   0000000F <n>`): *buying back takes N gold*, nothing changes. Load a save: no buy-back is offered.
7. **Changed meanwhile.** Panel → Actions → Sell chest shows *8 items for N gold: ...*. Click *Sell now
   (N gold)...*, then before *Yes* put another tankard into the sell chest via the hotkey's Open (or Deposit).
   **Expected:** *Yes* sells nothing: *the sell chest changed; sell again*. Click again: the new total.
8. **Panel.** Containers tab: the sell chest is tagged *[Sell]*, its page says what it is, *Stop selling from it*
   turns it off (HUD), *Make sell chest...* asks inline. Actions tab: *Sales this session* lists each sale (game
   time, chest, items, gold, *(bought back)*); each opens to its items.
9. **Timed selling.** Settings → *Sell by itself after* = 1 h. Put 3 tankards in the sell chest, close it.
   **Expected:** nothing sold yet (log at debug: `timed selling (a container closed): 1 kinds of item seen since
   day ...`). Wait 1 hour in the home (T): HUD *Dova Stash: sold 3 items for N gold*; the log says `(timed)`.
   Put 2 more in, wait 30 minutes: nothing; wait another hour: they go. Leave the home for a day and come back:
   what was there when you left is sold on entering. Load a save and wait 30 minutes: nothing (the period starts
   again after a load). Set it back to *off*: nothing sells by itself any more.
10. **Template switch.** With a sell chest of your own, switch to the home's template (panel *Use*): the sell
    chest is no longer one (the role comes from the template); its items stay in it.
11. **Fail if** anything is sold without a *Yes* (or timed selling off), anything leaves any other container, a
    stolen / improved / enchanted-by-you / renamed / quest / favorited item or a potion you made is sold, the gold
    differs from the confirmation, a buy-back gives back other items, or items appear on the floor next to the
    chest instead of disappearing (then `RemoveItem` without a target drops rather than removes: send the log).

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
   opens and the game pauses. The header shows *Breezehome* (its cell's form ref is a tooltip, not a line). The body shows
   *This home is not set up yet.*, a **Make Chest the master** button, and *Templates* (tooltip: which home),
   which lists *Breezehome v1.0.2 by Dova Stash (offered first)* with a **Use** button. *All templates*
   (collapsed) is at the bottom.
2. Click **Make Chest the master**. The view switches to the tabs (Actions, Containers, Learned,
   Keep rules, Template). The header now shows *Setup: Breezehome v1.0.0 by Player*,
   *Custom setup (not from a template)* and *Master: Chest*. The status line at the top, under the header, reads
   *Chest is now the master container* (no *Dova Stash:* prefix; red when an action failed).
3. Close the window with the title-bar **X**, the **Close** button, or Esc. The game unpauses, and the
   HUD shows any queued notifications.
4. Press K at nothing. The window opens on the **Actions** tab.
5. Press K at another container (for example the cupboard in the loft). The window opens on the
   **Containers** tab with that container selected. If it is not managed yet, it appears in the table
   as *not managed yet*.
6. Press K at a respawning container in a dungeon (`coc BleakFallsBarrow01`). No window opens. The HUD
   shows *can't use ...* (same as M2).
7. Outside a home: open the SKSE Menu Framework menu (its own hotkey) → **Dova Stash**. The section lists four
   pages: **This home**, **Displays**, **Settings**, **About**. **This home** shows *You are not in a player home
   (...)*, then *Templates* with *Go into a player home to use a template there or save its
   setup.*, then *All templates*, open (5.2 step 9).

### 5.2 Actions tab (steps 5 to 9b: Template tab, Templates)

1. Build the test inventory from 2.2. Click **Deposit**. The status line and, after you close the
   window, the HUD show *deposited N items into ...*. The **Retrieve** tree refreshes with counts.
2. **Retrieve**: *Everything (N)* plus one row per root that has items, such as *Weapons (5)*. A root
   with populated children expands (for example *Swords (3)*, *Daggers (2)*), and each row has a
   **Retrieve** button. Retrieve *Swords*: only swords come back, and the counts drop.
3. Click **Refresh counts** after moving items by hand. The counts match the containers.
4. **Sort all** behaves like the M2 menu action (3.4).
5. **Template → Templates** (Actions has only **Use or save a template**, which opens the Template
   tab): *Breezehome v1.0.2 by Dova Stash (offered first)* with a small **Use...**. The setup from 5.1 is your own (*Custom setup*), so it asks
   first: a yellow *Use the template "Breezehome"? It replaces your changes: ... Your setup is backed up first.
   No items are moved.* with **Yes**/**No** (the hotkey menus ask the same). **No** leaves things unchanged.
   **Yes** applies the template: the header shows *Based on template: Breezehome v1.0.2* (hover:
   *Templates/Vanilla/Breezehome.json@1.0.2*), the row shows *(in use)*, the button becomes **Re-apply**, and
   the status line reads *Breezehome is set up*.
6. **Switching templates** (DESIGN §6.2 step 4). Copy `Templates/Vanilla/Breezehome.json` to
   `Templates/Test/Breezehome.json`, set its `name` to *Test* and `"priority": -1`, give one container other
   categories and pick another master; restart (debug log level). Both rows are listed, *Breezehome* first
   with *(offered first)*, then *Test*. Note what each container and display holds. Right after step 5 the
   setup has no changes of your own, so **Use** on *Test* switches at once, with no question: the header
   says *Based on template: Test v1.0.1* (hover: *Templates/Test/Breezehome.json@1.0.1*) and *(in use)* moves to *Test*. Log:
   *Breezehome: template switched from Templates/Vanilla/Breezehome.json@1.0.2 to
   Templates/Test/Breezehome.json@1.0.1*. Nothing moved: every container and display holds what it held;
   learned items, keep rules and ignored containers are still there (Containers, Learned, Keep rules tabs).
   **Sort all** then follows *Test*. **Use** on *Breezehome* switches back the same way.
7. **Asks when you changed something.** Tick one more category on a container (Containers tab), back to
   the Template tab: the other template's button reads **Use...** and asks first. **Yes** switches; `Layouts/` has a new timestamped
   `.bak`, and **Saved setups** → *Older copies* has a new entry whose **Restore** brings back the setup with
   your extra category. **Re-apply** on the template in use, after no change of yours, does not ask. Leave the
   panel open a minute: no repeated log lines (the check runs only when the setup changes).
8. **Drafts come last.** Add an auto-draft for the cell: the editor's *Save N drafts...*, or a copy of the
   Breezehome template under `Templates/Breezehome/Unknown.json` with `"autoDraft": true` and
   `"priority": 10`. Restart. It is listed last with *(draft)* (hover: drafted by the editor for many homes
   at once), never *(offered first)*, priority 10 or not; its **Use** works like the others. Reset the home
   with `bAutoEnableTemplates = true` and walk in again: the template someone made is enabled, not the draft,
   and with `false` the notice and the hotkey's *Use template* name it. Remove `Templates/Test/` and the
   Vanilla Breezehome (or add a missing plugin to their `requires`) and restart: the draft is now
   *(offered first)*, the notice and the hotkey offer it, and a first visit enables it.
9. **All templates** (moved from the former Templates page, which showed the same). In Breezehome, the Template
   tab ends with *All templates*, collapsed (hover: where templates load from and when one is offered); open
   it: **Only installed homes** is ticked, and the catalogue's Status says *in use* / *fits this home* for
   Breezehome's rows, red *missing: ...* for homes whose mods are not loaded once you untick the box (ticked, it
   hides those rows); the auto-draft reads *(draft)*. Outside a home, **This home** shows *Templates* with
   *Go into a player home to use a template there or save its setup.*, then *All templates* open.
9b. **Remove from this home.** On *Test*'s row, **Remove...** asks *Remove Test from this home? ...*; **Yes**:
   the row moves to the end, dimmed, *(removed)*, with **Add back**; the setup file has `"hiddenTemplates":
   ["Templates/Test/Breezehome.json"]`; the hotkey's *Advanced...* no longer offers it;
   `Templates/Test/Breezehome.json` is still on disk; *All templates* says *removed from this home*. Remove the
   template in use: the header shows *Custom setup (not from a template)*, nothing moved. **Add back** (either
   place) offers it again. Automated in plan 11 test *5.2.11* (resolution, Remove and Add back through the
   harness's `hideTemplate`, the setup file, nothing moved).
10. **Reset home...** → **Yes**. The layout is backed up to `.json.bak` and the window returns to the
   setup view. No items are moved. The setup view shows *Saved setups* with a **Restore your last setup
   (date)** button (no question: there is nothing to replace); it brings the setup back.
11. **Saved setups** (above Reset; hover its header for where the copies are): **Restore your last setup (date)...** →
   *Restore the setup saved (date)? This home's current setup is backed up first (.bak); no items are
   moved.* **No** changes nothing; **Yes**: status line *Breezehome restored* (HUD: *Dova Stash: Breezehome restored*), and `Layouts/` has a
   new timestamped `.bak` of the setup that was there. *Older copies (N)* lists the history copies (one per
   earlier session, newest first), each with **Restore the setup from (date)...**. With a broken layout
   file (4.1) the setup view offers the same buttons above **Reset home...**.
12. **Only meaningful restores.** Right after a save (bind a category, close the panel, open it again; or
   after step 11's restore) the latest copy equals the setup, so **Saved setups** shows *The latest copy
   (date) is this setup as it is now.* instead of the **Restore last setup** button; *Older copies* stay.
   Change something (tick another category) and refresh: the button is back. The comparison ignores
   formatting: reformat `Backups/<file>.json` by hand (one line, spaces instead of tabs) and it still counts
   as the same. Nothing is compared per frame (no log lines, no hitch while the panel is open).

### 5.3 Containers tab

1. The table lists the master first (*[Master] Chest*, *(items without a destination)*), then every
   managed container with its categories, learned count and status. A container that is
   unavailable, such as unbuilt Hearthfire furniture, shows *unavailable: not built yet* in red (the log keeps
   *container is disabled (not built yet?)*). Hover the name to see the FormRef. A container with learned items only
   (2b in §3.5) shows *Books, learned* in grey under Categories; selected, its tree shows
   *[ ] Books (1 learned)* and a grey *Learned here: Books. ...* line.
2. Select the loft cupboard. In the details:
   - Type *Weapons* into the name box (empty, it shows the game's name) and click **Rename** or press
     Enter (hover **Rename**: *Saves the name. Empty: the game's name.*). The table shows *Weapons*, and the
     layout JSON has `"label": "Weapons"`.
   - **Categories sent here** shows a tree, each category by name (hover: *Id: weapons.\**). Expand *[ ] Weapons*
     and tick **All Weapons**.
     The node shows *[x]*, and every child shows *(via a wider choice)*. The JSON has `"weapons.*"`.
   - Untick it, then tick only *Swords*. The root shows *[~]*.
   - Tick **Everything** (hover: *Id: \**). It is added as `"*"`. Untick it.
   - Add `"weapons.swords.*"` by hand in the JSON and re-enter the cell. The pattern is listed under
     *Other patterns (unknown categories)* with a **Remove** button, which works.
3. **Ignore** → the status shows *ignored* and the category tree shows the ignored note.
   **Stop ignoring** (hover: *Lets it receive items and learn again, with its categories.*) restores it.
   **Remove...** → **Yes** takes the container out of the list.
   **Remove...** is not offered on the master or on a container that is not managed yet.
4. **Clear categories...** asks first (Yes / No); **Yes** empties the list (status line: *Categories cleared for ...*).
5. Hover **Make sell chest...** (*Makes it the sell chest: what you put in is sold when you choose Sell now.*) and
   **Make master...** (*Makes it the master: items without a destination go here.*). **Make master...** → **Yes**.
   The selected container becomes the master (header updates). The old
   master is no longer listed. No items are moved.
6. With learned entries pointing at a container (3.3), **Forget learned (n)...** → **Yes** removes
   them. The Learned column drops to 0.
7. An unavailable container (for example an unbuilt Hearthfire chest bound by a template) shows its
   reason in red, and its category tree is greyed out.
8. **Aimed container** (new). Look at the loft cupboard and open the framework menu → **This home** →
   **Containers**: its row's status ends in *(looking at it)*, e.g. *managed (looking at it)* (hover: *The container
   you are looking at.*), the table is scrolled to it and, with nothing selected, it is selected. A chest not in the
   setup yet is listed, *not managed yet (looking at it)*. Close the menu, look at another container, open it again: the mark moves and the table
   scrolls to it once (scrolling away by hand is not undone). Look at nothing: no row is marked. The hotkey's
   window marks the container it was opened at the same way.

### 5.4 Learned tab

1. Teach a destination by hand (3.3), then open the window. The tab lists *Item → Container*, sorted
   by item name. Hover a cell to see its FormRef.
2. **Forget** (the row button; it was *Remove*) drops one entry. The layout JSON no longer has it, and the next Distribute uses
   categories again for that item.
3. With nothing learned, the tab shows the explanation text.
4. **Stale entries.** Ignore a container with learned items (3.5): its rows read *<container>
   (ignored)* in yellow, and the tab says *N of these point at a container that is no longer used, so
   they do nothing.* with **Forget those (N)...** → **Yes** removes exactly those (HUD: *forgot N learned
   items*); the others stay. A learned entry for an unbuilt Hearthfire chest reads its reason in grey
   (*not built yet*) and is not counted or removed. In a multi-cell home, an entry for a container out
   of reach from this area is not marked.

### 5.5 Keep rules tab

1. A home without a `keep` block shows *This home uses the default keep rules only...*. The read-only
   *Default keep rules* list shows *Equipped*, *Favorited*, *Quest items* and the items *Gold*, *Lockpick* and
   *Torch* by name (each form ref in its tooltip), or your `Settings.json`.
2. Add `misc.clutter` under **Never keep** with the *Add category* picker, add `quest` with the text
   field and **Add**, and add a quota: **Add quota**, choose *Potions* (hover: *Id: alchemy.potions*), set the
   quota to 3 with the +/- buttons. *Unsaved changes* appears. **Save** is disabled while a quota has
   no category.
2a. The pickers next to the text field: **Add rule** (Equipped, Favorited, Quest items), **Add category** and
   **Add item** (the items you carry, with a *Search* box: type `iron` and only iron items stay; `zzz` shows *No item
   matches.*) each add their choice; each button has a tooltip. Type `quets` and click **Add**: a yellow *Not a keep
   selector.* and nothing is added.
3. **Save** → status *Keep rules saved*. The layout JSON has
   `"keep": { "inherit": true, "always": ["quest"], "never": ["misc.clutter"], "quotas": [ { "category": "alchemy.potions", "quota": 3 } ] }`
   (key order may differ).
4. Deposit with 5 Potions of Minor Healing and some clutter. 3 potions stay in the inventory and the
   clutter goes, even if a global rule would keep it.
5. Untick **Also apply the default keep rules** and save. Gold and lockpicks are no longer
   kept, apart from equipped, favorited and quest items, which are always kept.
6. **Revert** discards unsaved edits. **Use the defaults only...** → **Yes** removes the `keep`
   block.
7. A layout whose keep block came from JSON with extra keys or `"rules"` instead of `"quotas"` keeps
   them after saving (round-trip).

### 5.6 Template tab

0. The tab starts with *Templates* (5.2 steps 5 to 9b), then **Save as template**, then *All
   templates* (collapsed, 5.2 step 9).
1. The name defaults to the home's name; author and version are empty (hints *your character's name*
   and *automatic*); **Include where you put things by hand** is ticked (hover: the items go to
   the same containers for anyone using the template). Set the name to *My Breezehome*, then click
   **Save** (Enter in the name field saves too).
2. The status reads *Saved to Data/SKSE/Plugins/DovaStash/Templates/User/My Breezehome.json. This home
   offers it now. N learned items kept as fixed item bindings.* and the status line *Saved as template "My Breezehome"
   (incl. N learned items)*; the HUD says the same with the *Dova Stash:* prefix. Under MO2
   the file is in `overwrite/SKSE/Plugins/DovaStash/Templates/User/`. It has your character's name as
   author, version 1.0.0, no `learned` (its items are `forms` of their containers) and no `basedOn`,
   `"cell"` is the Breezehome cell, and
   `"requires"` lists every plugin the refs point into (at least `Skyrim.esm`, and the plugins of
   display refs, plaque items and mannequin sets). Any template lint (for example *no "master"
   container*) is listed in yellow.
3. Click **Save** again with the same name. The shared yellow question *Replace the template "My Breezehome"?
   Templates/User/My Breezehome.json (v1.0.0) is saved as v1.0.1; the old file is backed up.* appears with **Yes**
   and **No**. **No** writes nothing (check the file timestamp). **Yes** writes v1.0.1 and a timestamped `.bak` of
   the old file. Changing the name withdraws the question. A version typed into the field is used as it is.
4. A name with `/:*?` characters is written with `_` in their place (the grey line shows the file
   name). An empty name gives *Enter a template name.*
4b. Untick **Include where you put things by hand** and save under another name: no `forms` from learned items, and the
   HUD has no *incl. ... learned items*. The box is ticked again in another home.
5. No restart: *All templates* lists *My Breezehome*, and in Breezehome the
   **Template → Templates** section lists it as *(offered first)* (the player's own templates
   rank before the shipped ones of the same priority). The hotkey's **Save as template...** (§3.6) writes the same
   file.

### 5.7 The pages (SKSE Menu Framework menu → Dova Stash)

Four pages, in this order: **This home**, **Displays**, **Settings**, **About**. The former Armor sets,
Templates and Diagnostics pages are gone; rows 5.7.3 and 5.7.4 keep their ids and say where they moved.

| # | Page | Expected |
|---|---|---|
| 5.7.1 | **This home** | The same panel as the window, for the current cell. Actions work from here too. Outside a home: 5.1 step 7. |
| 5.7.2 | **Settings** | Every `DovaStash.ini` key, applied at once (§6.6), in sections: Hotkey (press-to-bind buttons: keyboard *K*, gamepad *Off* with Clear), Menus, Sorting, Learning, Crafting, Templates, Sell chest, QuickLoot IE, In the world, Log; then the *Default keep rules*, and last, under a *Reset* header, **Reset settings to defaults...** (keeps the hotkeys and the default keep rules). Hovering a setting shows what it does and its INI key, also while it is greyed out. The status line sits under the page's note: make `DovaStash.ini` read-only and change a setting: a red *Settings not saved (see DovaStash.log)*. |
| 5.7.3 | **This home → Template** (moved from the Templates page) | In a home: *Templates* (5.2 steps 5 to 9b) and *Save as template*; outside one, This home shows one dimmed line instead. Then *All templates* (collapsed in a home, open outside one): **Only installed homes** (ticked) and every loaded template with name and version (*(draft)* on an auto-draft), author, home name (hover: cell FormRef), requires, status (*in use*, *fits this home*, *ok*, red *missing: ...* / *conflicts: ...*, dimmed *removed from this home* with **Add back**) and an issue count (hover: the warnings). Untick the box: templates for DLC or CC homes you don't have show *missing*. |
| 5.7.4 | **About → Diagnostics** (moved from the Diagnostics page) | One line each for *Categories*, *Templates*, *Settings.json (default keep rules)*, *Homes that sort themselves (SelfSorting)* and, in a home with a setup, *This home's setup (&lt;home&gt;)*: green *No problems.*, or the counts with the problems under it, matching the `[warning]`/`[error]` lines in the log (each listed once), such as a container pattern that matches no category. Break a category file on purpose (`"formTypes": ["XXXX"]`) and restart: the error is listed in red. Put `"equiped"` in `Settings.json`'s `keep.always` and restart: *Settings.json (default keep rules)* warns *keep.always: category pattern "equiped" matches no known category; did you mean "equipped"?* (the same in the log; a layout's or template's keep block gets the same hint). |
| 5.7.5 | **About** | *Dova Stash vX*, *Auto-sorting loot storage for any player home. Look at a container in a player home and press the hotkey to open its menu.*, the hotkey (*Hotkey: K (gamepad: LB)* when a gamepad hotkey is set), *Dova Stash Editor: installed at ...\Tools\DovaStashEditor.exe* when the build deployed the editor next to the plugin (under MO2 the path may read as the game's `Data\Tools\...`; rename the exe and restart: *Dova Stash Editor: not installed*), the license; then *Diagnostics* (5.7.4); then *For bug reports*, collapsed: runtime, Menu Framework version, QuickLoot IE and UIExtensions, category and template counts, where the data, the log and the setup copies are. |
| 5.7.6 | **Displays** | See §3.7 Displays, step 8e; a mannequin's *Armor set* is step 8d (moved from the Armor sets page). In a home with more than 500 displays (or sets in the picker), the table says *Showing the first 500; search to narrow it down.* The status line sits at the top, under the header, and stays in view while the table scrolls. Filters that match nothing: *No display matches.*; a set search that finds nothing: *No set matches.*. |

### 5.8 Threading and robustness

| # | Steps | Expected |
|---|---|---|
| 5.8.1 | Keep the window open for a minute on each tab | No hitch. The log shows no repeated lines. A light refresh runs every ~2 s, and item counts are only recomputed on open, after actions, and on **Refresh counts**. |
| 5.8.2 | Click **Deposit**, **Sort all** and **Retrieve** several times quickly | Each runs once, in order. No crash, no duplicated or lost items (check the totals). |
| 5.8.3 | Open the window in Breezehome, Deposit, close it, travel to Honeyside, and open the framework menu → **This home** | It shows Honeyside, not Breezehome. Nothing from Breezehome (selection, keep draft, the status line) carries over: the status line belongs to the home it was set in and is cleared when the window opens. The header, status line and tabs stay in view; only the tab body scrolls. |
| 5.8.4 | Open the master by hand (ContainerMenu), close it, then press K | The window shows the distributed result. |
| 5.8.5 | Save and reload with the window closed | Nothing in the panel survives the reload except the data in the layout JSON. |

## 6. Borrowed-UI integration (M4b)

These features draw nothing of their own: what you see should look like the rest of your UI. Test with
your normal skin, then repeat 6.2 with UIExtensions' stock `listmenu.swf` (disable any UIExtensions
skin in MO2). Dova Stash must work the same with both.

### 6.1 Startup log

| # | Steps | Expected |
|---|---|---|
| 6.1.1 | Start the game, read `DovaStash.log` | Lines for: QuickLoot IE (`API V21, item destinations + info bar + button bar` with QuickLoot IE 4.1+), `UIExtensions: installed, list and text-entry menus available`, `Activate prompt: ... hooked`, `Overlay: HUDMenu::AdvanceMovie hooked`, `Icons: SkyUI item icons '...', category icons '...'`, and `UI: in-world menus are UIExtensions list menus`. |
| 6.1.2 | Disable QuickLoot IE, UIExtensions and I4 in MO2, start the game | The game starts and the log says each one is not installed. The hotkey opens native message boxes; the overlay uses SkyUI's default icons. |

### 6.2 UIExtensions lists (hotkey menus)

| # | Steps | Expected |
|---|---|---|
| 6.2.1 | In an enabled home, press K at nothing | A top-down list headed `-- <home name> --`: Deposit, Restock (when a quota restocks), Sort all, Retrieve..., Advanced..., Sell now / Buy back (with a sell chest), Rename \<master\>... / Restore original name, and in an added area Rename \<area\>... / Remove this area from \<home\>... It is drawn by your UIExtensions list menu skin. With `iMode = 1` the message box has the same entries in the same order (body *Dova Stash: \<home\>*). |
| 6.2.2 | Press K at a managed container | A list headed `-- <container name> --`: Categories... (with a summary), Open \<name\>, Sell now (N gold)... / Buy back (N gold)... (at a sell chest, after Open), Rename..., Learned items (N)..., Clear categories..., Ignore, Make sell chest... / Stop selling from it, Remove..., Make master..., Open panel... (Open panel... only with the SKSE Menu Framework; Learned items (N)..., Restore original name, Clear categories... and Remove... when they apply). Clear categories..., Make sell chest..., Remove... and Make master... ask first; No returns to this list. No Advanced... |
| 6.2.3 | Categories... -> pick a group -> toggle two categories | The menu stays open: each toggle flips its `[x]` in place, with the selection and scroll position where they were and no close/reopen flash. Debug log: `entry N chosen, list instance M stays open`, then `updated in place`. **< Back** returns to the groups, which show `[x]` / `[~]` / `[ ]`. Esc also goes back. |
| 6.2.4 | Retrieve... -> a group -> a category; then Retrieve... -> Everything with more than 50 items stored | The items move to you. Counts match what was stored. Everything asks first: *Retrieve N items? You may be over-encumbered. Deposit puts them back.*; No returns to Retrieve... (plan 17 6.2.4-lists-everything, plan 18 6.2.4-mb-everything) |
| 6.2.5 | Advanced... -> Browse containers... -> pick a container across the room | That container's own menu opens (SkyUI, with your I4 icons). Items you put in are learned like a normal visit. In a home with area masters each is listed right after the home's master, as *\<container\> - \<area\> master (N items)* (plan 38 6.2.5-tundra-browse). With `iMode = 1`, a list over 3 pages shows the masters, then one *\<area\> (N)...* line per area (a single-area home: per category root, *Other* without one), each opening its containers (plan 21 6.2.5-lakeview-browse). |
| 6.2.6 | Advanced... on the home's list; Open panel... on a container's list and the setup prompt | Advanced... opens a list `-- Advanced --` (message box: *Dova Stash: \<home\>: Advanced*): Browse containers..., Use / Re-apply template "\<name\>", Save as template..., Reset home..., and with the framework Open panel..., which opens the panel's This home page. Containers and the setup prompt have no Advanced...: their Open panel... (only with the framework) opens the panel on that container. |
| 6.2.7 | Open the list, press Esc; open it again right away | It reopens. No "close the open menu first" message. |
| 6.2.8 | Set `iMode = 1`, restart, press K | Native message boxes, as before: 5 options a page, then More..., Back and Cancel; the last page holds up to 6, so a lone 6th option never gets a page of its own. |
| 6.2.9 | Walk through every list: Categories... and a group, Learned items..., Advanced..., Retrieve... and a group, Browse containers..., a display and its Categories... | Each starts with a plain header naming it, then **< Back** on sub-lists: `-- Chest: Categories --`, `-- Chest: Weapons --`, `-- Chest: Learned items --`, `-- Advanced --`, `-- Retrieve --`, `-- Retrieve: Weapons --`, `-- Browse containers --`, `-- Weapon Rack --`, `-- Weapon Rack: Categories --`. Choosing a header does nothing (the menu stays open). |
| 6.2.10 | With "Open the lists in the middle of the screen" on (default), open lists of different lengths and the rename box | No jump: each menu appears already centred, never at the skin's own position first. With the debug log, one `centred (... -> ...) after N tries` line per menu. |
| 6.2.11 | Turn that setting off in the panel, press K | The list opens where the skin puts it, shown at once. |
| 6.2.12 | Press K at nothing, then at a container, then open a sub-list | The Dova Stash logo sits centred above every list, clear of the entries, in the same colour as the list text, and the whole menu (logo included) is centred. Debug log: `logo shown`. |
| 6.2.13 | Categories..., a group, Retrieve..., Browse containers..., a display's Categories... | Each category entry has its icon left of the text (your SkyUI/I4 icons, same as the floating icons and QuickLoot); Browse shows each container's icon; the container menu's Categories... shows the container's icon. Text lines up in one column; the header and `< Back` have a blank icon slot. The master menu's actions have no icons. |
| 6.2.14 | In a long list (more rows than fit), scroll down and up with the mouse wheel and the keys | Every row keeps the icon of its own entry (rows are reused while scrolling). No stutter. |
| 6.2.15 | Rename `Interface/DovaStash/logo.dds` in the mod folder, open a list | The list shows after at most ~0.4 s, with an empty space where the logo goes; after ~2 s one warning `did not load` in the log. Restore the file. |
| 6.2.16 | At a container with learned items: Learned items (N)... -> Forget all N learned items... (*Forget 1 learned item...* with one) | A message box asks *Forget N learned items for X? They go by category again. No items are moved.* (*1 learned item* for one); Yes forgets them and returns to the container's list, No returns to the learned list. |
| 6.2.17 | In a home over several areas, press K at nothing in an area without the master | Where no master takes the leftovers from there, the list is headed `-- <home> (master not here) --` (message box: *Dova Stash: \<home\> (master not here)*). In an area whose own master (`cells[].master`) is reachable it is headed plainly, and K at that area master opens this same master menu (no Ignore, no Make sell chest...). A container in an extra area offers *Make master of \<area\>...* instead of *Make master...*, naming the area the container is in, not the one you stand in (Heljarchen's Grounds chest seen from the Smelter yard: *Make master of Grounds...*; in a message box on page 2, More..., when it has categories); Yes sets that area's `cells[].master` and keeps the home's master (HUD: *Dova Stash: \<container\> is now the master of \<area\>*). In a cellar or gallery of a merged template with no setup anywhere, K offers *Use template "\<home\>"* (body *Dova Stash: part of \<home\>*): it sets up the whole home from there and the cellar becomes an area of it; Cancel changes nothing. Plan 38 3.9-tundra-b3, 3.9-tundra-b5, 3.9-tundra-i, 3.9-tundra-h and 3.9-tundra-h2; plan 22 3.9-heljarchen-border. |
| 6.2.18 | Press K at a container nothing is assigned to, in both UIs | The list's *Categories...* reads *(not managed yet)*; the message box body reads *Not managed yet (put items in by hand to teach it, or assign categories)*. |
| 6.2.19 | Leave another mod's custom menu open (or rename `listmenu.swf`), press K | HUD *Dova Stash: the list menu did not open (see DovaStash.log)*. |
| 6.2.20 | Give a container more than 128 learned items, open its *Learned items (N)...* | Paged: the first page ends with *More...*, the next starts with *< Back*, which returns to the first (plan 17 6.2-lists-pages, 130 items). |

### 6.3 Activate prompt

| # | Steps | Expected |
|---|---|---|
| 6.3.1 | Look at a container bound to Weapons and Armor (QuickLoot disabled) | The prompt reads e.g. *Chest (Weapons, Armor)*. The master reads *(Master)*. Unmanaged and ignored containers are unchanged. |
| 6.3.2 | Save, disable Dova Stash, load | Container names are vanilla again: nothing was written to the save. |

### 6.4 QuickLoot IE

| # | Steps | Expected |
|---|---|---|
| 6.4.1 | Look at a bound container | QuickLoot shows the container's category icons bottom-right (their names, without a prefix, when *Show category icons instead of names* is off); the master reads *Master*. The button bar shows **Stash** with your hotkey's glyph. |
| 6.4.2 | Scroll the list in the master | With *Show where each item goes* on, misplaced items read *Iron Sword  >  Weapon Rack* in the list. With it off, the info bar shows *> \<container\>* for the selected item. |
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
| 6.6.1 | Click the keyboard hotkey on the Settings page (it turns yellow, "Press a key..."), press L, close the menu, press L in a home | The Dova Stash menu opens; K does nothing. No restart. `DovaStash.ini` now has `iHotkey = 38`, comments intact. Pressing L while binding opens nothing. |
| 6.6.1a | Bind the gamepad hotkey to LB, then Clear it; start a binding and press Esc; start one and click the button again | LB shows "Gamepad LB" and `iGamepadHotkey = 274`, the window stays open; Clear sets Off (-1); Esc closes the window and nothing is bound; clicking again cancels; F1/LB open and close the window as before. Binding LB shows a yellow *Gamepad LB is also \<game control\> and the SKSE Menu Framework key.* (*Gamepad LB is also the SKSE Menu Framework key.* when no game control uses it; the framework's keys come from `SKSEMenuFramework.ini`). |
| 6.6.1b | Bind the keyboard hotkey to E | E is bound, and a yellow line says *E is also Activate.* Bind K again. |
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
| 6.7.4 | Put an Iron Sword by hand into a managed container that is not for weapons | Notification "Iron Sword now goes to <container>. K: undo". Hotkey at it (after the 5 s undo window) -> Learned items (1)... -> Forget Iron Sword removes it. |
| 6.7.5 | QuickLoot on a container holding items that belong elsewhere | Those items read "Iron Sword  >  Weapon Rack" in QuickLoot's list; items that belong there are unchanged. |
| 6.7.6 | Open the panel (hotkey -> Advanced... -> Open panel...), press Esc | The window closes. It always opens centred and fully on screen, with Close at the top. Floating icons hide while it is open. |
| 6.7.7 | QuickLoot on the master, then on a container bound to Weapons, Armor and Alchemy | The master shows "Master" bottom-right. The bound container shows three category icons (your SkyUI/I4 art) bottom-right in binding order, no text, clear of the weight readout; with more than 5 categories the rest show as "+N". The button reads "Stash". |
| 6.7.8 | SKSE Menu Framework -> Dova Stash -> Settings -> Default keep rules: Add category > Potions (hover: *Id: alchemy.potions*) to Always keep, Save | Settings.json is written; Deposit now keeps potions in every home. Hotkey -> Advanced... -> Open panel... -> Keep rules shows them under "Default keep rules". |
| 6.7.9 | Compare SKSE menu "This home" with hotkey -> Advanced... -> Open panel... | Same page, same tabs (Actions, Containers, Learned, Keep rules, Template). |


## 7. What to send back

- `DovaStash.log` from the session, at `iLogLevel = 1`.
- The layout file(s) from MO2 `overwrite/SKSE/Plugins/DovaStash/Layouts/` (for a restore problem, also
  the home's files in `Documents/My Games/Skyrim Special Edition/SKSE/DovaStash/Backups/`).
- Each failed step: the table row number, what you saw, and what was in the inventory or container before and after.

## 8. Dova Stash Editor (no game needed)

The editor's one-workspace window (docs/EDITOR.md §2, docs/EDITOR-UX.md). Build it with the `editor` preset
(docs/BUILDING.md), add it to MO2 (EDITOR.md §9) and run it from there; for repeatable runs use a state file of its
own: `$env:DOVASTASH_EDITOR_STATE = "$env:TEMP\ds-test-state.json"`. Every row also works from a terminal with
`--mo2 "<instance>" --gui` and the flags shown; add `--screenshot out.png` to save the window once it has loaded
(a `--screenshot` run keeps its state in memory unless `--state <file>` or `DOVASTASH_EDITOR_STATE` is given). In a
shell, type `DovaStashEditor` (the `DovaStashEditor.com` next to the exe): it waits for the editor and passes on its
output and exit code.

Unit tests, no window: `cmake --build --preset editor-release`, then `ctest --preset editor` (or `cmake --workflow
--preset editor` for configure, build and test). DovaStashEditorTests: the BSA, Assets and DDS readers, hand-built
NIF meshes (a textured BSTriShape, collision-only, a black card, alpha 0), and `state.json` saved with no `--state`
and read back (author, instance, home stage, no `.tmp` left). Golden test: `python -I editor/tests/editor_golden.py
--data <Skyrim Data>` (or set `DOVASTASH_SKYRIM_DATA`) compares the editor's `--cell` with `esm_dump.py cell` on
every shipped template's cell; a cell whose plugin is not in `--data` (the `Templates/Mods` homes under MO2) prints
`skip <cell>: <plugin> not in --data` and is not counted. Exit 0 when every checked cell agrees, 1 on any
difference.

| # | Steps | Expected |
|---|---|---|
| 8.1 | Start with no arguments (no state file) | Start panel in the middle: the setup in one line, "N player homes, M with templates", three cards. The rail on the left lists the homes by mod; *Game Data folder* holds the base game and DLC homes; *Rescan* and the scan summary at its bottom. Status row: `N homes`, `Ctrl+K commands`. |
| 8.2 | Rail search: `92177`, then `Whitepeak`, then `Eli_Druid Home.esp` | Each finds Whitepeak Tower or Aldamar under its mod (Nexus id muted). The filter (funnel) with *Only homes without a template* on shows the count of filters on and hides homes that have one. |
| 8.3 | Click *Whitepeak Tower* (or `--home "Whitepeak Tower"`) | The tower in 3D from above; `archives` / `meshes` / `textures n/m` progress in the status row, not over the view. Title row: the home's name, chip *shipped* / *yours* / *no template*. Drawer tabs with counts. |
| 8.4 | Press 2, then 1 (or `--tab plan`) | Plan, then 3D; the selection is the same in both. The floating toolbar stays: *Select*, *Paint*, *Layers*, *Colours*, *Camera*. |
| 8.5 | Click a chest; Ctrl+click two more; drag a box | Selection panel: its mesh picture, the reference block, *Role*. With three: the bulk editor. A box selects the containers and displays inside. Esc clears. |
| 8.6 | *Make master*, add a category with *+ add*, type a label | The title chip reads *draft*, a dot follows the name (title row, status row). Ctrl+Z / Ctrl+Y undo and redo each step. |
| 8.7 | Press P, *Pick another* ▾ `alchemy.potions`, click two satchels, press S | The paint bar replaces Layers/Colours/Camera; colours switch to by category; S returns to Select. |
| 8.8 | L; untick *Structure*; *Height cut* > *Only between* | The Layers popover; walls go; one floor left. |
| 8.9 | Ctrl+2 (or `--tab containers`), then *Doors*, double-click the exterior row, *Back* | The drawer opens on Containers; Doors lists the cells behind the doors (a click selects the door); the exterior opens with its terrain, the template comes along; *Back* (title row) returns. |
| 8.10 | Drawer *Where items go* (or `--tab preview`), V, click a chest | The table per container; containers coloured by fill in the view above; the Selection panel lists the chest's items (*Where its items go*). R runs it again. |
| 8.11 | *Draft* panel of a home without a template (or `--home Aldamar --inspector draft`) | Accept all / Reject the weak ones / Draft again; the master with its reason; chips per container (green, yellow, orange). A click on a chip adds or removes the category in the Selection panel and the preview at once; Ctrl+Z takes it back. |
| 8.12 | Ctrl+D (or `--drafts`) | Progress `Drafting all homes n/m` in the status row; then a *draft* chip on each home and the *Save N drafts...* banner over the rail. The start panel's *In progress* does not gain every home. |
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
| 8.23 | Drawer *Containers* (or `--tab containers --sort containers:role --filter chest --quick Ignored`): click *Kind*, Shift+click *Height (m)*, click *Role* until its arrow goes; type `sack` in *Filter rows*, Esc; *Kind* ▾ Barrel; chips *Ignored*, *Respawning*; Ctrl+click two rows, double-click one; the same on *Displays*, *Doors*, *Where items go* | Rows sort by kind, then height; the third click brings the table's order back; *ignored* / *not listed* / *no categories* sort after the categories, the master first. The box and chips hide rows ("N of M" beside them; Esc clears the box); selection, Ctrl+click and double-click (frame; on Doors: open the cell) work on the shown rows. Switching tabs and homes keeps each table's sort and filter until the editor closes. |
| 8.24 | A big home on the plan (`--home "Lakeview Manor" --tab plan --popover layers`, also Heljarchen Hall, Whitepeak Tower): L, tick *Effects*, then *Other*; untick *Structure*, *Furniture*, *People*, *Disabled* one by one; hover and click where a hidden footprint was; Home. Then press 1 and untick *Effects* (or `--layers effects,other`, `--layers -structure`) | The plan opens with *Effects* and *Other* off: no large faint boxes of fog, mist, light beams, Hearthfire's crafting triggers or `defaultBlankTrigger`. *Effects* adds faint blue outlines (dust beams, lights), *Other* grey ones (triggers, markers, room bounds); each untick removes its footprints at once, and nothing hidden shows a tooltip or gets selected (a chest under a trigger is picked first). Before you pan or zoom, each change re-fits the plan to what is shown, never to a fog bank or a trigger. In 3D the *Effects* tick is the 3D view's own (on): unticking it hides the glows and the dust beams; the plan's stays as it was. |
| 8.26 | Vlindrel Hall, select the *Weapon Plaque* (`--home Vlindrel --select "Skyrim.esm|0x00102D37" --popover categories`), tick *Use this display*, open *+ add*; then add `soulgems.*` by typing it + Enter | The picker lists only *weapons.\** and the weapon categories (crossbows included: the editor can't see the crossbow spot); no armor, soul gems, potions. The typed pattern shows as a warning chip (hover: *Soul gems can't go on a weapon rack*) and Checks has `display Skyrim.esm\|0x00102D37 (Weapon Plaque): Soul gems can't go on a weapon rack; nothing goes on it`, which selects the plaque. A mannequin offers armor but no shields, a bookshelf books only. |
| 8.25 | `--draft-all --save-to <scratch> --author Tester` twice, with a hand-made `Templates/<Home>/Tester.json` (no `autoDraft`) for one home and another author's file for a second home already in `<scratch>`; then save one auto-draft as a person (`--cell <its cell> --draft --save-to <scratch>`, or open it in the window and Ctrl+S) and run it once more; in the window, `--drafts --popover savedrafts` | The new files start with `"autoDraft": true`; both hand-made files are unchanged (same hash, no `.bak`) and each has a *not saved: ... is a template someone made* line; two homes of the same name: the second says *another home's auto-draft*; the second run says *refreshed the earlier auto-draft* (`.bak`, version raised); the person's save has no mark and the last run leaves it alone. The dialog counts new and refreshed files and lists the homes left alone; a home whose only template is an auto-draft shows the *auto-draft* chip. |
| 8.27 | `--home "Whitepeak Tower" --text-size 150 --screenshot out.png` | The window's text and controls at 150 % for that run only (the remembered *UI size* is unchanged), nothing clipped in the screenshot. |
| 8.28 | A home with missing wall or clutter meshes: look at the status row, click *N missing*, then right-click it | Missing-mesh boxes are off at first (`--layers missing` turns them on for one run); the click shows them as see-through boxes that clicks pass through; the right-click copies the list. |

## 9. Automated in-game tests

Most of sections 1-4 runs unattended: `tools/autotest/run.ps1` starts Skyrim in its own MO2 profile, a test
harness inside `DovaStash.dll` plays the steps (no input needed) and checks the result, and the game quits by
itself. A run of every plan takes about 40 minutes (about a minute per plan), a few minutes per plan with `-Only`. Nothing is saved, and the player's profiles and saves are not touched. The player's setups in overwrite are set aside during a run and put back by the runner, by its watchdog (`run.ps1 -Finish` in a hidden process outside the shell's process tree) if the runner is killed, or by `pwsh -File tools\autotest\run.ps1 -Finish [-WaitMinutes n]` by hand.

### 9.1 Setup (once)

```powershell
pwsh -File tests\ingame\install.ps1 -Profile      # MO2 profile "Dova Stash AutoTest" + mod "Dova Stash AutoTest"
```

The profile (`profiles\Dova Stash AutoTest`) has the base game, the DLCs and Creation Club as installed, the
Unofficial Patches (USSEP, USCCCP), SKSE, Address Library, SKSE Menu Framework, UIExtensions, QuickLoot IE (with
SkyUI, which its loot menu needs: without it QuickLoot IE stops the game at the main menu with a dialog), KID,
Dova Stash and *Dova Stash AutoTest*, plus the home mods listed in `tests/ingame/profile/extra-mods.txt` (one
`<MO2 mod folder>|<plugin.esp>` per line: the mod-added homes the plans in `4N_*`/`5N_*` test; `-ExtraMods <file>` names
another list, e.g. `extra-mods-wynter.txt` for Wynter's Breezehome Redux, which rebuilds Breezehome and so gets a profile
of its own); nothing else. Profile-local INIs (`tests/ingame/profile`): windowed 1280x720,
low settings, no intro (`sIntroSequence=`), runs without focus (`bAlwaysActive=1`), no gamepad, no autosaves, sound
off, Papyrus logging on (`bEnableLogging`, `bEnableTrace`). Crash Logger SSE AE VR (PDB support) is enabled
when installed (`install.ps1` warns when it is missing): a crash then leaves a `crash-*.log` the runner copies
into the run folder. The *Dova Stash AutoTest* mod holds the plans
(`SKSE/Plugins/DovaStash/AutoTest/*.json`) and a `DovaStash.ini` of its own (`iLogLevel = 0`,
`bAutoEnableTemplates = false`, `iMode = 1`), so a run never changes the player's INI. Run `install.ps1` again
(with `-Profile`) after changing the plans or the profile files; `run.ps1` re-installs the plans by itself.

### 9.2 Running

```powershell
pwsh -File tools\autotest\run.ps1                    # the Dova Stash mod's DLL
pwsh -File tools\autotest\run.ps1 -Build             # build first (deploys as configured, docs/BUILDING.md)
pwsh -File tools\autotest\run.ps1 -Dll build\plugin\src\plugin\Release\DovaStash.dll   # a DLL of your own
pwsh -File tools\autotest\run.ps1 -Only '^(2|3)\d_'      # only the plans whose file name matches
pwsh -File tools\autotest\run.ps1 -WaitMinutes 30          # wait for a game to end (leftovers are set aside)
pwsh -File tools\autotest\run.ps1 -NoQuarantine             # refuse to start while leftovers are in overwrite
pwsh -File tools\autotest\run.ps1 -Finish -WaitMinutes 30   # clean up after a runner that was killed; starts no run
pwsh -File tools\autotest\run.ps1 -ListAll                  # the summary lists every test, passed ones too
pwsh -File tools\autotest\run.ps1 -StallMinutes 15          # a hung game is ended after 15 min without a write (default 10)
```

`Get-Help tools\autotest\run.ps1 -Parameter *` documents each parameter.

`-Only <regex>` installs only the matching plans (and `_run.json`), so a run of many homes can be split to stay under
the run timeout (`_run.json` `timeoutMinutes`, `-TimeoutMinutes`). `-Mo2` defaults to `$env:DOVASTASH_MO2`, else the
developer's instance. The runner and `install.ps1` need pwsh 7 (`#Requires -Version 7`).

The game needs the desktop session. From session 0 (a service, an agent shell), or a shell that is not
UserInteractive, `run.ps1` hands the run to the logged-on user's desktop session: a one-shot scheduled task
*DovaStash AutoTest &lt;yyyyMMdd-HHmmss&gt;* (Interactive logon, Limited, hidden) with the same arguments plus
`-NoHandoff`. The task starts the runner through WMI `Win32_Process.Create`, which escapes the task's job object
(inside it MO2 cannot start `skse64_loader`: ERROR_ACCESS_DENIED). Its output streams back to this shell, which exits
with its code; the files are in `%TEMP%\DovaStashAutoTest\handoff-<stamp>\` (`output.txt`, `exitcode.txt`; only the
newest 5 handoff folders are kept, older ones are removed when a handoff starts). The wait
is bounded by the watchdog limit + `-WaitMinutes` + 30 min (past it the task is stopped, exit 1). Exit 2 only when
nobody is logged on to the desktop, the task cannot be made, or `-NoHandoff` is passed (*run.ps1 needs the desktop
session (this shell is in session N): start it from a terminal on the desktop, or without -NoHandoff*). A leftover
Steam, or Steam only in another session, is refused (exit 2) before the run changes anything: no run folder, no
`summary.txt`, no quarantine of overwrite files, no profile save, no watchdog. After a run it force-ends the MO2 it started if that MO2
is still up 5 minutes later, and any Steam whose parent is the game it started; `-Finish` does the same. Every run
that made its folder writes `summary.txt`, a stopped one with the reason (exit 3: *NOT started: Skyrim did not start
within 3 minutes, attempt N*; exit 4: no results file). Logs not written since the run started are not copied;
`summary.txt` names them instead (*DovaStash.log not written by this run (last write ...)*).

One run at a time on the machine: `run.ps1` holds `%TEMP%\DovaStashAutoTest\.lock` and counts a running
`SkyrimSE.exe`, or an AutoTest MO2 a live run owns, as busy (exit 2; `-WaitMinutes <n>` checks every 60 s instead). An
AutoTest MO2 (`--multiple -p "Dova Stash AutoTest"`) with no `running.json` or a dead runner, or a `steam://run/489830`
Steam whose game is gone, was left by an earlier run: the runner ends it, or stops (exit 2) with the `Stop-Process`
command. A stopped run with no results still gets `summary.txt` with the reason. Files an earlier run left in MO2's `overwrite\SKSE\Plugins\DovaStash` are moved into that run's
`written\` folder (or `<run>\earlier\` when the run is unknown), listed, and never put back. The runner knows them
by the run's game window (the run folders' `results.json` and `%TEMP%\DovaStashAutoTest\runs.json`, which every
run appends to) or by AutoTest markers (`Templates\User\AutoTest*`, a layout named *... (AutoTest)*). The
player's own `Layouts\*` and `Backups\*` files go to `<run>\preexisting` for the run and come back afterwards;
`-NoQuarantine` refuses to start instead (exit 2, one-line list; `-Quarantine` is still accepted, the default).
`-Build` builds with the `plugin-release` preset (configuring `build/plugin` with the `plugin` preset when it has no
`CMakeCache.txt`), prints where that build deploys, and stops if `VCPKG_ROOT` is unset and `E:\vcpkg` is missing.
The player's MO2 profile is remembered in `%TEMP%\DovaStashAutoTest\player-profile.txt` and put back after the run's
MO2 exits (it waits up to 5 min). When `ModOrganizer.ini` already names the AutoTest profile and no run saved the
player's, it puts back `-PlayerProfile <name>`, else "Dova Stash Testing" if it exists, else the profile whose
`modlist.txt` changed last, and says *(guessed; -PlayerProfile sets it)*. `player-profile.txt` marks a guessed
profile with a second line `guessed`, and every restore says *(guessed; -PlayerProfile sets it)* until a run sees
another `selected_profile` or gets `-PlayerProfile`. Plans left out because the profile lacks a plugin they need (the
Wynter plans 60/61) run right after the main run in `<ProfileName> - <suffix>` (`Dova Stash AutoTest - wynter` for
`extra-mods-wynter.txt`) when that profile exists; a failure there fails the run too. Make it once with
`pwsh -File tests\ingame\install.ps1 -Profile -ProfileName "Dova Stash AutoTest - wynter" -ExtraMods tests\ingame\profile\extra-mods-wynter.txt`.
Without it the summary lists them as `[blocked]` with that command, and they fail the run (exit 1) unless
`-AllowNotRun` is passed. A missing MO2 instance, a missing profile or no `DovaStash.dll` is
exit 2 with what to do (`install.ps1` stops on a missing instance too). It starts Steam
first when it is not running (the game needs it; MO2 would otherwise ask) and polls for it for up to 2 minutes.
It prints `<n> plan(s): about <n> min; timeout <t> min`, then launches
`ModOrganizer.exe --multiple -p "Dova Stash AutoTest" "moshortcut://:Launch Game"` (a second MO2 process: an MO2
window that is open stays as it is; the runner puts `selected_profile` in `ModOrganizer.ini` back afterwards),
waits for the results, ends only the game it started on a timeout (default: the installed `_run.json`
`timeoutMinutes` plus 10), a stall (no write to `results.json` or `DovaStash.log` for `-StallMinutes`, default 10, or
not responding for 2 min) or a dialog nobody can
answer (its text goes to `dialog.txt`). A stopped run's summary header reads *NOT finished: stalled &lt;n&gt; min after
&lt;test id&gt; (game not responding)* or *NOT finished: timeout &lt;t&gt; min*, and the reason is saved to `<run>\stopped.txt`.
A game that exits by itself mid-run gets the reason *the game exited by itself after &lt;test id&gt; (exit code N;
crash log: &lt;name&gt;|none; Windows event: &lt;faulting module&gt;|none[; dump: SkyrimSE.exe.&lt;pid&gt;.dmp])*, in
`stopped.txt` and `summary.txt`. Then, once per run, the plans with no result yet run again in a run of their own
(a new folder, `-Only` those plans, `-NoResume`), and the combined `all:` line adds its counts; `-NoResume` turns
that second run off. A header of a run that did not finish ends *(results last said: &lt;state&gt;)*. It starts the game again (up to three times) if it ended before the tests
began (MO2 sometimes drops its virtual file system when `skse64_loader` exits before the game is up). `-Focus` brings
the game window to the front once (not needed so far). Everything goes to `build/autotest/<date-time>/`: `results.json`, `DovaStash.log`, `Papyrus.0.log`, the
run's home backups and, under `written/`, the files the run created in MO2's `overwrite` folder (layouts,
exported templates; moved out, so other profiles never see them). It prints only the tests that did not pass (fail,
xfail, xpass, skip, manual, with their notes and failed checks), the blocked plans and the counts; `-ListAll` prints
every test. The full summary, every test included, is in `<run folder>\summary.txt`; its first line is `Plans: all`
or `Plans: -Only '<regex>'`. When plans ran in an extra
profile, the last line sums both runs: `all: pass N, fail N, xpass N, blocked N, skip N, manual N; exit <code>`. Read
`summary.txt` or that last line, not the child run's tail. It exits 1 when a test failed, a test passed that expected
to fail (xpass), a plan was blocked or the run did not finish; xfail alone does not fail the run. It keeps the newest
20 run folders under `build\autotest`, every run younger than 48 h and the newest full run (`Plans: all`); an older
one is deleted unless its `preexisting\` still holds files.

Collecting and restoring the profile run in a `finally` block, so Ctrl+C or an error still does them. A run killed
outright leaves `%TEMP%\DovaStashAutoTest\running.json`. Every run starts a watchdog (`run.ps1 -Finish` in a hidden
process outside the shell's process tree) that, once the runner is gone, waits for the game the run started, ends it
at the run's deadline or on the same stall rule (not only at `-WaitMinutes`), and puts the player's setups and profile
back. A run finished by the watchdog or by the next run gets `summary.txt` and the counts line too. `run.ps1 -Finish [-WaitMinutes n]` does the
same by hand: it starts no run, ends only the game the stopped run started, and exits 0 when nothing is left to
finish, 2 while that run's runner is still at work. Failing both, the next run finishes the collect first (`written\`,
quarantined files, `selected_profile`). A saved `selected_profile` equal to the AutoTest profile is never put back
(a warning says so).

### 9.3 How it works

`src/plugin/AutoTest/` (AutoTest.h has the design). Inert in normal play: without a plan in
`Data/SKSE/Plugins/DovaStash/AutoTest/` it does one directory check at startup and nothing else. With plans:

- **Isolation.** Home backups go to `SKSE/DovaStash/AutoTest/Backups` instead of the player's `Backups`; a plan
  whose home already has a layout file (the player's own, which MO2's overwrite folder shows to every profile) is
  refused (its tests are `blocked`, with that reason; the summary counts them), so a run only ever changes setups
  it made itself.
- **Start.** At the main menu it waits a few seconds, then `coc <home>` (the console command, through `RE::Script`)
  starts a game in the plan's home. Plans run one after the other in file-name order.
- **Drive.** Steps call the Controller as the menus do (Make master, Use template, Deposit, Sort all, Retrieve,
  Restock, Sell now's confirmed sale, Buy back, Use this rack, Save as template, Add/Rename/Remove area...), and the
  game as a player would: `player.additem`-like adds (tempered, renamed and stolen copies are placed in the world
  and picked up, so the game builds those stacks), equipping, opening a container's menu and putting items in by
  hand (Learn), the hotkey and choosing an entry of its menu (`choose`), a mannequin's armor set (`chooseArmorSet`), walking through a load door, sitting at a
  crafting station (`sit` stands the player at it and waits for both 3Ds; a menu that is already open or opening,
  as when that move seated the player, is only waited for: activating the station again got the player up and closed
  it, which preceded the silent game exits of runs 20261028-011117 and 021524. Else it activates the station, then the
  console's way, then stands the player at it again and keeps trying, 2-6 s after each move (a late *got up* closed a
  just-opened menu), until the menu opens or `timeout`; with `skipIfClosed`, a station the plan's own `enable`
  turned on that never opens a menu is skipped with its reason, plans 21-23) and
  consuming a material as that menu does (`PlayerCharacter::RemoveItem`, where the craft-from-storage hook sits).
- **Wait.** Steps run in SKSE tasks every 100 ms; a step that needs the game (a cell to load, a menu to open, a
  display to fill) waits for it; after a move the next step waits half a second, so the container-changed events
  are handled first, as between two things a player does.
- **Assert.** Checks read inventories (counts per item, also only tempered / renamed / stolen / worn copies),
  gold, the layout file (JSON pointer), exported templates and backups, the log since the test began (the HUD
  lines are in it as `Notify: ...`), `[error]` lines, the crafting session, the item count a crafting menu sees,
  open menus, the entries of the open UIExtensions list or the last message box's buttons (`menuEntries`), the
  panel's view of a template or an item (`panel`); "no item lost or gained" compares the player, every container of
  the cell, its mannequins and the weapons and shields within a rack trigger's bounds (what racks hold) with a
  snapshot. Other loose items in the world are left out (NPCs take or eat them). Plan
  `17_HotkeyLists` runs the hotkey menus with `iMode = 0`.
- **Message boxes nobody answers.** A box the game or another mod shows by itself blocks every other menu (no
  container or crafting menu opens behind it). `_run.json` lists the ones to answer (`messageBoxes`: text to look
  for, button): the SkyUI build's *MCM-Unlocked.dll* warning and the Survival Mode question (No). Every box is
  logged (`AutoTest: a message box is up: ...`); Dova Stash's own (the category question) are answered by the
  plan (`answerMessageBox`, as a click).
- **Threads.** SKSE tasks run on several worker threads; the crafting menu counts and consumes materials on
  another one (the log names them: `AutoTest: Crafting Menu opened (event on thread ...)`). The crafting steps
  (`hookedCount`, `consume`, `craft`) run in UI tasks, as the menu does. `hookedCount` is the player's count plus
  what the count hook adds on that thread (`hooks::crafting::Added`; the hooked engine function itself is not
  called by the harness); `craft` is the real menu path: it picks a recipe in the open recipe menu and calls the
  menu's own `ChooseItem` and `CraftButtonPress` callbacks (what its Scaleform calls when the player picks and
  crafts), so the menu checks its hooked
  counts, consumes through `PlayerCharacter::RemoveItem` and adds the result.
- **Left-open menus.** A test that ends with a message box still open (Dova Stash's own hotkey menu in `iMode = 1`,
  a template offer, a category question) fails with *left a message box open*: such a box blocks every container and
  crafting menu of the tests and plans that follow, and `closeMenu` cannot close one (answer it with
  `answerMessageBox`, or avoid the hotkey when a check on the log or the layout file does). After failing that test
  the harness closes the box with its last button (*Cancel* / *No*), so the tests and plans after it do not fail too
  (run 20261028-044838 lost 33 tests to one box). `store`/`take` wait out the
  short grace after Dova Stash's own moves (`ItemMover::IsSuppressing`), during which container events are ignored:
  the harness is faster than a player there. `snapshot`/`conserved` count the home's containers in its other cells
  too (DESIGN §6.7), not only the player's cell; containers that leave the layout (a removed area) leave the comparison.
- **Home scope.** Home-scoped actions (`reset`, `useTemplate`, `hideTemplate`, `displayUse`, `op`, `bind` ...) fail
  with *the player is in &lt;home&gt;, not the plan's home (enter it first)* unless the player's home is the plan's
  home, has it as an area, or is a cell this plan's own `enter` steps reached.
- **Expected failures.** A test with `"expectFail": "<reason>"` that fails is `xfail` (note: the reason); one that
  passes is `xpass` and counts as a failure. `results.json`'s summary has `xfail` and `xpass`. No plan uses it now
  (plan 53, 3.4-wuthrein, did until the Wuth Rein template 1.0.2; §9.5).
- **Not run.** A failed action ends the test with *N step(s) not run after a failed action*, its detail naming the
  next step (`next: ...`).
- **Report.** `SKSE/DovaStash/AutoTest/results.json` after every test, then `qqq`.

### 9.4 Plans

`tests/ingame/mod/SKSE/Plugins/DovaStash/AutoTest/`: `_run.json` (start delay, run timeout, quit), then one file
per home. Plans run on the shipped templates; test templates are declared by plan 10 (`NotInstalledHome.json`,
checked as `installed: false` in the catalogue), 19 (`BreezehomeMayNotFit.json`), 21 (`LakeviewManorCellar.json`)
and 47 (`WhitepeakTowerKexodus.json`). At each plan start the harness also removes the `Templates/User/AutoTest *.json`
files that earlier plans saved. A plan may declare test templates (`"templates": ["LakeviewManor.json"]`, files under
`tests/ingame/mod/SKSE/Plugins/DovaStash/Templates/AutoTest/`, typically `"priority": 10` so *Use template* picks them
over the shipped one). At each plan start the plugin loads only the `Templates/AutoTest/*.json` files that plan
names in `"templates"` (`Store::ScopeTestTemplates`), so the other plans see the shipped ones; `install.ps1` leaves
out templates no installed plan declares and warns about a test template no plan in the repository declares. `install.ps1` mirrors `Templates/AutoTest` exactly: a deployed test template no longer in the repository is deleted
and logged, and one no installed plan declares is left out. Plans 21-23 and 38 use the merged shipped templates
(plan 21 also declares `LakeviewManorCellar.json`, a copy of the retired cellar template, for 3.9.6; plans 21, in 3.9.7
and 3.9.8, and 22 add `BYOHHouse1Exterior02` (`0x9BD6`) and `BYOHHouse3Exterior02` (`0x90A5`) by hand, so before those
cells, the tower-roof chests and satchel or the Lakeview apiary, go into the shipped templates these tests need another
hand-added area or a remove-then-add flow), 24 and 25 check that the shipped
one is chosen and applied, and plans 41-46 and 51-57 use the shipped `Templates/Mods` files; plans 43-46 check display fills automatically (only
the `-look` placement entries are manual). Shipped-template checks do not pin a version (`/basedOn` contains `"<source>@"`, template log
lines use a version regex). A plan for a
mod's version of a home names it: `"requires": ["WynterBreezehomeRedux.esp"]` makes its tests `skip` (*needs X*) in a
load order without it, and `install.ps1` does not install it when the profile's `plugins.txt` does not enable it. A
plan:

```json
{
  "$schema": "dovastash-autotest-1",
  "name": "Breezehome",
  "home": "Skyrim.esm|0x000165A8",
  "coc": "WhiterunBreezehome",
  "tests": [
    {"id": "2.3", "title": "Deposit", "steps": [
      {"do": "op", "op": "Deposit"},
      {"check": "count", "in": "master", "item": "Skyrim.esm|0x00012EB7", "eq": 2},
      {"check": "notify", "regex": "Dova Stash: deposited \\d+ items? into 1 container"}
    ]},
    {"id": "2.5", "title": "Container-menu guard", "manual": "needs real key presses"}
  ]
}
```

`id` is the step of this document. A test passes when all its checks pass; an action that fails ends the test;
`require` skips it (furniture the game does not have; `"plugin"`: a plugin that is not loaded). Holders: `"player"`, `"master"`, a FormRef.

| Actions (`do`) | |
|---|---|
| `enter` (`coc`, `cell`, `any`) | coc to the plan's home (or another cell) and wait until it is loaded |
| `aim` (`ref`: the crosshair reports it; `null`: nothing, and no display in view either (DESIGN §10.3); `"clear": true` gives the crosshair back to the game; cleared at each test start) | what the player looks at |
| `activate` (`ref`) | the player activates it (an item plaque hands back what it shows; a door, a lever) |
| `clearInventory`, `additem` (`extra`: `health`, `name`, `owner`), `additemMany`, `removeitem`, `equip` | the inventory |
| `buyHome`, `enable` (the whole enable-parent chain; waits for the 3D attached to the cell), `require`, `ownCell` | what buying a home and its decorations does; skip when something is missing |
| `makeMaster`, `useTemplate`, `hideTemplate` (`source`, `hidden`: the panel's Remove... / Add back), `reset`, `restore`, `bind`, `unbind`, `ignore`, `removeContainer`, `label`, `keep`, `settings` | the setup |
| `op` (`Deposit`, `Distribute`, `SortAll`, `Retrieve` + `pattern`, `Restock`) | operations |
| `sellChest`, `quote`, `sellConfirmed`, `buyBack` | the sell chest (the quote is what the confirmation box shows) |
| `displayUse`, `displayUseAll` (`kind`: *Use all* on the displays of that kind found in the cell), `displayCategory`, `displayCategories` (`ref`, `categories`: a used display's categories, `[]` clears), `fill`, `chooseArmorSet` (`ref`, `set`: a piece of the set, or null = Auto) | displays (the layout stores the set by its indexed piece, e.g. the Iron cuirass `0x00012E49`) |
| `export` (`expect`: `ok` / `exists`) | Save as template |
| `exitDoor`, `addArea`, `renameArea`, `removeArea` | homes over several cells |
| `openContainer` (waits 16 s, past Browse's own retry), `store`, `take`, `closeMenu`, `answerMessageBox` (`button`), `hotkey` (`target`), `choose` (`entry`: its text or start, or `index`; -1 = Esc), `acceptSuggestion`, `declineSuggestion` | by hand |
| `learnMany` (`to`, `count`: 130 by default) | learned items in bulk (paging tests) |
| `bindHotkey` (`target`: `keyboard` / `gamepad`; `code`, `cancel` or `clear`), `panelVisible` (`page`), `setCategories` (`ref`, `categories`) | the panel (`bindHotkey` logs `Hotkey capture (test): ...`) |
| `sit` (`keyword`, `"skipIfClosed": true`: only for a station the plan's own `enable` turned on, skip with *the station opened no menu* after every fallback), `consume`, `craft` (`uses`, `product`) | crafting (`craft`: through the open recipe menu's own callback; plans that craft close `Crafting Menu`, as `closeMenu` with no menu closes `ContainerMenu`) |
| `writeLayout`, `deleteLayout`, `writeBackup`, `deleteBackup` (`cell`: the plan's home or a cell its `enter` steps reached; never another cell's file this run did not write), `clearContainer` | the plan's own home |
| `console`, `snapshot`, `wait`, `waitIdle` | |

| Checks (`check`; `within`: seconds to wait for it) | |
|---|---|
| `count` (`in`, `item`, `filter`, `from`: change since a snapshot, `eq`/`ge`/`le`/`gt`/`lt`/`ne`), `gold` | inventories |
| `log`, `notify` (`contains` / `regex`, `absent`, `count`, `max`, `since`) | the log since the test (or `action`: the last action, `plan`, `run`); `AutoTest:` lines are skipped |
| `noErrors` (`except`) | no `[error]` line |
| `layout`, `templateFile`, `backupFile` (`pointer` + `eq` / `contains` / `lacks` / `absent` / `size`), `layoutFile`, `files` | files |
| `conserved` (`snapshot`, `ignore`) | no item lost or gained (containers, mannequins, loose weapons and shields, an item plaque that shows its one item: holder `plaque <name>`). Snapshots leave out leveled lists (LVLI) on every plan, the harness's own ignore list (`Steps.cpp` `IsEngineToken`): `Skyrim.esm\|0x0010FCFB` is LItemBarrelFoodSame70, a barrel's leveled list the engine resolves into food |
| `menuEntries` (`contains` / `lacks` / `eq` / `order`: these entries in this order, others between allowed; `includes`: substrings that must appear in the entries, in this order; `body` / `bodyLacks`: the message box's text contains it / does not) | the open UIExtensions list, or the last message box's buttons |
| `panel` (`template`: a source, with `hidden` / `applied` / `best`; `inventoryHas`: a form ref; `aimed`: a ref or null, with `managed`: bool; `keepEntry`: a ref, with `name`: the name the keep rules show, `""` = not loaded; `displayAimed`: a ref or null, the Displays page's marked display, use `within`; `ownChanges`: bool; `catalogue`: `{"source", "installed"}`; `status`: `{page, contains, empty, error}`) | what the panel shows |
| `displayHolds` (`ref`, or `refs`: an array, one of them holds the item, none with `holds: false`; `item`, `holds`) | what a display holds: a mannequin, case or bookshelf by its inventory, a rack by the loose reference in its trigger, an item plaque by its script's placed flag (the script keeps the item in no inventory: `conserved` counts a plaque made for one item that shows it, not one made for a list) |
| `home`, `bestTemplate` (`source`: null = no template offered), `menu`, `quote`, `crafting`, `hookedCount`, `hooks` | state |

### 9.5 What is covered, what stays manual

Automated (plan): startup log and the `[error]`-free start (0), 5.0.1/5.0.2; eligibility 1.1-1.5, 1.8; M1 2.1-2.4
(tempered, renamed, stolen and enchanted copies survive Deposit and two round trips; equipped copies stay; 2.3 checks
ammo too), 2.6
(and 2.6b: more arrows of the equipped type);
M2 3.1-3.5 (bind, Distribute, sort on arrival, Learn, undo, item families, the category suggestion, Sort all,
Retrieve by category, Ignore / Remove / Make master / Reset); 3.6 (Use and Re-apply template), 5.2.11 (plan
`11_Hjerim.json`: a template removed from a home is not offered or applied, Remove and Add back), 3.6.5-3.6.8 (Save as
template, same name, round trip), 3.6a (backup copy, restore, broken copy); 3.6b.5 (Hearthfire not "rebuilt");
3.7 (a rack takes a weapon on Deposit; 3.7-template in plans 10-15: the template's displays, Deposit puts a book on a
shelf and a weapon on a rack where empty racks exist; 3.7-armorset and 6.2-mb-armorset (the message-box *Armor set...* picker: *[x] Auto*, Iron, *wears*, *picks its
own set*) in `14_VlindrelHall`; 3.7-hendraheim-soulgems); 3.8b (hooks installed, a storage session at the cooking pot counts the
stored materials on the menu's thread, a consumption and a craft through the menu's own callback take the
shortfall from storage); 3.8a (Restock, Restock after Deposit); 3.9 (add,
rename, Deposit from, remove an area; 3.9.6 takes in an untouched cellar; 3.9.7 in the Lakeview and Heljarchen plans
enables the second grounds chest first; 3.9-myrwatch-whole in plan 31: the home's template from the tower, the
gallery joins, Deposit conserved; 3.9-tundra-f in plan 38 and 1.1-gallery in plan 31: a merged template is offered
only in its main cell; plan 38: 3.9-tundra-b3 (no *(master not here)* in the cellar), 3.9-tundra-b4 (a sword stored
in the cellar chest moves on when it is closed, nothing learned), 3.9-tundra-b5 (the hotkey at `0xF85` opens the master
menu), 3.9-tundra-h (in the cellar of a home not set up, the hotkey offers only *Use template "Tundra Homestead"*;
Cancel changes nothing), 3.9-tundra-h2 (choosing it sets up the whole home from the cellar), 6.2.5-tundra-browse
(Browse lists the cellar's master), 3.9-tundra-i (*Make master
of Cellar...* sets `/cells/0/master` to `0xDBB` and keeps `/master`); plan 21 3.9.1-unvisited and 3.9.1-visit (Deposit
with the master away or in an area not visited yet says why; then every area visited); plan 22 3.9-heljarchen-border
(*Make master of Grounds...* from the Smelter yard names the container's area); plan 14 6.2-lists-armorset (the
UIExtensions *Armor set...* list); 3.9b-TelJerdein
in plan 42: the potions container is ignored, a potion deposited in the Foyer goes to the home's master (persistent,
so it comes before the Foyer's urn `0x75488`, DESIGN §6.7); 3.9g-WinterCove in plan 44: a Deposit in the main hall sends soul gems to the hall's
Soulgems container and arrows to the storage room's Weapons chest, conserved; plan 44's master is `0x23D65` and its
storage cell entry has master `0x216D8`: 2.3b-WinterCove sends a homeless potion to `0x23D65` from the main hall,
3.9c-WinterCove one to `0x23D65` from the storage room too, ahead of the room's own `0x216D8`); 3.10 (sell chest routing, quote, sale, never sold, buy back, changed
meanwhile); 4.1, 4.4; a Creation Club home (Tundra Homestead). Hotkey menus and panel: `17_HotkeyLists` (`iMode = 0`:
6.2.2-lists-order, 6.2.16-lists, 6.2.4-lists-everything, 6.2-lists-pages, Reset home... No and Yes),
`18_HotkeyMessageBoxes` (`iMode = 1`: 6.2.4-mb-everything, the setup prompt's
body with a backup, master and Advanced paging, Reset home... No and Yes, the container menu over two pages, a
Categories group on page 2 and Back, Make master... No; 6.2-mb-display: a rack's *Categories...* box with *(nothing chosen:
anything that fits)*, a toggle and Back), `05_Startup` (6.6.1, 6.6.1a, 6.6.1b), 3.7-useall (`14_VlindrelHall`, the Displays page's *Use all*),
6.2.5-lakeview-browse (`21_LakeviewManorBuilt`), 3.6c's panel status
line (`10_Breezehome`), 6.2.5-bloodchill (Browse right after a container closed, `33_BloodchillManor`), and 3.7 in
`41_Niflholm` / `42_TelJerdein` (no display fills in another cell). `24_HeljarchenHallShipped` and
`25_WindstadManorShipped` check that the shipped template is chosen and applied.

Plans per home (file-name prefix): `1N_` the vanilla and DLC city homes (Hjerim, Honeyside, Proudspire Manor, Vlindrel
Hall, Severin Manor, the Arch-Mage's Quarters; `17_`/`18_` the hotkey menus), `2N_` the Hearthfire homes built, each as
one home over hall, cellar and grounds through the shipped template (3.9 steps 1-11), `3N_` the Creation Club homes (Myrwatch and its gallery,
Hendraheim's displays, Bloodchill's Learn paths, Shadowfoot's sell chest, Goldenhills' Restock and cooking pot,
Nchuanthumz' lever-built containers and the robustness steps, Gallows Hall's template paths, Tundra Homestead with its
cellar as one home, Dead Man's Dread and its template), `4N_` mod homes over several cells (Niflholm, Tel Jerdein,
Antennaria, Winter Cove, Unique Vampire Dens), `5N_` single-cell mod homes (Pinegrove Lodge, Bal Ruhn, Wuth Rein,
Ebongrove, Hydda, Sicarius' Refuge, Black Fur Refuge, White Lighthouse, Mirele Bismath Reborn as a self-sorting
home) and `6N_` Wynter's Breezehome Redux (its own profile). The mod homes need `tests/ingame/profile/extra-mods.txt`
(§9.1).

Manual: everything you look at or click in a menu (the UIExtensions lists, message boxes, the Menu Framework
panel 5.1-5.8, the activate prompt, QuickLoot IE, floating icons: sections 5-6), the hotkey with menus open (1.7,
2.5), how displays look (3.7 placement, nodes, bookshelves), favorited and quest items (2.2), items a script hands
back (2.7), other home mods and the Menu Framework missing (3.6b, 5.0.3), Move mode at a forge (3.8; Breezehome has
none), the forge outside a built Lakeview Manor (3.9-lakeview; plans 21-23 cover the rest of the built Hearthfire
homes), timed selling (3.10.9), and the Dova Stash Editor (section 8). Severin Manor's purchase (1.9, 1.9-owned) is
automated in `15_SeverinManor`; the *1.9* manual entry in `05_Startup` is a leftover.

Known limitation: Wuth Rein's bookshelf `0x13C5`: a Deposit put a book from chest `0x137D` on it, and about 1 s later
the shelf held none with no move logged (its marker lies outside the shelf's trigger); the root cause is unknown. The
shipped template (1.0.2) leaves the scripted shelves out, a book that drops off such a shelf goes back to its container
(the copy found in the cell's containers first, a fresh one only when it is found nowhere; that shelf is then left
alone for the session), so 3.4-wuthrein is a plain pass with conservation; 3.7-wuthrein still
turns `0x13C5` on by hand to exercise Fill now.

Known limitation (third party): `QuickLootIE.dll` crashes on every game exit (offset 0x918ec), also in the test runs,
after `results.json` is written; it is QuickLoot IE's fault, not Dova Stash's, and loses nothing.

Not reproduced since 2026-10-22: the game stopping at a cell change (plan 10 at the `exitDoor` into Whiterun, plans
21 and 31), also without QuickLoot (docs/research/playtest-2026-10-14.md). In round 6 every *Cell change* start has
its done line (`20261028-021524`: 146 of 146; the full run `20261028-034030`: 318 of 318), and plan 10 ran past 3.10
and the `exitDoor` to its end in `20261028-013814`, `021524` and `034030`. Re-check: a run that stops with *stalled*
reopens it. To bisect a hang, the last line with no partner names the stuck step: debug *Cell change (&lt;source&gt;): start in X* / *done in X after N ms* (sources: load
game, actor cell, cell loaded, ...; a repeat check of the same cell within 5 s is skipped at trace, except after the
player moves: actor cell); trace *Task #N &lt;tag&gt;: start* / *done* for each queued event task; *AutoTest: wait: X (up
to N s)* and *done* / *timed out after N ms*; *AutoTest: console: X returned*; *Overlay: first frame in X: start* /
*done*.

Run record (round 5, 2026-10-28, from session 0 through the desktop handoff): `20261028-011117` (+ wynter
`20261028-013235`), all plans: pass 155, fail 10 (before the round's fixes); `20261028-013814`, plans 10-18, 20-25
and 38: pass 161, fail 4, skip 1 (3.9.4: the anvil menu did not open, environmental), manual 13;
`20261028-020304`, plan 38: pass 13, fail 0, manual 2. Plan 47 (3.7-whitepeak-extra and 3.7-whitepeak-testchest,
no longer `expectFail`) was not run: Whitepeak Tower.esp is not in the AutoTest profile.

Run record (round 6, 2026-10-28): `20261028-021524`, all plans: pass 153, fail 0, skip 1, manual 15, not finished
(the game exited by itself after 3.9.4 in plan 23, Windstad Manor's grounds smelter; `sit` since then never
activates a station whose menu is open); wynter `20261028-023653`: 12/12; plan 47 (`20261028-024118`, Whitepeak
Tower now in `extra-mods.txt`): 5/5. Full run `20261028-034030`: pass 417, fail 3, skip 1, manual 30; those 3 have since passed or became
a skip with its reason: plans 21-23 (`20261028-051335`) pass 40, skip 1; plans 14, 17, 18 and 38 (`20261028-052500`)
pass 44. Plans 42 and 44 now expect the home's master (shipped area masters; the persistent home master comes first,
DESIGN §6.7). The crash in `20261028-044838` was in engine teardown after `qqq` (`BSFaceGenModel` destructor), not
during the tests.

The Menu Framework panel's drawn UI (5.1-5.8: tab switching, the yes/no buttons, *Only installed homes*, the set
picker's rows) has no automated check: the automated panel checks drive the Controller, the panel's snapshot and the
key capture, not the ImGui code. No manual pass of 5.1-5.8 with the current DLL is recorded yet; record one (date, DLL
build, pass/fail per row) under §5 when it is done.