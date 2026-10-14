#include "UI/Overlay.h"

#include "Game/FormUtil.h"
#include "Home/HomeRegistry.h"
#include "Icons/IconService.h"
#include "Settings.h"
#include "UI/MenuFramework.h"

namespace dova::ui::overlay
{
	namespace
	{
		constexpr auto        kRootName = "DovaStashOverlay";
		constexpr float       kRescanSeconds = 0.25f;  // which containers are shown
		constexpr float       kIconTTLSeconds = 3.0f;  // how long a container's icon row is cached
		constexpr std::size_t kMaxIcons = 5;           // per container, like QuickLoot's row; then "+N"

		struct CachedRow
		{
			icons::IconRow row;
			float          age{ 0.0f };
		};

		struct Target
		{
			RE::FormID     formID{ 0 };  // the container
			RE::FormID     anchor{ 0 };  // where the icons go: the container, or the activator that opens it
			icons::IconRow row;
			float          distance{ 0.0f };
		};

		// One loaded icon inside a container's row.
		struct IconState
		{
			core::IconRef applied;     // what the clip currently shows
			bool          loading{ false };
			int           loadFrames{ 0 };  // frames spent waiting for loadMovie
			bool          labelSet{ false };
			double        baseWidth{ 0.0 };
			double        baseHeight{ 0.0 };
		};

		// One HUD clip per shown container ("s0".."sN" under kRootName), reused across frames. Inside it:
		// "i0".."i4" (one icon each) and "more" (the "+N" text).
		struct Slot
		{
			RE::FormID             formID{ 0 };
			std::vector<IconState> icons;
		};

		struct State
		{
			std::vector<Target>                       targets;
			std::vector<Slot>                         slots;
			std::unordered_map<RE::FormID, CachedRow> rowCache;
			std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> lastSeen;  // line-of-sight hysteresis
			float                                     sinceRescan{ kRescanSeconds };
			bool                                      visible{ false };
		};
		State g_state;

		bool OverlayAllowed()
		{
			auto* player = game::Player();
			if (!player || !player->GetParentCell() || player->IsInCombat()) {
				return false;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused()) {
				return false;
			}
			for (const auto& menu : ui->menuStack) {
				if (menu && menu->menuFlags.any(RE::IMenu::Flag::kPausesGame, RE::IMenu::Flag::kUsesCursor, RE::IMenu::Flag::kModal)) {
					return false;
				}
			}
			if (menu_framework::IsAnyBlockingWindowOpen()) {
				return false;  // an SKSE Menu Framework window (our editor, or another mod's) has the screen
			}
			return !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) && !ui->IsMenuOpen(RE::Console::MENU_NAME);
		}

		std::string Describe(const icons::IconRow& a_row)
		{
			std::string out;
			for (const auto& icon : a_row.icons) {
				out += (out.empty() ? "" : ",") + icon.label;
			}
			if (a_row.extra) {
				out += std::format(",+{}", a_row.extra);
			}
			return out;
		}

		icons::IconRow RowFor(const core::Layout& a_layout, RE::TESObjectREFR* a_ref)
		{
			auto& cached = g_state.rowCache[a_ref->GetFormID()];
			if (cached.age <= 0.0f || cached.age > kIconTTLSeconds) {
				auto row = icons::RowForContainer(a_layout, a_ref, kMaxIcons);
				if (!(row == cached.row)) {
					logger::debug("Overlay: {} icons {}", game::Describe(a_ref), Describe(row));
				}
				cached.row = std::move(row);
				cached.age = 0.0001f;
			}
			return cached.row;
		}

