#pragma once

namespace dova
{
	// User settings from Data/SKSE/Plugins/DovaStash.ini. Every key has an in-code default, so a
	// missing file or key is never an error. Read at plugin load; changed live from the SKSE Menu
	// Framework settings page (Update), which also writes them back to the INI. Main thread only:
	// everything that uses a setting reads it when it needs it, so a change applies immediately.
	class Settings
	{
	public:
		static constexpr auto kPath = "Data/SKSE/Plugins/DovaStash.ini"sv;

		// Sentinel for "no gamepad hotkey".
		static constexpr std::int32_t kNoKey = -1;

		struct General
		{
			// spdlog level: 0 trace, 1 debug, 2 info, 3 warn, 4 error, 5 critical, 6 off.
			// Info for players. Testers set 0 (everything) on the settings page or in the INI.
			std::int32_t logLevel{ 2 };

			bool operator==(const General&) const = default;
		};

		struct Input
		{
			// DirectInput scan code. Default 0x25 (37) = K.
			// Vanilla Skyrim's PC control map (Interface/Controls/PC/controlmap.txt) binds
			// W A S D, E, R, Q, Tab, T, I, P, J, M, F, Z, C, Shift, Ctrl, Alt, Space, CapsLock,
			// '/', F5, F9, 1-8 (favorites), Esc and '~' (console). K is unbound there, is not one
			// of the letters other mods most often take by default (G, H, N, V), and a scan of
			// the SKSE plugin INIs in a ~2000-mod MO2 list found no other binding of code 37.
			std::int32_t hotkey{ 0x25 };

			// SKSE gamepad key code (266-281), or kNoKey. Disabled by default because every
			// gamepad button already has a vanilla action; a combo/hold scheme arrives with the
			// input sink in M1.
			std::int32_t gamepadHotkey{ kNoKey };

			bool operator==(const Input&) const = default;
		};

		struct Templates
		{
			// DESIGN §4.1: auto-enable a matching template on first entry (on by default).
			bool autoEnable{ true };

			bool operator==(const Templates&) const = default;
		};

		struct Sorting
		{
			// DESIGN §8.1: closing the master container distributes its contents.
			bool distributeOnMasterClose{ true };
			// Items put into the master are sorted out of it right away (~0.4 s later), even while its
			// container menu is still open (Skyrim Souls); the open menu is refreshed.
			bool sortOnArrival{ true };
			// DESIGN §8.1 Learn: items you put into a managed container by hand teach their destination.
			bool learn{ true };
			// DESIGN §8.6: after this many learned items of one category point at the same container, ask
			// once whether the whole category should go there ("Send all Books to End Table?"). 0 = never ask.
			std::int32_t suggestCategoryAfter{ 3 };
			// DESIGN §8.6: for this many seconds after a learn, the hotkey takes it back. 0 = no undo.
			std::int32_t learnUndoSeconds{ 5 };
			// DESIGN §8.4: Deposit and Sort all put gear on the home's empty weapon racks, shield plaques,
			// mannequins, item plaques and display cases.
			bool fillDisplays{ true };
			// DESIGN §8.7: Deposit then tops you up to every keep quota marked "Restock" from the home's containers
			// (nothing happens without such a quota).
			bool restockAfterDeposit{ true };
			// DESIGN §8.5 Materials at the workbench (core::crafting::SupplySetting):
			// 0 = auto: craft from storage where the hooks are in (SE/AE), else move; off when another
			//     crafting-from-storage mod is loaded,
			// 1 = off, 2 = move (bring the stored materials when you sit down, the rest goes back when you get
			//     up), 3 = craft from storage (the menu counts the containers; crafting takes from them).
			std::int32_t craftingSupply{ 0 };
			// ... including weapons, armor and ammo that recipes use as materials (breakdown, upgrades).
			bool craftingSupplyGear{ false };

			bool operator==(const Sorting&) const = default;
		};

		struct Selling
		{
			// DESIGN §8.8: what the sell chest pays, in percent of the game's own barter price for you (your
			// Speech, perks and Fortify Barter). 10-200.
			std::int32_t pricePercent{ 100 };
			// Timed selling: what has been in the sell chest for this many game hours is sold by itself (you get
			// the gold and one HUD line). 0 = off (the default): only "Sell now" sells, after you confirm. 0-168.
			std::int32_t autoSellHours{ 0 };

