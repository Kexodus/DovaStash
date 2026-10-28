#include "UI/UIExtensions.h"

#include "Settings.h"

#include <cmath>
#include <mutex>
#include <thread>

namespace dova::ui::uiext
{
	namespace
	{
		using Control = RE::BSEventNotifyControl;

		// The UIExtensions contracts we use: UIListMenu.psc and UITextEntryMenu.psc. The list extras
		// (icons, stay-open entries, logo; see UIExtensions.h) are optional on top of them.
		constexpr auto        kCustomMenu = "CustomMenu"sv;  // SKSE's custom menu that hosts the SWFs
		constexpr std::size_t kMaxEntries = kMaxListEntries;  // UIListMenu.psc buffers 128 entries; skins may assume it

		enum class Kind : std::uint8_t
		{
			List,
			Text
		};

		struct Contract
		{
			std::string_view swf;     // UI.OpenCustomMenu path
			std::string_view root;    // *.psc MENU_ROOT
			std::string_view load;    // mod events
			std::string_view result;
			std::string_view close;
		};
		constexpr Contract kList{ "listmenu"sv, "_root.listMenu."sv, "UIListMenu_LoadMenu"sv, "UIListMenu_SelectItem"sv, "UIListMenu_CloseMenu"sv };
		constexpr Contract kText{ "textentrymenu"sv, "_root.textEntry."sv, "UITextEntryMenu_LoadMenu"sv, "UITextEntryMenu_TextChanged"sv, "UITextEntryMenu_CloseMenu"sv };

		const Contract& ContractOf(Kind a_kind) { return a_kind == Kind::List ? kList : kText; }

		// State of the one menu we may have open. Touched from three places: SKSE tasks (worker threads on
		// AE), UI tasks and the mod-event sink (the UI thread). Every access goes through g_lock; callbacks
		// always run with the lock released (they may open the next menu).
		//
		// Two ids keep events with the right list. `serial` is our request (OpenList call); `instance`
		// counts menus that actually loaded (UIListMenu_LoadMenu). Select/close events carry the instance
		// that was on screen, so the close event of the previous list (which often arrives after we have
		// already asked for the next one, e.g. when a choice opens the next list) can never cancel the new
		// request.
		struct Pending
		{
			bool                   open{ false };
			Kind                   kind{ Kind::List };
			std::uint64_t          serial{ 0 };
			std::uint64_t          instance{ 0 };  // 0 until this request's menu has loaded
			std::vector<ListEntry> entries;        // List
			ChoiceCallback         callback;       // List
			ListOptions            options;        // List
			std::string            text;           // Text: initial text
			TextCallback           textCallback;   // Text
		};
		std::mutex    g_lock;
		Pending       g_pending;
		std::uint64_t g_serial{ 0 };
		std::uint64_t g_instance{ 0 };  // sink side: last loaded menu

		// Ends the pending request if `a_should(g_pending)` holds: list index (-1 = cancelled) or entered
		// text (nullopt = cancelled). The callback runs after the lock is released.
		template <class Pred>
		void FinishIf(Pred&& a_should, std::int32_t a_index, std::optional<std::string> a_text = std::nullopt)
		{
			Pending pending;
			{
				std::scoped_lock guard(g_lock);
				if (!a_should(g_pending)) {
					return;
				}
				pending = std::move(g_pending);
				g_pending = {};
			}
			if (pending.kind == Kind::List) {
				logger::debug("UIExtensions: list closed, choice {}", a_index);
				if (pending.callback) {
					pending.callback(a_index);
				}
			} else {
				logger::debug("UIExtensions: text entry closed, {}", a_text ? std::format("\"{}\"", *a_text) : "cancelled"s);
				if (pending.textCallback) {
					pending.textCallback(std::move(a_text));
				}
			}
		}

		// A result/close event from the menu instance `a_instance`.
		void Complete(std::uint64_t a_instance, std::int32_t a_index, std::optional<std::string> a_text = std::nullopt)
		{
			FinishIf([a_instance](const Pending& p) { return p.open && p.instance != 0 && p.instance == a_instance; }, a_index, std::move(a_text));
		}

		// Invokes `a_fn` with every element of `a_args` as a separate argument, like SKSE's UI.Invoke*A
		// (what UIListMenu.psc uses).
		void Invoke(RE::GFxMovieView* a_movie, const Contract& a_contract, std::string_view a_fn, std::span<const RE::GFxValue> a_args)
		{
			const auto path = std::format("{}{}", a_contract.root, a_fn);
			if (!a_movie->Invoke(path.c_str(), nullptr, a_args.data(), static_cast<std::uint32_t>(a_args.size()))) {
				logger::warn("UIExtensions: {} could not be invoked ({}.swf replaced by something incompatible?)", path, a_contract.swf);
			}
		}

		std::string Sanitize(std::string_view a_text)
		{
			// Entries are encoded as "name;;parent;;id;;callback;;children" and split on ";;".
			std::string out{ a_text };
			for (std::size_t pos; (pos = out.find(";;")) != std::string::npos;) {
				out.replace(pos, 2, "; ");
			}
			if (out.ends_with(';')) {
				out += ' ';  // "name;" + ";;" would split as "name" / ";parent"
			}
			return out;
		}

		void Later(std::chrono::milliseconds a_delay, std::function<void()> a_task)
		{
			std::thread([a_delay, task = std::move(a_task)]() mutable {
				std::this_thread::sleep_for(a_delay);
				SKSE::GetTaskInterface()->AddTask(std::move(task));
			}).detach();
		}

		RE::GPtr<RE::GFxMovieView> CustomMenuMovie()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui ? ui->GetMovieView(kCustomMenu) : nullptr;
		}

		bool IsCurrent(std::uint64_t a_instance)
		{
			std::scoped_lock guard(g_lock);
			return g_pending.open && g_pending.instance == a_instance;
		}

		// ---- List extras (UIExtensions.h): icons, stay-open entries, logo ------------------------------
		//
		// Built only from what listmenu.swf's own classes have (ListMenu and SkyUI's ScrollingList; the
		// stock SWF and Nordic UI's have identical ActionScript). Anything missing turns that extra off.
		// No GFxValue is kept across calls: everything is looked up from _root each time, and the state
		// lives in members on the menu's own objects ("dovaStash..."). Never touched with g_lock held:
		// the hooks below run inside the menu's ActionScript and take the lock themselves.

		constexpr auto kLogoPath = "DovaStash/logo.dds"sv;  // Data/Interface/DovaStash/logo.dds
		constexpr int  kDecorTries = 120;                    // ~2 s for icon SWFs and the logo to load
		constexpr auto kDecorRetry = std::chrono::milliseconds(16);