		void Rescan()
		{
			g_state.targets.clear();
			const auto current = home::HomeRegistry::Get().Display();
			auto*      player = game::Player();
			if (!current || !current->layout || !player) {
				return;
			}
			const auto  settings = Settings::GetSingleton().GetOverlay();
			const auto& layout = *current->layout;
			const auto  origin = player->GetPosition();

			auto consider = [&](const core::FormRef& a_ref) {
				auto* container = game::LookupRef(a_ref);
				if (!container || container->IsDisabled()) {
					return;
				}
				// A hidden chest behind a shelf: the icons go on the shelf that opens it.
				auto* ref = container;
				if (const auto anchor = current->Anchor(container->GetFormID()); anchor != container->GetFormID()) {
					auto* form = RE::TESForm::LookupByID(anchor);
					ref = form ? form->AsReference() : nullptr;
				}
				if (!ref || ref->IsDisabled() || !ref->Get3D() || !ref->GetParentCell() || ref->GetParentCell()->GetFormID() != current->cellId) {
					return;
				}
				const float distance = origin.GetDistance(ref->GetPosition());
				if (distance > settings.radius) {
					return;
				}
				if (settings.requireLineOfSight) {
					// The engine's line-of-sight test flickers for containers (bounds partly behind trim or
					// furniture), which made icons blink. Keep an icon for a second after the last "seen".
					bool       unused = false;
					const auto now = std::chrono::steady_clock::now();
					if (player->HasLineOfSight(ref, unused)) {
						g_state.lastSeen[ref->GetFormID()] = now;
					} else if (const auto it = g_state.lastSeen.find(ref->GetFormID());
							   it == g_state.lastSeen.end() || now - it->second > std::chrono::milliseconds(1000)) {
						return;
					}
				}
				auto row = RowFor(layout, container);
				if (row.Empty()) {
					return;
				}
				g_state.targets.push_back({ container->GetFormID(), ref->GetFormID(), std::move(row), distance });
			};
			if (layout.master) {
				consider(*layout.master);
			}
			for (const auto& c : layout.containers) {
				if (!c.ignored) {
					consider(c.ref);
				}
			}
			std::ranges::sort(g_state.targets, {}, &Target::distance);
			if (g_state.targets.size() > static_cast<std::size_t>(settings.maxIcons)) {
				g_state.targets.resize(static_cast<std::size_t>(settings.maxIcons));
			}

			// Keep every container in the slot it already has: the order by distance changes constantly as
			// the player moves, and a slot that changes container must reload its icon SWFs.
			{
				std::vector<std::optional<Target>> placed(g_state.targets.size());
				std::vector<Target>                rest;
				for (auto& t : g_state.targets) {
					bool kept = false;
					for (std::size_t i = 0; i < placed.size() && i < g_state.slots.size(); ++i) {
						if (!placed[i] && g_state.slots[i].formID == t.formID) {
							placed[i] = std::move(t);
							kept = true;
							break;
						}
					}
					if (!kept) {
						rest.push_back(std::move(t));
					}
				}
				auto next = rest.begin();
				g_state.targets.clear();
				for (auto& p : placed) {
					g_state.targets.push_back(p ? std::move(*p) : std::move(*next++));
				}
			}

			static std::string       lastSet;
			std::vector<std::string> entries;
			for (const auto& t : g_state.targets) {
				entries.push_back(std::format("{:08X}={}", t.formID, Describe(t.row)));
			}
			std::ranges::sort(entries);  // the set, not the order
			std::string set;
			for (const auto& e : entries) {
				set += e + " ";
			}
			if (set != lastSet) {
				logger::debug("Overlay: {} container(s): {}", g_state.targets.size(), set.empty() ? "none"s : set);
				lastSet = std::move(set);
			}
		}

		// The overlay root clip on the HUD movie, created on first use (and again if the HUD movie was
		// reloaded). Looked up by name every frame: no GFxValue is kept across frames.
		bool GetRoot(RE::GFxMovieView* a_movie, RE::GFxValue& a_root)
		{
			RE::GFxValue stage;
			if (!a_movie->GetVariable(&stage, "_root") || !stage.IsDisplayObject()) {
				return false;
			}
			if (stage.GetMember(kRootName, &a_root) && a_root.IsDisplayObject()) {
				return true;
			}
			RE::GFxValue depth;
			stage.Invoke("getNextHighestDepth", &depth);
			const auto d = depth.IsNumber() ? static_cast<std::int32_t>(depth.GetNumber()) : 0x3FFF;
			if (!stage.CreateEmptyMovieClip(&a_root, kRootName, d)) {
				return false;
			}
			g_state.slots.clear();  // a new root means every slot clip is gone too
			return true;
		}

