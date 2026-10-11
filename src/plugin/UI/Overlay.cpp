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
		constexpr auto  kRootName = "DovaStashOverlay";
		constexpr float kRescanSeconds = 0.25f;  // which containers are shown
		constexpr float kIconTTLSeconds = 3.0f;  // how long a container's icon choice is cached

		struct CachedIcon
		{
			core::IconRef icon;
			float         age{ 0.0f };
		};

		struct Target
		{
			RE::FormID    formID{ 0 };
			core::IconRef icon;
			float         distance{ 0.0f };
		};

		// One HUD clip per shown icon ("s0".."sN" under kRootName), reused across frames.
		struct Slot
		{
			RE::FormID    formID{ 0 };
			core::IconRef applied;     // what the clip currently shows
			bool          loading{ false };
			int           loadFrames{ 0 };  // frames spent waiting for loadMovie
			bool          labelSet{ false };
			double        baseWidth{ 0.0 };
			double        baseHeight{ 0.0 };
		};

		struct State
		{
			std::vector<Target>                        targets;
			std::vector<Slot>                          slots;
			std::unordered_map<RE::FormID, CachedIcon> iconCache;
			float                                      sinceRescan{ kRescanSeconds };
			bool                                       visible{ false };
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

		core::IconRef IconFor(const core::Layout& a_layout, RE::TESObjectREFR* a_ref)
		{
			auto& cached = g_state.iconCache[a_ref->GetFormID()];
			if (cached.age <= 0.0f || cached.age > kIconTTLSeconds) {
				auto icon = icons::ForContainer(a_layout, a_ref);
				if (!(icon == cached.icon)) {
					logger::debug("Overlay: {} icon {} | {}{}", game::Describe(a_ref), icon.source, icon.label,
						icon.color ? std::format(" #{:06X}", *icon.color) : ""s);
				}
				cached.icon = std::move(icon);
				cached.age = 0.0001f;
			}
			return cached.icon;
		}

		void Rescan()
		{
			g_state.targets.clear();
			auto* current = home::HomeRegistry::Get().Peek();
			auto* player = game::Player();
			if (!current || !current->layout || !player) {
				return;
			}
			const auto& settings = Settings::GetSingleton().GetOverlay();
			const auto& layout = *current->layout;
			const auto  origin = player->GetPosition();

			auto consider = [&](const core::FormRef& a_ref) {
				auto* ref = game::LookupRef(a_ref);
				if (!ref || ref->IsDisabled() || !ref->Get3D() || !ref->GetParentCell() || ref->GetParentCell()->GetFormID() != current->cellId) {
					return;
				}
				const float distance = origin.GetDistance(ref->GetPosition());
				if (distance > settings.radius) {
					return;
				}
				if (settings.requireLineOfSight) {
					bool unused = false;
					if (!player->HasLineOfSight(ref, unused)) {
						return;
					}
				}
				auto icon = IconFor(layout, ref);
				if (icon.Empty()) {
					return;
				}
				g_state.targets.push_back({ ref->GetFormID(), std::move(icon), distance });
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

			static std::string lastSet;
			std::string        set;
			for (const auto& t : g_state.targets) {
				set += std::format("{:08X}={}|{} ", t.formID, t.icon.source, t.icon.label);
			}
			if (set != lastSet) {
				logger::debug("Overlay: {} icon(s): {}", g_state.targets.size(), set.empty() ? "none"s : set);
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

		bool GetSlotClips(RE::GFxValue& a_root, std::size_t a_index, RE::GFxValue& a_slot, RE::GFxValue& a_icon, bool a_create)
		{
			const auto name = std::format("s{}", a_index);
			if (!a_root.GetMember(name.c_str(), &a_slot) || !a_slot.IsDisplayObject()) {
				if (!a_create || !a_root.CreateEmptyMovieClip(&a_slot, name.c_str(), static_cast<std::int32_t>(a_index))) {
					return false;
				}
			}
			if (!a_slot.GetMember("icon", &a_icon) || !a_icon.IsDisplayObject()) {
				if (!a_create || !a_slot.CreateEmptyMovieClip(&a_icon, "icon", 0)) {
					return false;
				}
			}
			return true;
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

		// Loads/labels the icon clip; returns true once it shows `a_icon`.
		bool PrepareIcon(RE::GFxMovieView* a_movie, Slot& a_slot, RE::GFxValue& a_iconClip, const core::IconRef& a_icon)
		{
			const std::string source = a_icon.source.empty() ? std::string(core::kSkyUIItemIcons) : a_icon.source;
			if ((!a_slot.loading && !a_slot.labelSet) || a_slot.applied.source != source) {
				const RE::GFxValue path(source.c_str());
				a_iconClip.Invoke("loadMovie", nullptr, &path, 1);
				a_slot.applied = a_icon;
				a_slot.applied.source = source;
				a_slot.loading = true;
				a_slot.loadFrames = 0;
				a_slot.labelSet = false;
				logger::debug("Overlay: loading icon SWF {}", source);
				return false;
			}
			if (a_slot.loading) {
				RE::GFxValue loaded, total;
				a_iconClip.Invoke("getBytesLoaded", &loaded);
				a_iconClip.Invoke("getBytesTotal", &total);
				if (!loaded.IsNumber() || !total.IsNumber() || total.GetNumber() <= 0.0 || loaded.GetNumber() < total.GetNumber()) {
					if (++a_slot.loadFrames == 300) {
						logger::warn("Overlay: icon SWF {} still not loaded after 300 frames (missing file?)", a_slot.applied.source);
					}
					return false;
				}
				a_slot.loading = false;
			}
			if (!a_slot.labelSet || a_slot.applied.label != a_icon.label || a_slot.applied.color != a_icon.color) {
				const auto appliedSource = a_slot.applied.source;
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
				a_slot.baseWidth = Number(a_iconClip, "_width");
				a_slot.baseHeight = Number(a_iconClip, "_height");
				if (a_slot.baseWidth <= 0.0 || a_slot.baseHeight <= 0.0) {
					logger::warn("Overlay: label '{}' in {} has no size (label missing from that SWF?)", a_icon.label, appliedSource);
				} else {
					logger::trace("Overlay: label '{}' in {} is {}x{}", a_icon.label, appliedSource, a_slot.baseWidth, a_slot.baseHeight);
				}
				a_slot.applied = a_icon;
				a_slot.applied.source = appliedSource;
				a_slot.labelSet = true;
			}
			return a_slot.baseWidth > 0.0 && a_slot.baseHeight > 0.0;
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
			if (!Settings::GetSingleton().GetOverlay().enabled || !OverlayAllowed()) {
				Hide(root);
				return;
			}

			for (auto& [id, cached] : g_state.iconCache) {
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

			const auto& settings = Settings::GetSingleton().GetOverlay();
			const auto  rect = movie->GetVisibleFrameRect();
			if (g_state.slots.size() < g_state.targets.size()) {
				g_state.slots.resize(g_state.targets.size());
			}
			for (std::size_t i = 0; i < g_state.slots.size(); ++i) {
				RE::GFxValue slotClip, iconClip;
				const bool   active = i < g_state.targets.size();
				if (!GetSlotClips(root, i, slotClip, iconClip, active)) {
					continue;
				}
				if (!active) {
					SetVisible(slotClip, false);
					continue;
				}
				auto&       slot = g_state.slots[i];
				const auto& target = g_state.targets[i];
				auto*       form = RE::TESForm::LookupByID(target.formID);
				auto*       ref = form ? form->AsReference() : nullptr;
				float       x = 0.0f, y = 0.0f;
				if (!ref || !PrepareIcon(movie, slot, iconClip, target.icon) || !Project(ref, rect, x, y)) {
					SetVisible(slotClip, false);
					continue;
				}
				slot.formID = target.formID;

				// Shrinks to half size and fades to 40% at the edge of the radius.
				const float t = std::clamp(target.distance / settings.radius, 0.0f, 1.0f);
				const double size = settings.iconSize * (1.0 - 0.5 * t);
				const double scale = 100.0 * size / std::max(slot.baseWidth, slot.baseHeight);
				const double width = slot.baseWidth * scale / 100.0;
				const double height = slot.baseHeight * scale / 100.0;

				RE::GFxValue::DisplayInfo iconInfo;
				iconInfo.SetScale(scale, scale);
				iconClip.SetDisplayInfo(iconInfo);

				RE::GFxValue::DisplayInfo info;
				info.SetPosition(x - width / 2.0, y - height);
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