		// Members we add. On the menu objects:
		constexpr auto kSavedItemPressed = "dovaStashItemPressed";  // _root.listMenu: the SWF's onItemPressed
		constexpr auto kSavedUpdateList = "dovaStashUpdateList";    // itemList: the SWF's UpdateList
		constexpr auto kHasIcons = "dovaStashIcons";                // itemList: rows have an icon column
		constexpr auto kLogoClip = "dovaStashLogo";                 // _root.listMenu: the logo holder
		constexpr auto kLogoSize = "dovaStashSize";                 // logo holder
		constexpr auto kLogoDone = "dovaStashDone";                 // logo holder: true shown, false given up
		// On each entry object (itemList.entryList[i]): the entry's icon.
		constexpr auto kIconSource = "dovaStashIconSource";
		constexpr auto kIconLabel = "dovaStashIconLabel";
		constexpr auto kIconColor = "dovaStashIconColor";
		// On each row clip (pooled, reused for whatever entry it shows). The icon clip's own members do not
		// survive loadMovie, so its state is kept here.
		constexpr auto kIconClip = "dovaStashIcon";
		constexpr auto kTextX = "dovaStashTextX";  // the text field's x and width as the skin laid them out
		constexpr auto kTextW = "dovaStashTextW";
		constexpr auto kLoadedSource = "dovaStashSource";  // SWF loaded into the icon clip
		constexpr auto kUrlBefore = "dovaStashUrlBefore";  // the icon clip's _url before that loadMovie
		constexpr auto kShownIcon = "dovaStashShown";      // "label|color" it shows
		constexpr auto kIconW = "dovaStashIconW";          // that frame's size at 100%
		constexpr auto kIconH = "dovaStashIconH";

		double NumberMember(const RE::GFxValue& a_object, const char* a_name, double a_default = 0.0)
		{
			RE::GFxValue v;
			return a_object.GetMember(a_name, &v) && v.IsNumber() ? v.GetNumber() : a_default;
		}

		std::string StringMember(const RE::GFxValue& a_object, const char* a_name)
		{
			RE::GFxValue v;
			return a_object.GetMember(a_name, &v) && v.IsString() && v.GetString() ? v.GetString() : std::string{};
		}

		// Strings stored in ActionScript objects are created by the movie (copied), never pointing at ours.
		void SetStringMember(RE::GFxMovie* a_movie, RE::GFxValue& a_object, const char* a_name, const std::string& a_value)
		{
			RE::GFxValue v;
			a_movie->CreateString(&v, a_value.c_str());
			a_object.SetMember(a_name, v);
		}

		void SetVisible(RE::GFxValue& a_clip, bool a_visible)
		{
			RE::GFxValue::DisplayInfo info;
			info.SetVisible(a_visible);
			a_clip.SetDisplayInfo(info);
		}

		// new Color(clip).setRGB(rgb), or the identity transform for none (how SkyUI tints icons).
		void Tint(RE::GFxMovie* a_movie, RE::GFxValue& a_clip, std::optional<double> a_rgb)
		{
			RE::GFxValue color;
			a_movie->CreateObject(&color, "Color", &a_clip, 1);
			if (!color.IsObject()) {
				return;
			}
			if (a_rgb) {
				const RE::GFxValue rgb(*a_rgb);
				color.Invoke("setRGB", nullptr, &rgb, 1);
				return;
			}
			RE::GFxValue identity;
			a_movie->CreateObject(&identity);
			for (const auto* m : { "ra", "ga", "ba", "aa" }) {
				identity.SetMember(m, RE::GFxValue(100.0));
			}
			for (const auto* m : { "rb", "gb", "bb", "ab" }) {
				identity.SetMember(m, RE::GFxValue(0.0));
			}
			color.Invoke("setTransform", nullptr, &identity, 1);
		}

		// The SWF loaded into `a_clip` has finished loading and replaced the clip's previous content
		// (which still reports itself loaded until then): its MovieClip._url names `a_source`, or at least
		// is no longer `a_urlBefore` (the _url before loadMovie).
		bool HasLoaded(const RE::GFxValue& a_clip, std::string_view a_source, std::string_view a_urlBefore)
		{
			RE::GFxValue clip = a_clip, loaded, total;
			clip.Invoke("getBytesLoaded", &loaded);
			clip.Invoke("getBytesTotal", &total);
			if (!loaded.IsNumber() || !total.IsNumber() || total.GetNumber() <= 0.0 || loaded.GetNumber() < total.GetNumber()) {
				return false;
			}
			auto url = StringMember(clip, "_url");
			if (url.empty()) {
				return true;  // no _url to compare: trust the byte counts
			}
			std::ranges::replace(url, '\\', '/');
			std::string want{ a_source };
			std::ranges::replace(want, '\\', '/');
			const bool names = url.size() >= want.size() && core::IEquals(std::string_view(url).substr(url.size() - want.size()), want);
			return names || url != a_urlBefore;
		}

		bool GetList(RE::GFxMovie* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_list)
		{
			return a_movie->GetVariable(&a_menu, "_root.listMenu") && a_menu.IsDisplayObject() && a_menu.GetMember("itemList", &a_list) &&
			       a_list.IsDisplayObject();
		}

		// The list's entry objects (what LM_AddTreeEntries pushed): entryList, or its backing _entryList.
		bool GetEntryArray(const RE::GFxValue& a_list, RE::GFxValue& a_array)
		{
			return (a_list.GetMember("entryList", &a_array) && a_array.IsArray()) || (a_list.GetMember("_entryList", &a_array) && a_array.IsArray());
		}

		// Our entry behind a row's entry object (its id is our index), or nullptr.
		const ListEntry* EntryOf(const RE::GFxValue& a_object, const std::vector<ListEntry>& a_entries)
		{
			const auto id = NumberMember(a_object, "id", -1.0);
			return id >= 0.0 && id < static_cast<double>(a_entries.size()) ? &a_entries[static_cast<std::size_t>(id)] : nullptr;
		}

		// Copies each entry's icon (and with `a_text` its text) onto its entry object.
		void Annotate(RE::GFxMovie* a_movie, RE::GFxValue& a_list, const std::vector<ListEntry>& a_entries, bool a_text)
		{
			RE::GFxValue array;
			if (!GetEntryArray(a_list, array)) {
				return;
			}
			for (std::uint32_t i = 0; i < array.GetArraySize(); ++i) {
				RE::GFxValue object;
				if (!array.GetElement(i, &object) || !object.IsObject()) {
					continue;
				}
				const auto* entry = EntryOf(object, a_entries);
				if (!entry) {
					continue;
				}
				if (a_text) {
					SetStringMember(a_movie, object, "text", Sanitize(entry->text));
				}
				const auto& icon = entry->icon;
				SetStringMember(a_movie, object, kIconSource, icon.Empty() ? std::string{} : icon.source.empty() ? std::string(core::kSkyUIItemIcons) : icon.source);
				SetStringMember(a_movie, object, kIconLabel, icon.label);
				object.SetMember(kIconColor, icon.color ? RE::GFxValue(static_cast<double>(*icon.color)) : RE::GFxValue());
			}
		}