		bool GetChild(RE::GFxValue& a_parent, const std::string& a_name, std::int32_t a_depth, RE::GFxValue& a_child, bool a_create)
		{
			if (a_parent.GetMember(a_name.c_str(), &a_child) && a_child.IsDisplayObject()) {
				return true;
			}
			return a_create && a_parent.CreateEmptyMovieClip(&a_child, a_name.c_str(), a_depth);
		}

		void SetVisible(RE::GFxValue& a_clip, bool a_visible)
		{
			RE::GFxValue::DisplayInfo info;
			info.SetVisible(a_visible);
			a_clip.SetDisplayInfo(info);
		}

		double Number(RE::GFxValue& a_clip, const char* a_member)
		{
			RE::GFxValue v;
			return a_clip.GetMember(a_member, &v) && v.IsNumber() ? v.GetNumber() : 0.0;
		}

		// Loads/labels one icon clip; returns true once it shows `a_icon`.
		bool PrepareIcon(RE::GFxMovieView* a_movie, IconState& a_state, RE::GFxValue& a_iconClip, const core::IconRef& a_icon)
		{
			const std::string source = a_icon.source.empty() ? std::string(core::kSkyUIItemIcons) : a_icon.source;
			if ((!a_state.loading && !a_state.labelSet) || a_state.applied.source != source) {
				const RE::GFxValue path(source.c_str());
				a_iconClip.Invoke("loadMovie", nullptr, &path, 1);
				a_state.applied = a_icon;
				a_state.applied.source = source;
				a_state.loading = true;
				a_state.loadFrames = 0;
				a_state.labelSet = false;
				logger::debug("Overlay: loading icon SWF {}", source);
				return false;
			}
			if (a_state.loading) {
				RE::GFxValue loaded, total;
				a_iconClip.Invoke("getBytesLoaded", &loaded);
				a_iconClip.Invoke("getBytesTotal", &total);
				if (!loaded.IsNumber() || !total.IsNumber() || total.GetNumber() <= 0.0 || loaded.GetNumber() < total.GetNumber()) {
					if (++a_state.loadFrames >= 300) {
						logger::warn("Overlay: icon SWF {} still not loaded after 300 frames (missing file?); shown without it", a_state.applied.source);
						a_state.loading = false;
						a_state.labelSet = true;
						a_state.baseWidth = 0.0;
						a_state.baseHeight = 0.0;
						return true;
					}
					return false;
				}
				a_state.loading = false;
			}
			if (!a_state.labelSet || a_state.applied.label != a_icon.label || a_state.applied.color != a_icon.color) {
				const auto appliedSource = a_state.applied.source;
				a_iconClip.GotoAndStop(a_icon.label.c_str());
				if (a_icon.color) {
					// new Color(clip).setRGB(0xRRGGBB): how SkyUI tints icons.
					RE::GFxValue color;
					a_movie->CreateObject(&color, "Color", &a_iconClip, 1);
					const RE::GFxValue rgb(static_cast<double>(*a_icon.color));
					color.Invoke("setRGB", nullptr, &rgb, 1);
				}
				RE::GFxValue::DisplayInfo info;
				info.SetScale(100.0, 100.0);
				a_iconClip.SetDisplayInfo(info);
				a_state.baseWidth = Number(a_iconClip, "_width");
				a_state.baseHeight = Number(a_iconClip, "_height");
				if (a_state.baseWidth <= 0.0 || a_state.baseHeight <= 0.0) {
					logger::warn("Overlay: label '{}' in {} has no size (label missing from that SWF?)", a_icon.label, appliedSource);
				}
				a_state.applied = a_icon;
				a_state.applied.source = appliedSource;
				a_state.labelSet = true;
			}
			return true;  // shown, or known to have no size (missing label): DrawRow skips those
		}

		// "+N" text, drawn with the game's own font alias so the HUD skin's fontconfig applies.
		bool GetMoreText(RE::GFxValue& a_slot, RE::GFxValue& a_text)
		{
			if (a_slot.GetMember("more", &a_text) && a_text.IsDisplayObject()) {
				return true;
			}
			RE::GFxValue args[6]{ RE::GFxValue("more"), RE::GFxValue(100.0), RE::GFxValue(0.0), RE::GFxValue(0.0), RE::GFxValue(80.0), RE::GFxValue(40.0) };
			a_slot.Invoke("createTextField", nullptr, args, 6);
			if (!a_slot.GetMember("more", &a_text) || !a_text.IsDisplayObject()) {
				return false;
			}
			a_text.SetMember("embedFonts", RE::GFxValue(true));
			a_text.SetMember("html", RE::GFxValue(true));
			a_text.SetMember("selectable", RE::GFxValue(false));
			a_text.SetMember("autoSize", RE::GFxValue("left"));
			return true;
		}

