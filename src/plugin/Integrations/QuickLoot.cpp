#include "Integrations/QuickLoot.h"

#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Home/Store.h"
#include "Icons/IconService.h"
#include "Settings.h"
#include "UI/Labels.h"

#include <QuickLootAPI.h>

#include <thread>

namespace dova::integrations::quickloot
{
	namespace
	{
		namespace QL = QuickLoot::API;

		bool g_ready{ false };

		bool Enabled()
		{
			return Settings::GetSingleton().GetIntegrations().quickLoot;
		}

		RE::TESObjectREFR* Resolve(RE::ObjectRefHandle a_handle, RE::NiPointer<RE::TESObjectREFR>& a_hold)
		{
			a_hold = a_handle.get();
			return a_hold.get();
		}

		// SKSE key code of the hotkey for the active input device, or nullopt (no button then).
		// QuickLoot's button art indices are SKSE key codes (QuickLootIE src/Input/ButtonArtIndex.h).
		std::optional<std::uint16_t> HotkeyArt()
		{
			const auto& input = Settings::GetSingleton().GetInput();
			auto*       devices = RE::BSInputDeviceManager::GetSingleton();
			const bool  gamepad = devices && devices->IsGamepadEnabled();
			const auto  code = gamepad ? input.gamepadHotkey : input.hotkey;
			if (code == Settings::kNoKey || code <= 0 || code > 0xFFFF) {
				return std::nullopt;
			}
			return static_cast<std::uint16_t>(code);
		}

		// Name of the container Dova Stash would put this stack in, "" when it already is there, would not
		// be moved, or the home has no layout. `a_here` is set when it belongs in `a_container`.
		std::string DestinationOf(const home::Home& a_home, RE::TESObjectREFR* a_container, const QL::ItemStack* a_stack, bool* a_here = nullptr)
		{
			if (!a_stack || !a_stack->entry || !a_stack->entry->object || !a_home.layout) {
				return {};
			}
			core::ItemInfo info;
			if (!game::FillBaseInfo(info, a_stack->entry->object)) {
				return {};
			}
			info.count = 1;
			if (!core::SortPlanner::CanMove(info)) {
				return {};
			}
			const core::SortPlanner planner(*a_home.layout, home::Store::Get().Categories());
			const auto              dest = planner.ResolveDestination(info);
			if (!dest) {
				return {};
			}
			const auto here = game::ToFormRef(a_container);
			if (here && dest->container == *here) {
				if (a_here) {
					*a_here = true;
				}
				return {};
			}
			if (a_home.layout->IsMaster(dest->container)) {
				return {};  // no better place than the inbox: nothing useful to say
			}
			// The container's in-game name (which is the player's custom name when they renamed it).
			return game::RefName(game::LookupRef(dest->container));
		}

		// The item list itself is what every QuickLoot skin shows, so the destination goes there:
		// "Iron Sword  >  Weapon Rack". Only in an enabled home, only for items that belong elsewhere.
		void OnModifyItemData(QL::ModifyItemDataEvent* a_event)
		{
			if (!a_event || !Enabled() || !Settings::GetSingleton().GetIntegrations().quickLootItemTags) {
				return;
			}
			RE::NiPointer<RE::TESObjectREFR> hold;
			auto*                            container = Resolve(a_event->container, hold);
			const auto                       role = ui::DescribeContainer(container);
			auto*                            home = home::HomeRegistry::Get().Peek();
			if (!home || role.kind == ui::ContainerRole::Kind::None || role.kind == ui::ContainerRole::Kind::Ignored) {
				return;
			}
			const auto dest = DestinationOf(*home, container, a_event->stack);
			if (dest.empty()) {
				return;
			}
			RE::GFxValue name;
			if (!a_event->data.GetMember("displayName", &name) || !name.IsString()) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView("LootMenu") : nullptr;
			if (!movie) {
				return;
			}
			const auto   text = std::format("{}  >  {}", name.GetString(), dest);
			RE::GFxValue value;
			movie->CreateString(&value, text.c_str());  // managed copy: our std::string dies with this call
			a_event->data.SetMember("displayName", value);
		}

		// --- Title icon ---------------------------------------------------------------------------------
		// The container's category icon (the player's own SkyUI/I4 art) next to its name in QuickLoot's
		// title. QuickLoot's API only takes text, so we add a clip to QuickLoot's own movie, positioned
		// from _root.lootMenu.title: the TextField QuickLoot's DLL writes the name into, so every QuickLoot
		// skin has it. Same loadMovie + gotoAndStop technique SkyUI uses for icons.
		constexpr auto kLootMenu = "LootMenu";
		constexpr auto kHolder = "dovaStashTitleIcon";