		// Adds the entries, as UIListMenu.psc OnLoadMenu does ("name;;parent;;id;;callback;;children"; id
		// and callback are both the entry index, the select event reports the id), then their icons.
		void AddEntries(RE::GFxMovie* a_movie, const std::vector<ListEntry>& a_entries)
		{
			// Strings outlive the Invoke.
			std::vector<std::string> encoded;
			encoded.reserve(a_entries.size());
			for (std::size_t i = 0; i < a_entries.size(); ++i) {
				const auto& e = a_entries[i];
				encoded.push_back(std::format("{};;{};;{};;{};;{}", Sanitize(e.text), e.parent, i, i, e.hasChildren ? 1 : 0));
			}
			std::vector<RE::GFxValue> args;
			args.reserve(encoded.size());
			for (const auto& s : encoded) {
				args.emplace_back(s.c_str());
			}
			const auto path = std::format("{}LM_AddTreeEntries", kList.root);
			if (!a_movie->Invoke(path.c_str(), nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
				logger::warn("UIExtensions: {} could not be invoked ({}.swf replaced by something incompatible?)", path, kList.swf);
				return;
			}
			RE::GFxValue menu, list;
			if (GetList(a_movie, menu, list)) {
				Annotate(a_movie, list, a_entries, false);
			}
		}

		// ---- Icons ----

		// Loads `a_source` into the row's icon clip and shows frame `a_label` fitted into an `a_box` square
		// at (a_x, a_y). False while the SWF is still loading.
		bool ShowIcon(RE::GFxMovie* a_movie, RE::GFxValue& a_row, RE::GFxValue& a_icon, const std::string& a_source, const std::string& a_label,
			std::optional<double> a_color, double a_x, double a_y, double a_box)
		{
			if (!core::IEquals(StringMember(a_row, kLoadedSource), a_source)) {
				SetVisible(a_icon, false);
				SetStringMember(a_movie, a_row, kUrlBefore, StringMember(a_icon, "_url"));
				const RE::GFxValue path(a_source.c_str());
				a_icon.Invoke("loadMovie", nullptr, &path, 1);
				SetStringMember(a_movie, a_row, kLoadedSource, a_source);
				SetStringMember(a_movie, a_row, kShownIcon, {});
				return false;
			}
			if (!HasLoaded(a_icon, a_source, StringMember(a_row, kUrlBefore))) {
				return false;
			}
			const auto key = std::format("{}|{}", a_label, a_color ? static_cast<std::int64_t>(*a_color) : -1);
			if (StringMember(a_row, kShownIcon) != key) {
				a_icon.GotoAndStop(a_label.c_str());
				Tint(a_movie, a_icon, a_color);
				RE::GFxValue::DisplayInfo unscaled;
				unscaled.SetScale(100.0, 100.0);
				a_icon.SetDisplayInfo(unscaled);
				a_row.SetMember(kIconW, RE::GFxValue(NumberMember(a_icon, "_width")));
				a_row.SetMember(kIconH, RE::GFxValue(NumberMember(a_icon, "_height")));
				SetStringMember(a_movie, a_row, kShownIcon, key);
			}
			const double w = NumberMember(a_row, kIconW);
			const double h = NumberMember(a_row, kIconH);
			if (w <= 0.0 || h <= 0.0) {
				SetVisible(a_icon, false);  // that label is not in the SWF: no icon, nothing to wait for
				return true;
			}
			const double              scale = 100.0 * a_box / std::max(w, h);
			RE::GFxValue::DisplayInfo info;
			info.SetScale(scale, scale);
			info.SetPosition(a_x + (a_box - w * scale / 100.0) / 2.0, a_y + (a_box - h * scale / 100.0) / 2.0);
			info.SetVisible(true);
			a_icon.SetDisplayInfo(info);
			return true;
		}

		// One row clip: moves its text right by one icon column and shows the icon of the entry it
		// currently draws (none for an unused clip or an entry without one). False while loading.
		bool DecorateRow(RE::GFxMovie* a_movie, RE::GFxValue& a_row, const RE::GFxValue& a_array, double a_box, double a_gap)
		{
			RE::GFxValue text;
			if (!a_row.GetMember("textField", &text) || !text.IsDisplayObject()) {
				return true;  // not a row we understand: leave it alone
			}
			RE::GFxValue textX, textW;
			if (!a_row.GetMember(kTextX, &textX) || !textX.IsNumber() || !a_row.GetMember(kTextW, &textW) || !textW.IsNumber()) {
				textX = RE::GFxValue(NumberMember(text, "_x"));
				textW = RE::GFxValue(NumberMember(text, "_width"));
				a_row.SetMember(kTextX, textX);
				a_row.SetMember(kTextW, textW);
			}
			const double shift = a_box + a_gap;
			if (std::abs(NumberMember(text, "_x") - (textX.GetNumber() + shift)) > 0.1) {
				RE::GFxValue::DisplayInfo info;
				info.SetX(textX.GetNumber() + shift);
				text.SetDisplayInfo(info);
				text.SetMember("_width", RE::GFxValue(std::max(10.0, textW.GetNumber() - shift)));
			}

			RE::GFxValue icon;
			if (!a_row.GetMember(kIconClip, &icon) || !icon.IsDisplayObject()) {
				RE::GFxValue depth;
				a_row.Invoke("getNextHighestDepth", &depth);
				if (!a_row.CreateEmptyMovieClip(&icon, kIconClip, depth.IsNumber() ? static_cast<std::int32_t>(depth.GetNumber()) : 1000)) {
					return true;
				}
			}
			std::string           source, label;
			std::optional<double> color;
			RE::GFxValue          itemIndex, object, c;
			if (a_row.GetMember("itemIndex", &itemIndex) && itemIndex.IsNumber() && itemIndex.GetNumber() >= 0.0 &&
				a_array.GetElement(static_cast<std::uint32_t>(itemIndex.GetNumber()), &object) && object.IsObject()) {
				source = StringMember(object, kIconSource);
				label = StringMember(object, kIconLabel);
				if (object.GetMember(kIconColor, &c) && c.IsNumber()) {
					color = c.GetNumber();
				}
			}
			if (source.empty() || label.empty()) {
				SetVisible(icon, false);
				return true;
			}
			const double middle = NumberMember(text, "_y") + NumberMember(text, "_height") / 2.0;
			return ShowIcon(a_movie, a_row, icon, source, label, color, textX.GetNumber(), middle - a_box / 2.0, a_box);
		}

		// Every row clip of the list. False while an icon is still loading.
		bool DecorateRows(RE::GFxMovie* a_movie, RE::GFxValue& a_list)
		{
			RE::GFxValue flag, array;
			if (!a_list.GetMember(kHasIcons, &flag) || !flag.IsBool() || !flag.GetBool() || !GetEntryArray(a_list, array)) {
				return true;
			}
			const double row = NumberMember(a_list, "entryHeight", 25.0);
			const double box = std::round(row * 0.8);
			const double gap = std::round(row * 0.3);
			bool         ready = true;
			for (std::size_t i = 0; i < kMaxEntries; ++i) {
				const RE::GFxValue index(static_cast<double>(i));
				RE::GFxValue       clip;
				if (!a_list.Invoke("getClipByIndex", &clip, &index, 1) || !clip.IsDisplayObject()) {
					break;  // past the pool
				}
				ready = DecorateRow(a_movie, clip, array, box, gap) && ready;
			}
			return ready;
		}

		// ---- Logo ----

		// The holder above the menu: an invisible square of the logo's size (so the menu's bounds, and the
		// centring, include the logo from the start), with the image loading into its "image" child.
		bool AddLogo(RE::GFxMovieView* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_list)
		{
			RE::GFxValue holder;
			if (a_menu.GetMember(kLogoClip, &holder) && holder.IsDisplayObject()) {
				return true;
			}
			RE::GFxValue bounds;
			a_menu.Invoke("getBounds", &bounds, &a_menu, 1);
			if (!bounds.IsObject()) {
				return false;
			}
			const double row = NumberMember(a_list, "entryHeight", 25.0);
			const double size = std::round(row * 2.4);
			const double gap = std::round(row * 0.4);
			const double x = std::round((NumberMember(bounds, "xMin") + NumberMember(bounds, "xMax") - size) / 2.0);
			const double y = NumberMember(bounds, "yMin") - gap - size;
			if (!Settings::GetSingleton().GetUI().centerMenus) {
				// Left where the skin put the menu: no logo if it would be above the screen.
				RE::GFxValue::DisplayInfo menuInfo;
				a_menu.GetDisplayInfo(&menuInfo);
				if (menuInfo.GetY() + y < a_movie->GetVisibleFrameRect().top) {
					return false;
				}
			}
			RE::GFxValue depth;
			a_menu.Invoke("getNextHighestDepth", &depth);
			if (!a_menu.CreateEmptyMovieClip(&holder, kLogoClip, depth.IsNumber() ? static_cast<std::int32_t>(depth.GetNumber()) : 1000)) {
				return false;
			}
			const RE::GFxValue fill[2]{ RE::GFxValue(0.0), RE::GFxValue(0.0) };  // black, alpha 0
			holder.Invoke("beginFill", nullptr, fill, 2);
			const std::array<std::pair<double, double>, 5> square{ { { 0.0, 0.0 }, { size, 0.0 }, { size, size }, { 0.0, size }, { 0.0, 0.0 } } };
			for (std::size_t i = 0; i < square.size(); ++i) {
				const RE::GFxValue point[2]{ RE::GFxValue(square[i].first), RE::GFxValue(square[i].second) };
				holder.Invoke(i == 0 ? "moveTo" : "lineTo", nullptr, point, 2);
			}
			holder.Invoke("endFill");
			RE::GFxValue::DisplayInfo info;
			info.SetPosition(x, y);
			holder.SetDisplayInfo(info);
			holder.SetMember(kLogoSize, RE::GFxValue(size));
			RE::GFxValue image;
			if (!holder.CreateEmptyMovieClip(&image, "image", 1)) {
				return false;
			}
			SetVisible(image, false);
			const RE::GFxValue path(kLogoPath.data());
			image.Invoke("loadMovie", nullptr, &path, 1);
			return true;
		}

		// Fits and shows the logo once loaded, in the colour of the list's own text. False while loading.
		bool DecorateLogo(RE::GFxMovie* a_movie, RE::GFxValue& a_menu, RE::GFxValue& a_list)
		{
			RE::GFxValue holder, image, done;
			if (!a_menu.GetMember(kLogoClip, &holder) || !holder.IsDisplayObject() || !holder.GetMember("image", &image) || !image.IsDisplayObject() ||
				(holder.GetMember(kLogoDone, &done) && done.IsBool())) {
				return true;
			}
			// The image clip starts empty: it has a size once the image is in (a missing file never does,
			// and PollDecor gives up on it).
			const double w = NumberMember(image, "_width");
			const double h = NumberMember(image, "_height");
			if (w <= 0.0 || h <= 0.0) {
				return false;
			}
			const double              size = NumberMember(holder, kLogoSize, 60.0);
			const double              scale = 100.0 * size / std::max(w, h);
			RE::GFxValue::DisplayInfo info;
			info.SetScale(scale, scale);
			info.SetPosition((size - w * scale / 100.0) / 2.0, (size - h * scale / 100.0) / 2.0);
			info.SetVisible(true);
			image.SetDisplayInfo(info);
			// The logo is white: take the colour the skin gives the list's text.
			const RE::GFxValue first(0.0);
			RE::GFxValue       row, text, color;
			if (a_list.Invoke("getClipByIndex", &row, &first, 1) && row.IsDisplayObject() && row.GetMember("textField", &text) && text.IsDisplayObject() &&
				text.GetMember("textColor", &color) && color.IsNumber()) {
				Tint(a_movie, image, color.GetNumber());
			}
			holder.SetMember(kLogoDone, RE::GFxValue(true));
			logger::debug("UIExtensions: logo shown ({}x{} at {:.0f}%)", w, h, scale);
			return true;
		}

		// Icons and logo. True when everything shows (or there is nothing to show).
		bool Decorate(RE::GFxMovie* a_movie)
		{
			RE::GFxValue menu, list;
			if (!GetList(a_movie, menu, list)) {
				return true;
			}
			const bool logo = DecorateLogo(a_movie, menu, list);
			return DecorateRows(a_movie, list) && logo;
		}

		// Re-checks Decorate every ~16 ms while something is still loading. One poll at a time.
		std::atomic<std::uint64_t> g_decorPoll{ 0 };  // instance being polled, 0 = none

		void PollDecor(std::uint64_t a_instance, int a_try)
		{
			auto movie = IsCurrent(a_instance) ? CustomMenuMovie() : nullptr;
			if (!movie || Decorate(movie.get())) {
				g_decorPoll = 0;
				return;
			}
			if (a_try >= kDecorTries) {
				g_decorPoll = 0;
				RE::GFxValue menu, list, holder, done;
				if (GetList(movie.get(), menu, list) && menu.GetMember(kLogoClip, &holder) && holder.IsDisplayObject() &&
					!(holder.GetMember(kLogoDone, &done) && done.IsBool())) {
					holder.SetMember(kLogoDone, RE::GFxValue(false));  // stop waiting for it
					logger::warn("UIExtensions: Interface/{} did not load (missing file?); lists show without the logo", kLogoPath);
				}
				logger::debug("UIExtensions: icons or logo still not loaded after {} tries (missing file?); shown without them", a_try);
				return;
			}
			Later(kDecorRetry, [a_instance, a_try]() {
				SKSE::GetTaskInterface()->AddUITask([a_instance, a_try]() { PollDecor(a_instance, a_try + 1); });
			});
		}

		void EnsureDecorPoll(std::uint64_t a_instance)
		{
			if (g_decorPoll.exchange(a_instance) != a_instance) {
				PollDecor(a_instance, 0);
			}
		}

		// ---- Hooks ----

		// Points a_object[a_name] at our native function and keeps the SWF's own one as a_object[a_saved],
		// which ours calls for everything it does not handle. False (nothing changed) when the SWF has no
		// such function.
		bool Hook(RE::GFxMovie* a_movie, RE::GFxValue& a_object, const char* a_name, const char* a_saved, RE::GFxFunctionHandler* a_handler)
		{
			RE::GFxValue original;
			if (a_object.GetMember(a_saved, &original) && original.IsObject()) {
				return true;  // already ours
			}
			if (!a_object.GetMember(a_name, &original) || !original.IsObject()) {
				return false;
			}
			RE::GFxValue function;
			a_movie->CreateFunction(&function, a_handler);
			if (!function.IsObject()) {
				return false;
			}
			return a_object.SetMember(a_saved, original) && a_object.SetMember(a_name, function);
		}

		// Never released: function objects created from it live in the menu movie.
		template <class T>
		RE::GFxFunctionHandler* Handler()
		{
			static auto* handler = new T();
			return handler;
		}

		void Refresh(std::uint64_t a_instance, std::vector<ListEntry> a_entries);

		// The press of a stay-open entry (itemPress event of the menu's itemList): runs the caller's
		// onStay on the main thread and updates the list in place, with the menu left open. False for any
		// other entry.
		bool Stay(RE::GFxValue& a_menu, const RE::GFxValue& a_event)
		{
			RE::GFxValue list, array, object;
			const auto   index = NumberMember(a_event, "index", -1.0);
			if (index < 0.0 || !a_menu.GetMember("itemList", &list) || !GetEntryArray(list, array) || !array.GetElement(static_cast<std::uint32_t>(index), &object) ||
				!object.IsObject()) {
				return false;
			}
			const auto    id = NumberMember(object, "id", -1.0);
			StayCallback  onStay;
			std::uint64_t instance = 0;
			{
				std::scoped_lock guard(g_lock);
				if (!g_pending.open || g_pending.kind != Kind::List || g_pending.instance == 0 || !g_pending.options.onStay || id < 0.0 ||
					id >= static_cast<double>(g_pending.entries.size()) || !g_pending.entries[static_cast<std::size_t>(id)].stayOpen) {
					return false;
				}
				onStay = g_pending.options.onStay;
				instance = g_pending.instance;
			}
			const auto choice = static_cast<std::int32_t>(id);
			logger::debug("UIExtensions: entry {} chosen, list instance {} stays open", choice, instance);
			SKSE::GetTaskInterface()->AddTask([onStay = std::move(onStay), instance, choice]() {
				auto entries = onStay(choice);
				SKSE::GetTaskInterface()->AddUITask([instance, entries = std::move(entries)]() mutable { Refresh(instance, std::move(entries)); });
			});
			return true;
		}

		// _root.listMenu.onItemPressed: stay-open entries are ours, everything else goes to the SWF.
		class ItemPressedHook final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (!a_params.thisPtr) {
					return;
				}
				if (a_params.argCount > 0 && Stay(*a_params.thisPtr, a_params.args[0])) {
					return;
				}
				a_params.thisPtr->Invoke(kSavedItemPressed, a_params.retVal, a_params.args, a_params.argCount);
			}
		};

		// itemList.UpdateList: the SWF draws the rows (also on every scroll), then we re-dress them.
		class UpdateListHook final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (!a_params.thisPtr) {
					return;
				}
				a_params.thisPtr->Invoke(kSavedUpdateList, a_params.retVal, a_params.args, a_params.argCount);
				if (a_params.movie && !DecorateRows(a_params.movie, *a_params.thisPtr)) {
					std::uint64_t instance = 0;
					{
						std::scoped_lock guard(g_lock);
						instance = g_pending.open ? g_pending.instance : 0;
					}
					if (instance != 0) {
						EnsureDecorPoll(instance);
					}
				}
			}
		};

		// After the first fill: hooks, icon column, initial selection, logo.
		void AddExtras(RE::GFxMovieView* a_movie, std::uint64_t a_instance, const std::vector<ListEntry>& a_entries, const ListOptions& a_options)
		{
			RE::GFxValue menu, list;
			if (!GetList(a_movie, menu, list)) {
				logger::debug("UIExtensions: list instance {}: no _root.listMenu.itemList, plain list", a_instance);
				return;
			}
			const bool stay = a_options.onStay && std::ranges::any_of(a_entries, &ListEntry::stayOpen) &&
			                  Hook(a_movie, menu, "onItemPressed", kSavedItemPressed, Handler<ItemPressedHook>());
			// Rows are pooled and redrawn on every scroll: icons need the UpdateList hook to follow them.
			const bool icons = std::ranges::any_of(a_entries, [](const ListEntry& e) { return !e.icon.Empty(); }) &&
			                   Hook(a_movie, list, "UpdateList", kSavedUpdateList, Handler<UpdateListHook>());
			list.SetMember(kHasIcons, RE::GFxValue(icons));
			if (a_options.select >= 0 && static_cast<std::size_t>(a_options.select) < a_entries.size()) {
				const RE::GFxValue args[2]{ RE::GFxValue(static_cast<double>(a_options.select)), RE::GFxValue(1.0) };  // SELECT_KEYBOARD
				list.Invoke("doSetSelectedIndex", nullptr, args, 2);
			}
			const bool logo = a_options.logo && AddLogo(a_movie, menu, list);
			logger::debug("UIExtensions: list instance {}: in-place entries {}, icons {}, logo {}", a_instance, stay ? "on" : "off", icons ? "on" : "off",
				logo ? "on" : "off");
		}

		// Vertical centring (DESIGN §10.3): skin-agnostic. We do not touch the skin's layout: we measure
		// where the menu's own clip (_root.listMenu / _root.textEntry) ended up and move the whole movie
		// (_root) so that clip sits in the middle of the visible screen.
		//
		// No visible jump: Fill makes _root transparent (alpha 0, so the list still lays out, advances
		// and measures exactly as when shown) before it adds the entries, and Center shows it once it is
		// in place. Center measures right away, then every ~16 ms, and moves the menu once two
		// measurements a frame apart agree (the list has finished laying out its rows) and the row icons
		// and logo have loaded (the logo's space is reserved from the start, so it is measured too).
		// Every way out of Center shows the menu again, so it can never stay invisible; icons still
		// loading by then keep loading while it is shown (PollDecor).
		constexpr int  kCenterTries = 20;  // ~0.4 s, then it is shown where it is
		constexpr auto kCenterRetry = std::chrono::milliseconds(16);

		struct Bounds
		{
			double yMin{ 0.0 };
			double yMax{ 0.0 };

			[[nodiscard]] bool SameAs(const Bounds& a_other) const
			{
				return std::abs(yMin - a_other.yMin) < 0.5 && std::abs(yMax - a_other.yMax) < 0.5;
			}
		};

		// The menu clip's vertical bounds in _root's space, or nullopt while it has not laid out.
		std::optional<Bounds> Measure(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, const Contract& a_contract)
		{
			std::string clipPath{ a_contract.root };
			clipPath.pop_back();  // "_root.listMenu." -> "_root.listMenu"
			RE::GFxValue clip, bounds, yMin, yMax;
			if (!a_movie->GetVariable(&clip, clipPath.c_str()) || !clip.IsDisplayObject()) {
				return std::nullopt;
			}
			clip.Invoke("getBounds", &bounds, &a_root, 1);
			if (!bounds.IsObject() || !bounds.GetMember("yMin", &yMin) || !bounds.GetMember("yMax", &yMax) || !yMin.IsNumber() || !yMax.IsNumber() ||
				yMax.GetNumber() - yMin.GetNumber() < 1.0) {
				return std::nullopt;
			}
			return Bounds{ yMin.GetNumber(), yMax.GetNumber() };
		}

		// `a_try`: 0 for the call from Fill. `a_last`: the previous measurement. `a_alpha`: _root's alpha
		// before Fill hid it.
		void Center(Kind a_kind, std::uint64_t a_instance, int a_try, std::optional<Bounds> a_last, double a_alpha)
		{
			{
				std::scoped_lock guard(g_lock);
				if (!g_pending.open || g_pending.instance != a_instance) {
					return;  // closed (its movie is gone or going), or a newer menu that shows itself
				}
			}
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kCustomMenu) : nullptr;
			if (!movie) {
				return;  // nothing left to show
			}
			const auto&  contract = ContractOf(a_kind);
			RE::GFxValue root;
			if (!movie->GetVariable(&root, "_root") || !root.IsDisplayObject()) {
				return;  // Fill could not hide it either
			}
			const bool decorated = Decorate(movie.get());
			// Shows the menu, at `a_y` when given.
			const auto show = [&](std::optional<double> a_y) {
				RE::GFxValue::DisplayInfo info;
				info.SetAlpha(a_alpha);
				if (a_y) {
					info.SetY(*a_y);
				}
				root.SetDisplayInfo(info);
				if (!decorated) {
					EnsureDecorPoll(a_instance);
				}
			};
			if (!Settings::GetSingleton().GetUI().centerMenus) {
				show(std::nullopt);
				logger::debug("UIExtensions: {} shown in place (centring turned off)", contract.swf);
				return;
			}
			const auto bounds = Measure(movie.get(), root, contract);
			const bool settled = bounds && a_last && bounds->SameAs(*a_last) && decorated;
			if (!settled && a_try < kCenterTries) {
				Later(kCenterRetry, [a_kind, a_instance, a_try, bounds, a_alpha]() {
					SKSE::GetTaskInterface()->AddUITask([a_kind, a_instance, a_try, bounds, a_alpha]() { Center(a_kind, a_instance, a_try + 1, bounds, a_alpha); });
				});
				return;
			}
			if (!bounds) {
				show(std::nullopt);
				logger::debug("UIExtensions: {} not centred (no usable bounds after {} tries), shown in place", contract.swf, a_try + 1);
				return;
			}
			RE::GFxValue::DisplayInfo info;
			root.GetDisplayInfo(&info);
			const auto   rect = movie->GetVisibleFrameRect();
			const double screenMid = (rect.top + rect.bottom) / 2.0;
			// getBounds(_root) is in _root's own space; add _root's current offset to get stage space.
			const double clipMid = (bounds->yMin + bounds->yMax) / 2.0 + info.GetY();
			const double newY = info.GetY() + (screenMid - clipMid);
			show(newY);
			logger::debug("UIExtensions: {} centred ({} -> {}) after {} tries{}", contract.swf, info.GetY(), newY, a_try + 1, settled ? "" : " (still changing)");
		}

		// Before filling: hides the menu until Center has placed it. Returns _root's alpha to restore, or
		// nullopt when centring is off or _root cannot be read (then it is never hidden).
		std::optional<double> HideForCentring(RE::GFxMovieView* a_movie)
		{
			if (!Settings::GetSingleton().GetUI().centerMenus) {
				return std::nullopt;
			}
			RE::GFxValue root;
			if (!a_movie->GetVariable(&root, "_root") || !root.IsDisplayObject()) {
				return std::nullopt;
			}
			RE::GFxValue::DisplayInfo current;
			const double              alpha = root.GetDisplayInfo(&current) && current.GetAlpha() > 0.0 ? current.GetAlpha() : 100.0;
			RE::GFxValue::DisplayInfo hidden;
			hidden.SetAlpha(0.0);
			root.SetDisplayInfo(hidden);
			return alpha;
		}

		// A stay-open entry's update (Stay -> caller's onStay on the main thread -> here, UI thread): the
		// menu stayed open; its entries change in place. Same count: new texts and icons on the existing
		// entry objects, then the list redraws its rows (selection and scroll position stay). Another
		// count: the entries are replaced the way LM_AddTreeEntries adds them, and the menu re-centred.
		void Refresh(std::uint64_t a_instance, std::vector<ListEntry> a_entries)
		{
			if (a_entries.empty()) {
				return;
			}
			if (a_entries.size() > kMaxEntries) {
				a_entries.resize(kMaxEntries);
			}
			std::size_t before = 0;
			{
				std::scoped_lock guard(g_lock);
				if (!g_pending.open || g_pending.instance != a_instance) {
					return;  // closed meanwhile
				}
				before = g_pending.entries.size();
				g_pending.entries = a_entries;
			}
			auto         movie = CustomMenuMovie();
			RE::GFxValue menu, list, array;
			if (!movie || !GetList(movie.get(), menu, list) || !GetEntryArray(list, array)) {
				return;
			}
			RE::GFxValue hasIcons;
			if (list.GetMember(kHasIcons, &hasIcons) && hasIcons.IsBool() && !hasIcons.GetBool() &&
				std::ranges::any_of(a_entries, [](const ListEntry& e) { return !e.icon.Empty(); })) {
				// Icons where there were none: only with the UpdateList hook (rows follow scrolling).
				list.SetMember(kHasIcons, RE::GFxValue(Hook(movie.get(), list, "UpdateList", kSavedUpdateList, Handler<UpdateListHook>())));
			}
			if (a_entries.size() == before && array.GetArraySize() == a_entries.size()) {
				Annotate(movie.get(), list, a_entries, true);
				list.Invoke("UpdateList");  // the SWF redraws the visible rows (and our hook re-dresses them)
				logger::debug("UIExtensions: list instance {} updated in place", a_instance);
				return;
			}
			array.ClearElements();
			RE::GFxValue linear;
			if (menu.GetMember("_linearChildren", &linear) && linear.IsArray()) {
				linear.ClearElements();
			}
			AddEntries(movie.get(), a_entries);
			logger::debug("UIExtensions: list instance {} refilled in place ({} -> {} entries)", a_instance, before, a_entries.size());
			// Not while Fill's own Center still has the menu hidden (alpha 0): that chain re-measures the
			// refilled list and shows it; a second chain started now would end by "restoring" alpha 0.
			RE::GFxValue              root;
			RE::GFxValue::DisplayInfo info;
			if (Settings::GetSingleton().GetUI().centerMenus && movie->GetVariable(&root, "_root") && root.IsDisplayObject() && root.GetDisplayInfo(&info) &&
				info.GetAlpha() > 0.0) {
				Center(Kind::List, a_instance, 0, std::nullopt, info.GetAlpha());
			} else if (!Decorate(movie.get())) {
				EnsureDecorPoll(a_instance);
			}
		}

		// A menu that loaded can still vanish without a select/close event (the game loads or reloads
		// on death and the CustomMenu is destroyed with its events unsent). Without this, `g_pending`
		// would stay open forever and every later list or text entry would be refused. Poll once a
		// second while this instance is pending; only after the menu has been gone for three checks
		// in a row (so a result or close already queued on the task queue wins) cancel it, through
		// the task queue like every other completion.
		void WatchLoaded(std::uint64_t a_instance, int a_missing)
		{
			if (!IsCurrent(a_instance)) {
				return;  // completed, or a newer menu: nothing to watch
			}
			auto*     ui = RE::UI::GetSingleton();
			const int missing = ui && ui->IsMenuOpen(kCustomMenu) ? 0 : a_missing + 1;
			if (missing >= 3) {
				logger::warn("UIExtensions: menu instance {} vanished without a result; treating it as cancelled", a_instance);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([a_instance]() { Complete(a_instance, -1); });
				}
				return;
			}
			Later(std::chrono::seconds(1), [a_instance, missing]() {
				SKSE::GetTaskInterface()->AddUITask([a_instance, missing]() { WatchLoaded(a_instance, missing); });
			});
		}

		void Fill(Kind a_kind, std::uint64_t a_instance)
		{
			std::vector<ListEntry> entries;
			ListOptions            options;
			std::string            initialText;
			{
				std::scoped_lock guard(g_lock);
				if (!g_pending.open || g_pending.kind != a_kind || g_pending.instance != 0) {
					return;  // not ours (another mod's menu), or already filled
				}
				g_pending.instance = a_instance;
				entries = g_pending.entries;
				options = g_pending.options;
				initialText = g_pending.text;
			}
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kCustomMenu) : nullptr;
			if (!movie) {
				logger::warn("UIExtensions: {} loaded but CustomMenu has no movie", ContractOf(a_kind).swf);
				return;
			}
			WatchLoaded(a_instance, 0);
			const auto alpha = HideForCentring(movie.get());
			if (a_kind == Kind::Text) {
				// UITextEntryMenu.psc OnLoadMenu: setTextEntryMenuText(initial text).
				logger::debug("UIExtensions: filling text entry instance {}{}", a_instance, alpha ? ", hidden until centred" : "");
				const RE::GFxValue text(initialText.c_str());
				Invoke(movie.get(), kText, "setTextEntryMenuText", { &text, 1 });
				if (alpha) {
					Center(a_kind, a_instance, 0, std::nullopt, *alpha);
				}
				return;
			}
			logger::debug("UIExtensions: filling list instance {} with {} entries{}", a_instance, entries.size(), alpha ? ", hidden until centred" : "");
			// Same calls, same order and format as UIListMenu.psc OnLoadMenu.
			AddEntries(movie.get(), entries);
			const RE::GFxValue sorting(false);
			Invoke(movie.get(), kList, "LM_SetSortingEnabled", { &sorting, 1 });
			AddExtras(movie.get(), a_instance, entries, options);
			// The list may lay its rows out over the next frames: Center waits until its size settles.
			if (alpha) {
				Center(a_kind, a_instance, 0, std::nullopt, *alpha);
			} else if (!Decorate(movie.get())) {
				EnsureDecorPoll(a_instance);
			}
		}

		bool OpenCustomMenu(std::string_view a_swf)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return false;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> noCallback;
			auto* args = RE::MakeFunctionArguments(RE::BSFixedString(a_swf), static_cast<std::int32_t>(0));
			return vm->DispatchStaticCall("UI", "OpenCustomMenu", args, noCallback);
		}


		// If the SWF never reports LoadMenu (missing file, another mod's custom menu won the race), give
		// up after a few seconds so the next request is not refused forever.
		void ArmTimeout(std::uint64_t a_serial)
		{
			Later(std::chrono::seconds(4), [a_serial]() {
				FinishIf(
					[a_serial](const Pending& p) {
						const bool stuck = p.open && p.serial == a_serial && p.instance == 0;
						if (stuck) {
							logger::warn("UIExtensions: {} did not open (is UIExtensions installed correctly?)", ContractOf(p.kind).swf);
						}
						return stuck;
					},
					kNotShown);
			});
		}

		// Opens the custom menu once the previous one (ours closing after a choice, or another mod's) is
		// gone: polls every 50 ms for up to 2 s.
		void TryOpen(std::uint64_t a_serial, int a_retries)
		{
			Kind kind;
			{
				std::scoped_lock guard(g_lock);
				if (!g_pending.open || g_pending.serial != a_serial) {
					return;
				}
				kind = g_pending.kind;
			}
			const auto ours = [a_serial](const Pending& p) { return p.open && p.serial == a_serial; };
			auto*      ui = RE::UI::GetSingleton();
			if (ui && ui->IsMenuOpen(kCustomMenu)) {
				if (a_retries <= 0) {
					logger::warn("UIExtensions: another custom menu stayed open; list not shown");
					FinishIf(ours, kNotShown);
					return;
				}
				Later(std::chrono::milliseconds(50), [a_serial, a_retries]() { TryOpen(a_serial, a_retries - 1); });
				return;
			}
			if (!OpenCustomMenu(ContractOf(kind).swf)) {
				logger::warn("UIExtensions: could not open {}", ContractOf(kind).swf);
				FinishIf(ours, kNotShown);
				return;
			}
			ArmTimeout(a_serial);
		}

		class ModEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			Control ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!a_event) {
					return Control::kContinue;
				}
				const std::string_view name{ a_event->eventName.c_str() };
				std::optional<Kind>    kind;
				for (const auto k : { Kind::List, Kind::Text }) {
					const auto& c = ContractOf(k);
					if (name == c.load || name == c.result || name == c.close) {
						kind = k;
					}
				}
				if (!kind) {
					return Control::kContinue;
				}
				auto* tasks = SKSE::GetTaskInterface();
				if (!tasks) {
					return Control::kContinue;
				}
				const auto&   c = ContractOf(*kind);
				std::uint64_t instance = 0;
				{
					std::scoped_lock guard(g_lock);
					instance = name == c.load ? ++g_instance : g_instance;
				}
				logger::debug("UIExtensions event {} (str \"{}\", num {}, instance {})", name, a_event->strArg.c_str(), a_event->numArg, instance);
				if (name == c.load) {
					tasks->AddUITask([k = *kind, instance]() { Fill(k, instance); });
				} else if (name == c.result && *kind == Kind::List) {
					// UIListMenu.psc: strArg is the entry id, numArg -1 means "back".
					std::int32_t      index = -1;
					const std::string str{ a_event->strArg.c_str() };
					if (a_event->numArg != -1.0f) {
						std::from_chars(str.data(), str.data() + str.size(), index);
					}
					tasks->AddTask([instance, index]() { Complete(instance, index); });
				} else if (name == c.result) {
					// UITextEntryMenu.psc: strArg is the entered text.
					tasks->AddTask([instance, text = std::string(a_event->strArg.c_str())]() { Complete(instance, 0, text); });
				} else {
					// A result is reported before the menu closes; only a close without one is a cancel. Run
					// a frame later so a result event queued in the same frame wins.
					tasks->AddTask([instance, tasks]() {
						tasks->AddTask([instance]() { Complete(instance, -1); });
					});
				}
				return Control::kContinue;
			}
		};

		ModEventSink g_sink;
	}

	bool IsInstalled()
	{
		static const bool installed = []() {
			if (REL::Module::IsVR()) {
				return false;
			}
			auto*       data = RE::TESDataHandler::GetSingleton();
			const auto* file = data ? data->LookupModByName("UIExtensions.esp") : nullptr;
			return file != nullptr && file->compileIndex != 0xFF;
		}();
		return installed;
	}

	void RegisterSinks()
	{
		if (!IsInstalled()) {
			logger::info("UIExtensions: not installed (in-world menus use native message boxes)");
			return;
		}
		if (auto* source = SKSE::GetModCallbackEventSource()) {
			source->AddEventSink(&g_sink);
			logger::info("UIExtensions: installed, list and text-entry menus available");
		} else {
			logger::warn("UIExtensions: no SKSE mod event source");
		}
	}

	bool IsBusy()
	{
		std::scoped_lock guard(g_lock);
		return g_pending.open;
	}

	bool OpenList(std::vector<ListEntry> a_entries, ChoiceCallback a_callback, ListOptions a_options)
	{
		if (!IsInstalled() || a_entries.empty()) {
			return false;
		}
		if (a_entries.size() > kMaxEntries) {
			logger::warn("UIExtensions: list has {} entries, only {} are shown", a_entries.size(), kMaxEntries);
			a_entries.resize(kMaxEntries);
		}
		std::uint64_t serial = 0;
		{
			std::scoped_lock guard(g_lock);
			if (g_pending.open) {
				return false;
			}
			g_pending = {};
			g_pending.open = true;
			g_pending.serial = serial = ++g_serial;
			logger::debug("UIExtensions: list request {} ({} entries)", serial, a_entries.size());
			g_pending.entries = std::move(a_entries);
			g_pending.callback = std::move(a_callback);
			g_pending.options = std::move(a_options);
		}
		TryOpen(serial, 40);
		return true;
	}

	bool OpenTextEntry(std::string a_initial, TextCallback a_callback)
	{
		if (!IsInstalled()) {
			return false;
		}
		std::uint64_t serial = 0;
		{
			std::scoped_lock guard(g_lock);
			if (g_pending.open) {
				return false;
			}
			g_pending = {};
			g_pending.open = true;
			g_pending.kind = Kind::Text;
			g_pending.serial = serial = ++g_serial;
			g_pending.text = std::move(a_initial);
			g_pending.textCallback = std::move(a_callback);
		}
		logger::debug("UIExtensions: text entry request {}", serial);
		TryOpen(serial, 40);
		return true;
	}

	ListState PendingList()
	{
		std::scoped_lock guard(g_lock);
		ListState        out;
		if (!g_pending.open || g_pending.kind != Kind::List) {
			return out;
		}
		out.serial = g_pending.serial;
		out.shown = g_pending.instance != 0;
		for (const auto& e : g_pending.entries) {
			out.entries.push_back(e.text);
		}
		return out;
	}

	bool ChooseForTest(std::int32_t a_index)
	{
		std::uint64_t instance = 0;
		StayCallback  onStay;
		{
			std::scoped_lock guard(g_lock);
			if (!g_pending.open || g_pending.kind != Kind::List || g_pending.instance == 0 || a_index < -1 ||
				a_index >= static_cast<std::int32_t>(g_pending.entries.size())) {
				return false;
			}
			instance = g_pending.instance;
			if (a_index >= 0 && g_pending.entries[static_cast<std::size_t>(a_index)].stayOpen && g_pending.options.onStay) {
				onStay = g_pending.options.onStay;
			}
		}
		logger::info("UIExtensions: AutoTest chooses entry {} of list instance {}", a_index, instance);
		if (onStay) {
			// What Stay does for a press of a stay-open entry: the menu stays open and its entries change in place.
			SKSE::GetTaskInterface()->AddTask([onStay = std::move(onStay), instance, a_index]() {
				auto entries = onStay(a_index);
				SKSE::GetTaskInterface()->AddUITask([instance, entries = std::move(entries)]() mutable { Refresh(instance, std::move(entries)); });
			});
			return true;
		}
		// What the SWF does for any other entry (and Esc, -1): the menu closes and the choice is reported (its close
		// event then finds nothing pending).
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(kCustomMenu, RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
		Complete(instance, a_index);
		return true;
	}
}