		// Screen anchor: just above the top of the reference's bounds, in HUD movie coordinates.
		bool Project(RE::TESObjectREFR* a_ref, const RE::GRectF& a_rect, float& a_x, float& a_y)
		{
			auto* node = a_ref->Get3D();
			auto* camera = RE::Main::WorldRootCamera();
			if (!node || !camera) {
				return false;
			}
			auto point = node->worldBound.center;
			point.z += node->worldBound.radius * 0.8f;
			float x = 0.0f, y = 0.0f, z = 0.0f;
			const auto& port = camera->GetRuntimeData2().port;
			if (!RE::NiCamera::WorldPtToScreenPt3(camera->GetRuntimeData().worldToCam, port, point, x, y, z, 1e-5f) || z < 0.0f) {
				return false;
			}
			if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) {
				return false;
			}
			y = 1.0f - y;  // Flash's y axis points down
			a_x = a_rect.left + (a_rect.right - a_rect.left) * x;
			a_y = a_rect.top + (a_rect.bottom - a_rect.top) * y;
			return true;
		}

		void Hide(RE::GFxValue& a_root)
		{
			if (g_state.visible) {
				SetVisible(a_root, false);
				g_state.visible = false;
			}
		}

		// Lays out one container's row, centred above it: icon icon icon +N. Returns false (slot hidden)
		// until every icon in the row has loaded, so the row never jumps around while it fills.
		bool DrawRow(RE::GFxMovieView* a_movie, RE::GFxValue& a_slotClip, Slot& a_slot, const Target& a_target, float a_x, float a_y,
			double a_size)
		{
			const auto& icons = a_target.row.icons;
			a_slot.icons.resize(std::max(a_slot.icons.size(), icons.size()));
			std::vector<RE::GFxValue> clips(a_slot.icons.size());
			bool                      ready = true;
			for (std::size_t k = 0; k < a_slot.icons.size(); ++k) {
				const bool used = k < icons.size();
				if (!GetChild(a_slotClip, std::format("i{}", k), static_cast<std::int32_t>(k), clips[k], used)) {
					ready = ready && !used;
					continue;
				}
				if (!used) {
					SetVisible(clips[k], false);
					continue;
				}
				ready = PrepareIcon(a_movie, a_slot.icons[k], clips[k], icons[k]) && ready;
			}
			if (!ready) {
				return false;
			}

			const double gap = a_size * 0.25;
			RE::GFxValue more;
			double       moreWidth = 0.0;
			const bool   hasMore = a_target.row.extra > 0 && GetMoreText(a_slotClip, more);
			if (hasMore) {
				const auto px = static_cast<int>(std::lround(a_size * 0.6));
				const auto html = std::format("<font face='$EverywhereMediumFont' size='{}' color='#FFFFFF'>+{}</font>", px, a_target.row.extra);
				more.SetTextHTML(html.c_str());
				moreWidth = Number(more, "textWidth") + 4.0;
			} else if (a_slotClip.GetMember("more", &more) && more.IsDisplayObject()) {
				SetVisible(more, false);
			}

			std::size_t  shown = 0;
			for (std::size_t k = 0; k < icons.size(); ++k) {
				if (a_slot.icons[k].baseWidth > 0.0 && a_slot.icons[k].baseHeight > 0.0) {
					++shown;
				}
			}
			const double total = shown * a_size + (shown > 1 ? (shown - 1) * gap : 0.0) + (hasMore ? gap + moreWidth : 0.0);
			double       cursor = -total / 2.0;
			for (std::size_t k = 0; k < icons.size(); ++k) {
				const auto&  st = a_slot.icons[k];
				if (st.baseWidth <= 0.0 || st.baseHeight <= 0.0) {
					SetVisible(clips[k], false);  // no such label in that SWF: skip it, show the rest
					continue;
				}
				const double scale = 100.0 * a_size / std::max(st.baseWidth, st.baseHeight);
				const double w = st.baseWidth * scale / 100.0, h = st.baseHeight * scale / 100.0;
				RE::GFxValue::DisplayInfo info;
				info.SetScale(scale, scale);
				info.SetPosition(cursor + (a_size - w) / 2.0, -h);  // icons sit on the row's baseline
				info.SetVisible(true);
				clips[k].SetDisplayInfo(info);
				cursor += a_size + gap;
			}
			if (hasMore) {
				RE::GFxValue::DisplayInfo info;
				info.SetPosition(cursor, -a_size / 2.0 - Number(more, "_height") / 2.0);
				info.SetVisible(true);
				more.SetDisplayInfo(info);
			}
			RE::GFxValue::DisplayInfo info;
			info.SetPosition(a_x, a_y);
			a_slotClip.SetDisplayInfo(info);
			return true;
		}

		void Update(RE::HUDMenu* a_menu, float a_interval)
		{
			auto* movie = a_menu ? a_menu->uiMovie.get() : nullptr;
			if (!movie) {
				return;
			}
			RE::GFxValue root;
			if (!GetRoot(movie, root)) {
				return;
			}
			const auto settings = Settings::GetSingleton().GetOverlay();
			if (!settings.enabled || !OverlayAllowed()) {
				Hide(root);
				return;
			}

			for (auto& [id, cached] : g_state.rowCache) {
				if (cached.age > 0.0f) {
					cached.age += a_interval;
				}
			}
			g_state.sinceRescan += a_interval;
			if (g_state.sinceRescan >= kRescanSeconds) {
				g_state.sinceRescan = 0.0f;
				Rescan();
			}
			if (g_state.targets.empty()) {
				Hide(root);
				return;
			}
			if (!g_state.visible) {
				SetVisible(root, true);
				g_state.visible = true;
			}

			const auto rect = movie->GetVisibleFrameRect();
			if (g_state.slots.size() < g_state.targets.size()) {
				g_state.slots.resize(g_state.targets.size());
			}
			for (std::size_t i = 0; i < g_state.slots.size(); ++i) {
				RE::GFxValue slotClip;
				const bool   active = i < g_state.targets.size();
				if (!GetChild(root, std::format("s{}", i), static_cast<std::int32_t>(i), slotClip, active)) {
					continue;
				}
				if (!active) {
					SetVisible(slotClip, false);
					continue;
				}
				auto&       slot = g_state.slots[i];
				const auto& target = g_state.targets[i];
				if (slot.formID != target.formID) {
					slot.formID = target.formID;
				}
				auto* form = RE::TESForm::LookupByID(target.anchor);
				auto* ref = form ? form->AsReference() : nullptr;
				float x = 0.0f, y = 0.0f;
				// Shrinks to half size and fades to 40% at the edge of the radius.
				const float  t = std::clamp(target.distance / settings.radius, 0.0f, 1.0f);
				const double size = settings.iconSize * (1.0 - 0.5 * t);
				if (!ref || !Project(ref, rect, x, y) || !DrawRow(movie, slotClip, slot, target, x, y, size)) {
					SetVisible(slotClip, false);
					continue;
				}
				RE::GFxValue::DisplayInfo info;
				info.SetAlpha(100.0 - 60.0 * t);
				info.SetVisible(true);
				slotClip.SetDisplayInfo(info);
			}
		}

		struct AdvanceMovie
		{
			static void Thunk(RE::HUDMenu* a_this, float a_interval, std::uint32_t a_currentTime)
			{
				_original(a_this, a_interval, a_currentTime);
				Update(a_this, a_interval);
			}
			static inline REL::Relocation<decltype(Thunk)> _original;
		};
	}

	void Install()
	{
		// Installed even when disabled: [Overlay] bEnabled is read every frame so it can be toggled live.
		if (REL::Module::IsVR()) {
			logger::info("Overlay: not available on VR");
			return;
		}
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_HUDMenu[0] };
		AdvanceMovie::_original = vtbl.write_vfunc(0x5, AdvanceMovie::Thunk);
		logger::info("Overlay: HUDMenu::AdvanceMovie hooked");
	}
}