		struct TitleIconState
		{
			RE::FormID    container{ 0 };
			core::IconRef icon;
			bool          shown{ false };
			std::uint64_t serial{ 0 };
		};
		TitleIconState g_titleIcon;

		void LaterUI(std::chrono::milliseconds a_delay, std::function<void()> a_fn)
		{
			std::thread([a_delay, fn = std::move(a_fn)]() mutable {
				std::this_thread::sleep_for(a_delay);
				SKSE::GetTaskInterface()->AddUITask(std::move(fn));
			}).detach();
		}

		double NumberMember(RE::GFxValue& a_obj, const char* a_name)
		{
			RE::GFxValue v;
			return a_obj.GetMember(a_name, &v) && v.IsNumber() ? v.GetNumber() : 0.0;
		}

		// UI thread. Hides the holder (when present).
		void HideTitleIcon()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kLootMenu) : nullptr;
			RE::GFxValue holder;
			if (movie && movie->GetVariable(&holder, "_root.lootMenu.dovaStashTitleIcon") && holder.IsDisplayObject()) {
				RE::GFxValue::DisplayInfo info;
				info.SetVisible(false);
				holder.SetDisplayInfo(info);
			}
		}

		// UI thread: waits for the icon SWF to load, then shows the label next to the title text.
		void PlaceTitleIcon(std::uint64_t a_serial, int a_tries)
		{
			if (a_serial != g_titleIcon.serial) {
				return;  // superseded
			}
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kLootMenu) : nullptr;
			RE::GFxValue holder, icon, title;
			if (!movie || !movie->GetVariable(&holder, "_root.lootMenu.dovaStashTitleIcon") || !holder.GetMember("icon", &icon) ||
				!movie->GetVariable(&title, "_root.lootMenu.title") || !title.IsDisplayObject()) {
				return;
			}
			RE::GFxValue loaded, total;
			icon.Invoke("getBytesLoaded", &loaded);
			icon.Invoke("getBytesTotal", &total);
			if (!loaded.IsNumber() || !total.IsNumber() || total.GetNumber() <= 0.0 || loaded.GetNumber() < total.GetNumber()) {
				if (a_tries > 0) {
					LaterUI(std::chrono::milliseconds(30), [a_serial, a_tries]() { PlaceTitleIcon(a_serial, a_tries - 1); });
				} else {
					logger::warn("QuickLoot title icon: {} did not load", g_titleIcon.icon.source);
				}
				return;
			}
			const auto& ref = g_titleIcon.icon;
			icon.GotoAndStop(ref.label.c_str());
			if (ref.color) {
				RE::GFxValue color;
				movie->CreateObject(&color, "Color", &icon, 1);
				const RE::GFxValue rgb(static_cast<double>(*ref.color));
				color.Invoke("setRGB", nullptr, &rgb, 1);
			}
			// Natural size at 100 %, then fit the icon to the title's text height.
			RE::GFxValue::DisplayInfo reset;
			reset.SetScale(100.0, 100.0);
			icon.SetDisplayInfo(reset);
			const double w = NumberMember(icon, "_width");
			const double h = NumberMember(icon, "_height");
			const double textH = std::max(NumberMember(title, "textHeight"), NumberMember(title, "_height") * 0.6);
			if (w <= 0.0 || h <= 0.0 || textH <= 0.0) {
				logger::warn("QuickLoot title icon: label '{}' in {} has no size", ref.label, ref.source);
				return;
			}
			const double size = textH * 1.1;
			const double scale = 100.0 * size / std::max(w, h);
			RE::GFxValue::DisplayInfo iconInfo;
			iconInfo.SetScale(scale, scale);
			icon.SetDisplayInfo(iconInfo);

			// Where the title's text ends, whatever the skin's alignment.
			RE::GFxValue format, align;
			title.Invoke("getTextFormat", &format);
			std::string alignment = "left";
			if (format.IsObject() && format.GetMember("align", &align) && align.IsString()) {
				alignment = align.GetString();
			}
			const double x = NumberMember(title, "_x"), y = NumberMember(title, "_y");
			const double width = NumberMember(title, "_width"), height = NumberMember(title, "_height");
			const double textW = NumberMember(title, "textWidth");
			double       textEnd = x + textW + 4.0;
			if (alignment == "center") {
				textEnd = x + (width + textW) / 2.0;
			} else if (alignment == "right") {
				textEnd = x + width;
			}
			RE::GFxValue::DisplayInfo info;
			info.SetPosition(textEnd + size * 0.4, y + (height - size * (std::min(w, h) / std::max(w, h))) / 2.0);
			info.SetVisible(true);
			holder.SetDisplayInfo(info);
			g_titleIcon.shown = true;
			logger::debug("QuickLoot title icon: {} | {} at ({}, {}), title align {}", ref.source, ref.label, textEnd, y, alignment);
		}

		// UI thread: show `a_icon` for container `a_container` (empty icon = hide).
		void ShowTitleIcon(RE::FormID a_container, core::IconRef a_icon)
		{
			if (a_icon.Empty()) {
				if (g_titleIcon.shown || g_titleIcon.container != a_container) {
					HideTitleIcon();
				}
				g_titleIcon = { a_container, {}, false, g_titleIcon.serial + 1 };
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kLootMenu) : nullptr;
			RE::GFxValue lootMenu, holder, icon;
			if (!movie || !movie->GetVariable(&lootMenu, "_root.lootMenu") || !lootMenu.IsDisplayObject()) {
				return;
			}
			if (!lootMenu.GetMember(kHolder, &holder) || !holder.IsDisplayObject()) {
				RE::GFxValue depth;
				lootMenu.Invoke("getNextHighestDepth", &depth);
				if (!lootMenu.CreateEmptyMovieClip(&holder, kHolder, depth.IsNumber() ? static_cast<std::int32_t>(depth.GetNumber()) : 0x3FFF)) {
					return;
				}
				g_titleIcon.icon = {};
			}
			if (!holder.GetMember("icon", &icon) || !icon.IsDisplayObject()) {
				holder.CreateEmptyMovieClip(&icon, "icon", 0);
				g_titleIcon.icon = {};
			}
			const bool reload = g_titleIcon.icon.source != a_icon.source;
			g_titleIcon.container = a_container;
			g_titleIcon.icon = std::move(a_icon);
			const auto serial = ++g_titleIcon.serial;
			if (reload) {
				const RE::GFxValue path(g_titleIcon.icon.source.c_str());
				icon.Invoke("loadMovie", nullptr, &path, 1);
			}
			// Even without a reload the title text (and so the position) may have changed: re-place after
			// QuickLoot has refreshed it.
			LaterUI(std::chrono::milliseconds(20), [serial]() { PlaceTitleIcon(serial, 50); });
		}

		// Main thread (QuickLoot handlers): decide the icon for this container and hand it to the UI thread.
		void RequestTitleIcon(RE::TESObjectREFR* a_container, const ui::ContainerRole& a_role)
		{
			core::IconRef icon;
			const auto*   home = home::HomeRegistry::Get().Peek();
			if (Settings::GetSingleton().GetIntegrations().quickLootTitleIcon && a_container && home && home->layout &&
				a_role.kind == ui::ContainerRole::Kind::Bound) {
				icon = icons::ForContainer(*home->layout, a_container);
			}
			const auto id = a_container ? a_container->GetFormID() : 0;
			if (id == g_titleIcon.container && icon == g_titleIcon.icon && (icon.Empty() || g_titleIcon.shown)) {
				// Same container, same icon: just keep it next to the (possibly re-rendered) title.
				if (!icon.Empty()) {
					SKSE::GetTaskInterface()->AddUITask([serial = g_titleIcon.serial]() { PlaceTitleIcon(serial, 5); });
				}
				return;
			}
			SKSE::GetTaskInterface()->AddUITask([id, icon]() { ShowTitleIcon(id, icon); });
		}

		// At most one short entry: what the container is ("Master", "Alchemy", "Weapons, Armor +1"), or, when
		// the item-list tags are off, where the selected item goes ("> Weapon Rack"). Nothing for ignored
		// or unmanaged containers, no prefix. Short on purpose: the info bar is right-aligned and long text
		// runs into the weight readout. The API only takes text (no icons); the container's category icon
		// is the floating overlay above it.
		void OnPopulateInfoBar(QL::PopulateInfoBarEvent* a_event)
		{
			if (!a_event || !Enabled()) {
				return;
			}
			RE::NiPointer<RE::TESObjectREFR> hold;
			auto*                            container = Resolve(a_event->container, hold);
			const auto                       role = ui::DescribeContainer(container);
			auto*                            home = home::HomeRegistry::Get().Peek();
			if (!home || role.kind == ui::ContainerRole::Kind::None || role.kind == ui::ContainerRole::Kind::Ignored) {
				return;
			}
			std::string text;
			// The item list already tags misplaced items, so the info bar only repeats a destination when
			// those tags are switched off.
			if (!Settings::GetSingleton().GetIntegrations().quickLootItemTags) {
				if (const auto dest = DestinationOf(*home, container, a_event->stack); !dest.empty()) {
					text = "> " + dest;
				}
			}
			const bool iconInTitle = Settings::GetSingleton().GetIntegrations().quickLootTitleIcon && role.kind == ui::ContainerRole::Kind::Bound &&
			                         !icons::ForContainer(*home->layout, container).Empty();
			if (text.empty() && !iconInTitle) {
				text = ui::RoleSummary(role, 2);  // "Master", "Alchemy", "Weapons, Armor +1"
			}
			if (!text.empty()) {
				a_event->result.push_back(RE::BSString(text.c_str()));
			}
			static std::string last;
			auto               now = std::format("{} | {}", game::Describe(container), text);
			if (now != last) {
				logger::debug("QuickLoot info bar: {}", now);
				last = std::move(now);
			}
		}

		bool WantsButton(RE::ObjectRefHandle a_container)
		{
			RE::NiPointer<RE::TESObjectREFR> hold;
			return ui::DescribeContainer(Resolve(a_container, hold)).kind != ui::ContainerRole::Kind::None;
		}

		void UpdateTitleIconFor(RE::ObjectRefHandle a_container)
		{
			RE::NiPointer<RE::TESObjectREFR> hold;
			auto*                            container = Resolve(a_container, hold);
			RequestTitleIcon(container, Enabled() ? ui::DescribeContainer(container) : ui::ContainerRole{});
		}

		void OnModifyButtonBar(QL::ModifyButtonBarEvent* a_event)
		{
			if (a_event) {
				UpdateTitleIconFor(a_event->container);
			}
			if (!a_event || !Enabled() || !WantsButton(a_event->container)) {
				return;
			}
			if (const auto art = HotkeyArt()) {
				logger::trace("QuickLoot button bar: Stash button (art {})", *art);
				QL::ButtonDefinition2 button{};
				button.label = "Stash";
				button.buttonArtIndex = *art;
				button.stealing = false;
				a_event->buttons.push_back(button);
			}
		}

		// API V20 only (QuickLoot IE before 4.1): same button through the older event.
		void OnPopulateButtonBar(QL::PopulateButtonBarEvent* a_event)
		{
			if (a_event) {
				UpdateTitleIconFor(a_event->container);
			}
			if (!a_event || !Enabled() || !WantsButton(a_event->container)) {
				return;
			}
			if (const auto art = HotkeyArt()) {
				QL::ButtonDefinition button{};
				button.label = "Stash";
				button.buttonArtIndex = *art;
				a_event->result.push_back(button);
			}
		}
	}

	void Init()
	{
		// Handlers are registered whenever QuickLoot is installed and check bQuickLoot on every call, so
		// the setting can be toggled live.
		if (!QL::QuickLootAPI::Init("DovaStash", QL::ApiVersion::kV20)) {
			logger::info("QuickLoot IE: not installed (or API V20 unavailable)");
			return;
		}
		g_ready = true;
		QL::QuickLootAPI::RegisterPopulateInfoBarHandler(OnPopulateInfoBar);
		if (QL::QuickLootAPI::IsReady(QL::ApiVersion::kV21)) {
			QL::QuickLootAPI::RegisterModifyButtonBarHandler(OnModifyButtonBar);
			QL::QuickLootAPI::RegisterModifyItemDataHandler(OnModifyItemData);
			logger::info("QuickLoot IE: API V21, item destinations + info bar + button bar");
		} else {
#pragma warning(suppress: 4996)
			QL::QuickLootAPI::RegisterPopulateButtonBarHandler(OnPopulateButtonBar);
			logger::info("QuickLoot IE: API V20, info bar + button bar (legacy)");
		}
	}

	bool IsReady() noexcept
	{
		return g_ready;
	}

	void Refresh()
	{
		if (g_ready) {
			logger::trace("QuickLoot: refresh requested");
			QL::QuickLootAPI::RefreshLootMenu();
		}
	}
}
