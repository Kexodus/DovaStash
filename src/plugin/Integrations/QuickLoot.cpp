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
		std::string DestinationOf(const home::DisplayHome& a_home, RE::TESObjectREFR* a_container, const QL::ItemStack* a_stack, bool* a_here = nullptr)
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
			const auto                       home = home::HomeRegistry::Get().Display();
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

		// --- Category icons -----------------------------------------------------------------------------
		// One icon per bound category (the player's own SkyUI/I4 art), in binding order, in the bottom-right
		// corner where the info bar text would go: QuickLoot's API only takes text, so we add clips to
		// QuickLoot's own movie. Positions come from _root.lootMenu.infoBar (right edge) and
		// _root.lootMenu.weight (line height): elements QuickLoot's DLL itself drives, so every QuickLoot
		// skin has them. Same loadMovie + gotoAndStop technique SkyUI uses for icons.
		constexpr auto        kLootMenu = "LootMenu";
		constexpr auto        kRowHolder = "dovaStashCategoryIcons";
		constexpr std::size_t kMaxIcons = 5;

		// UI thread only: RequestRow runs in QuickLoot's handlers and ShowRow/LayoutRow in UI tasks, which
		// SKSE runs on the same thread (confirmed in logs).
		struct RowState
		{
			RE::FormID                 container{ 0 };
			std::vector<core::IconRef> icons;     // what the row should show
			std::size_t                extra{ 0 };  // categories beyond kMaxIcons ("+N")
			std::vector<std::string>   loaded;    // SWF loaded into each slot
			std::uint64_t              serial{ 0 };
		};
		RowState g_row;

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

		void SetVisible(RE::GFxValue& a_clip, bool a_visible)
		{
			RE::GFxValue::DisplayInfo info;
			info.SetVisible(a_visible);
			a_clip.SetDisplayInfo(info);
		}

		bool Loaded(RE::GFxValue& a_clip)
		{
			RE::GFxValue loaded, total;
			a_clip.Invoke("getBytesLoaded", &loaded);
			a_clip.Invoke("getBytesTotal", &total);
			return loaded.IsNumber() && total.IsNumber() && total.GetNumber() > 0.0 && loaded.GetNumber() >= total.GetNumber();
		}

		// UI thread: lays the row out once every icon SWF has loaded (retries while loading).
		void LayoutRow(std::uint64_t a_serial, int a_tries)
		{
			if (a_serial != g_row.serial) {
				return;  // superseded
			}
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kLootMenu) : nullptr;
			RE::GFxValue holder, infoBar, weight;
			if (!movie || !movie->GetVariable(&holder, "_root.lootMenu.dovaStashCategoryIcons") || !holder.IsDisplayObject() ||
				!movie->GetVariable(&infoBar, "_root.lootMenu.infoBar") || !movie->GetVariable(&weight, "_root.lootMenu.weight")) {
				return;
			}
			if (g_row.icons.empty()) {
				SetVisible(holder, false);
				return;
			}
			// Line height of QuickLoot's own bottom line (the weight readout).
			const double lineH = std::max(NumberMember(weight, "textHeight"), NumberMember(weight, "_height") * 0.6);
			const double size = std::max(lineH, 12.0) * 1.15;
			const double gap = size * 0.3;

			std::vector<RE::GFxValue> slots(g_row.icons.size());
			for (std::size_t i = 0; i < g_row.icons.size(); ++i) {
				RE::GFxValue slot;
				if (!holder.GetMember(std::format("i{}", i).c_str(), &slot) || !slot.IsDisplayObject()) {
					return;
				}
				if (!Loaded(slot)) {
					if (a_tries > 0) {
						LaterUI(std::chrono::milliseconds(30), [a_serial, a_tries]() { LayoutRow(a_serial, a_tries - 1); });
					} else {
						logger::warn("QuickLoot icons: {} did not load", g_row.icons[i].source);
					}
					return;
				}
				slots[i] = slot;
			}

			// Right edge of the info bar (its renderers are right-aligned to its width), then right to left.
			double right = NumberMember(infoBar, "_x") + std::max(NumberMember(infoBar, "width"), NumberMember(infoBar, "_width"));
			const double midY = NumberMember(weight, "_y") + NumberMember(weight, "_height") / 2.0;

			RE::GFxValue more;
			if (holder.GetMember("more", &more) && more.IsDisplayObject()) {
				if (g_row.extra > 0) {
					more.SetText(std::format("+{}", g_row.extra).c_str());
					const double w = NumberMember(more, "textWidth") + 6.0;
					RE::GFxValue::DisplayInfo info;
					info.SetPosition(right - w, midY - NumberMember(more, "_height") / 2.0);
					info.SetVisible(true);
					more.SetDisplayInfo(info);
					right -= w + gap;
				} else {
					SetVisible(more, false);
				}
			}
			for (std::size_t n = slots.size(); n-- > 0;) {
				auto&       slot = slots[n];
				const auto& ref = g_row.icons[n];
				slot.GotoAndStop(ref.label.c_str());
				if (ref.color) {
					RE::GFxValue color;
					movie->CreateObject(&color, "Color", &slot, 1);
					const RE::GFxValue rgb(static_cast<double>(*ref.color));
					color.Invoke("setRGB", nullptr, &rgb, 1);
				}
				RE::GFxValue::DisplayInfo reset;
				reset.SetScale(100.0, 100.0);
				slot.SetDisplayInfo(reset);
				const double w = NumberMember(slot, "_width"), h = NumberMember(slot, "_height");
				if (w <= 0.0 || h <= 0.0) {
					logger::warn("QuickLoot icons: label '{}' in {} has no size", ref.label, ref.source);
					SetVisible(slot, false);
					continue;
				}
				const double scale = 100.0 * size / std::max(w, h);
				RE::GFxValue::DisplayInfo info;
				info.SetScale(scale, scale);
				info.SetPosition(right - size, midY - size / 2.0);
				info.SetVisible(true);
				slot.SetDisplayInfo(info);
				right -= size + gap;
			}
			SetVisible(holder, true);
			logger::debug("QuickLoot icons: {} icon(s){} for {:08X}", slots.size(), g_row.extra ? std::format(" +{}", g_row.extra) : "", g_row.container);
		}

		// UI thread: (re)build the row's clips for `a_icons` and lay it out.
		void ShowRow(RE::FormID a_container, std::vector<core::IconRef> a_icons, std::size_t a_extra)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kLootMenu) : nullptr;
			RE::GFxValue lootMenu, holder, weight;
			if (!movie || !movie->GetVariable(&lootMenu, "_root.lootMenu") || !lootMenu.IsDisplayObject()) {
				return;
			}
			if (!lootMenu.GetMember(kRowHolder, &holder) || !holder.IsDisplayObject()) {
				RE::GFxValue depth;
				lootMenu.Invoke("getNextHighestDepth", &depth);
				if (!lootMenu.CreateEmptyMovieClip(&holder, kRowHolder, depth.IsNumber() ? static_cast<std::int32_t>(depth.GetNumber()) : 0x3FFF)) {
					return;
				}
				g_row.loaded.clear();
				// "+N" label, styled like QuickLoot's own weight readout (whatever the skin made it).
				RE::GFxValue args[6]{ RE::GFxValue("more"), RE::GFxValue(100.0), RE::GFxValue(0.0), RE::GFxValue(0.0), RE::GFxValue(60.0), RE::GFxValue(30.0) };
				holder.Invoke("createTextField", nullptr, args, 6);
				RE::GFxValue more;
				if (holder.GetMember("more", &more) && movie->GetVariable(&weight, "_root.lootMenu.weight")) {
					RE::GFxValue format, embed;
					weight.Invoke("getTextFormat", &format);
					if (format.IsObject()) {
						more.Invoke("setNewTextFormat", nullptr, &format, 1);
					}
					if (weight.GetMember("embedFonts", &embed)) {
						more.SetMember("embedFonts", embed);
					}
					more.SetMember("autoSize", RE::GFxValue("left"));
					more.SetMember("selectable", RE::GFxValue(false));
					SetVisible(more, false);
				}
			}
			g_row.container = a_container;
			g_row.icons = std::move(a_icons);
			g_row.extra = a_extra;
			g_row.loaded.resize(std::max(g_row.loaded.size(), g_row.icons.size()));
			for (std::size_t i = 0; i < g_row.loaded.size(); ++i) {
				const auto   name = std::format("i{}", i);
				RE::GFxValue slot;
				if (!holder.GetMember(name.c_str(), &slot) || !slot.IsDisplayObject()) {
					if (i >= g_row.icons.size() || !holder.CreateEmptyMovieClip(&slot, name.c_str(), static_cast<std::int32_t>(i + 1))) {
						continue;
					}
					g_row.loaded[i].clear();
				}
				if (i >= g_row.icons.size()) {
					SetVisible(slot, false);
					continue;
				}
				if (g_row.loaded[i] != g_row.icons[i].source) {
					const RE::GFxValue path(g_row.icons[i].source.c_str());
					slot.Invoke("loadMovie", nullptr, &path, 1);
					g_row.loaded[i] = g_row.icons[i].source;
				}
			}
			const auto serial = ++g_row.serial;
			// QuickLoot re-renders its bottom line on refresh: lay out after that.
			LaterUI(std::chrono::milliseconds(20), [serial]() { LayoutRow(serial, 50); });
		}

		// Main thread (QuickLoot handlers): the icons for this container, handed to the UI thread.
		void RequestRow(RE::TESObjectREFR* a_container, const ui::ContainerRole& a_role)
		{
			std::vector<core::IconRef> icons;
			std::size_t                extra = 0;
			const auto                 home = home::HomeRegistry::Get().Display();
			if (Settings::GetSingleton().GetIntegrations().quickLootIcons && a_container && home && home->layout &&
				a_role.kind == ui::ContainerRole::Kind::Bound) {
				for (const auto& pattern : a_role.binding->categories) {
					auto icon = icons::ForPattern(pattern);
					if (icon.Empty() || std::ranges::find(icons, icon) != icons.end()) {
						continue;  // no icon, or another binding already shows it
					}
					if (icons.size() == kMaxIcons) {
						++extra;
						continue;
					}
					icons.push_back(std::move(icon));
				}
				if (icons.empty()) {  // learned items only
					if (auto icon = icons::ForContainer(*home->layout, a_container); !icon.Empty()) {
						icons.push_back(std::move(icon));
					}
				}
			}
			const auto id = a_container ? a_container->GetFormID() : 0;
			if (id == g_row.container && icons == g_row.icons && extra == g_row.extra) {
				// Same row: re-place it next to QuickLoot's (possibly re-rendered) bottom line.
				SKSE::GetTaskInterface()->AddUITask([serial = g_row.serial]() { LayoutRow(serial, 5); });
				return;
			}
			SKSE::GetTaskInterface()->AddUITask([id, icons = std::move(icons), extra]() mutable { ShowRow(id, std::move(icons), extra); });
		}

		// True when the row will show icons for this container (then the info bar shows no category text).
		bool RowShowsIcons(const home::DisplayHome& a_home, RE::TESObjectREFR* a_container, const ui::ContainerRole& a_role)
		{
			if (!Settings::GetSingleton().GetIntegrations().quickLootIcons || a_role.kind != ui::ContainerRole::Kind::Bound) {
				return false;
			}
			for (const auto& pattern : a_role.binding->categories) {
				if (!icons::ForPattern(pattern).Empty()) {
					return true;
				}
			}
			return a_home.layout && !icons::ForContainer(*a_home.layout, a_container).Empty();
		}

		// At most one short entry: what the container is ("Master", "Alchemy", "Weapons, Armor +1"), or, when
		// the item-list tags are off, where the selected item goes ("> Weapon Rack"). Nothing for ignored
		// or unmanaged containers, no prefix. Short on purpose: the info bar is right-aligned and long text
		// runs into the weight readout. Bound containers show the category icon row instead of their
		// names (RowShowsIcons); the master keeps the word "Master".
		void OnPopulateInfoBar(QL::PopulateInfoBarEvent* a_event)
		{
			if (!a_event || !Enabled()) {
				return;
			}
			RE::NiPointer<RE::TESObjectREFR> hold;
			auto*                            container = Resolve(a_event->container, hold);
			const auto                       role = ui::DescribeContainer(container);
			const auto                       home = home::HomeRegistry::Get().Display();
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
			if (text.empty() && !RowShowsIcons(*home, container, role)) {
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

		void UpdateRowFor(RE::ObjectRefHandle a_container)
		{
			RE::NiPointer<RE::TESObjectREFR> hold;
			auto*                            container = Resolve(a_container, hold);
			RequestRow(container, Enabled() ? ui::DescribeContainer(container) : ui::ContainerRole{});
		}

		void OnModifyButtonBar(QL::ModifyButtonBarEvent* a_event)
		{
			if (a_event) {
				UpdateRowFor(a_event->container);
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
				UpdateRowFor(a_event->container);
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
			// Callers run on task threads; QuickLoot's menu lives on the UI thread.
			SKSE::GetTaskInterface()->AddUITask([]() {
				logger::trace("QuickLoot: refresh requested");
				QL::QuickLootAPI::RefreshLootMenu();
			});
		}
	}
}