			bool operator==(const Selling&) const = default;
		};

		struct UIOptions
		{
			// DESIGN §10.1/§10.3: what the hotkey opens in the world.
			// 0 = auto: UIExtensions list menus when UIExtensions is installed (not VR), else native
			//     message boxes. The SKSE Menu Framework panel stays available from that framework's menu.
			// 1 = always message boxes, 2 = the SKSE Menu Framework panel (when loaded),
			// 3 = UIExtensions (falls back to message boxes when it is not installed).
			std::int32_t mode{ 0 };
			// UIExtensions lists and text entry open centred on screen (whatever the skin's own position).
			bool centerMenus{ true };

			bool operator==(const UIOptions&) const = default;
		};

		struct Integrations
		{
			// QuickLoot IE info bar + button bar (DESIGN §10.3).
			bool quickLoot{ true };
			// QuickLoot IE item list: "Iron Sword  >  Weapon Rack" for items that belong elsewhere.
			bool quickLootItemTags{ true };
			// QuickLoot IE: a row of category icons (one per bound category) where the info bar text goes,
			// instead of the category names.
			bool quickLootIcons{ true };
			// Container role in the activate prompt ("Chest (Weapons)"); every runtime including VR.
			bool activatePrompt{ true };

			bool operator==(const Integrations&) const = default;
		};

		struct Overlay
		{
			// Floating icons above managed containers, drawn with the player's own SkyUI/I4 icons.
			bool  enabled{ false };  // off by default (player feedback); the settings page turns it on live
			float radius{ 1200.0f };   // game units (~17 m)
			std::int32_t maxIcons{ 12 };
			float iconSize{ 40.0f };   // HUD movie units at the closest distance
			bool  requireLineOfSight{ true };

			bool operator==(const Overlay&) const = default;
		};

		// Every setting, by value (what the settings page edits).
		struct Values
		{
			General      general;
			Input        input;
			Templates    templates;
			Sorting      sorting;
			Selling      selling;
			UIOptions    ui;
			Integrations integrations;
			Overlay      overlay;

			bool operator==(const Values&) const = default;
		};

		[[nodiscard]] static Settings& GetSingleton() noexcept;

		// Loads kPath (relative to the game's working directory). Logs what it read.
		void Load();

		[[nodiscard]] Values Get() const;
		// Validates `a_values` (out-of-range values are clamped/reset like on load), makes them current
		// and writes every key to kPath, keeping the file's comments. Returns the names of the keys that
		// changed (empty = nothing changed, nothing written). `a_saved` (optional) tells whether the file was written.
		std::vector<std::string> Update(Values a_values, bool* a_saved = nullptr);

		[[nodiscard]] General GetGeneral() const { return _v.load()->general; }
		[[nodiscard]] Input GetInput() const { return _v.load()->input; }
		[[nodiscard]] Templates GetTemplates() const { return _v.load()->templates; }
		[[nodiscard]] Sorting GetSorting() const { return _v.load()->sorting; }
		[[nodiscard]] Selling GetSelling() const { return _v.load()->selling; }
		[[nodiscard]] UIOptions GetUI() const { return _v.load()->ui; }
		[[nodiscard]] Integrations GetIntegrations() const { return _v.load()->integrations; }
		[[nodiscard]] Overlay GetOverlay() const { return _v.load()->overlay; }

		// Valid keyboard scan code (1-255), mouse (256-265) or gamepad (266-281) code.
		[[nodiscard]] static bool IsValidKeyCode(std::int32_t a_code) noexcept { return a_code > 0 && a_code <= 281; }

	private:
		Settings() = default;

		static void Validate(Values& a_values);
		void        Log(std::string_view a_prefix) const;
		bool        Save() const;

		// Swapped as a whole: written on task threads (ApplySettings), read every frame on the UI thread
		// (overlay, activate prompt, QuickLoot). Getters return copies so no reference outlives a swap.
		std::atomic<std::shared_ptr<const Values>> _v{ std::make_shared<const Values>() };
	};
}
