#include "UI/UIExtensions.h"

#include "Settings.h"

#include <thread>

namespace dova::ui::uiext
{
	namespace
	{
		using Control = RE::BSEventNotifyControl;

		// The UIExtensions contracts we use: UIListMenu.psc and UITextEntryMenu.psc. Nothing else.
		constexpr auto        kCustomMenu = "CustomMenu"sv;  // SKSE's custom menu that hosts the SWFs
		constexpr std::size_t kMaxEntries = 128;             // UIListMenu.psc buffers 128 entries; skins may assume it

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

		// Main-thread state of the one list we may have open. The sink only copies event values and
		// enqueues; everything below runs from SKSE tasks / UI tasks on the game thread.
		//
		// Two ids keep events with the right list. `serial` is our request (OpenList call); `instance`
		// counts menus that actually loaded (UIListMenu_LoadMenu). Select/close events carry the instance
		// that was on screen, so the close event of the previous list (which often arrives after we have
		// already asked for the next one, e.g. when a toggle reopens the list) can never cancel the new
		// request.
		struct Pending
		{
			bool                   open{ false };
			Kind                   kind{ Kind::List };
			std::uint64_t          serial{ 0 };
			std::uint64_t          instance{ 0 };  // 0 until this request's menu has loaded
			std::vector<ListEntry> entries;        // List
			ChoiceCallback         callback;       // List
			std::string            text;           // Text: initial text
			TextCallback           textCallback;   // Text
		};
		Pending       g_pending;
		std::uint64_t g_serial{ 0 };
		std::uint64_t g_instance{ 0 };  // sink side: last loaded menu

		// Ends the pending request: list index (-1 = cancelled) or entered text (nullopt = cancelled).
		void Finish(std::int32_t a_index, std::optional<std::string> a_text = std::nullopt)
		{
			auto pending = std::move(g_pending);
			g_pending = {};
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
			if (!g_pending.open || g_pending.instance == 0 || g_pending.instance != a_instance) {
				return;
			}
			Finish(a_index, std::move(a_text));
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
			// Entries are encoded as "name;;parent;;id;;callback;;children".
			std::string out{ a_text };
			for (std::size_t pos; (pos = out.find(";;")) != std::string::npos;) {
				out.replace(pos, 2, "; ");
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

		// Vertical centring (DESIGN §10.3): skin-agnostic. We do not touch the skin's layout: we measure
		// where the menu's own clip (_root.listMenu / _root.textEntry) ended up and move the whole movie
		// (_root) so that clip sits in the middle of the visible screen. Runs a few frames after filling,
		// once the list has laid out its entries; a later change of entry count re-centres on reopen.
		void Center(Kind a_kind, std::uint64_t a_instance, int a_tries)
		{
			if (!Settings::GetSingleton().GetUI().centerMenus || !g_pending.open || g_pending.instance != a_instance) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kCustomMenu) : nullptr;
			if (!movie) {
				return;
			}
			const auto&  contract = ContractOf(a_kind);
			std::string  clipPath{ contract.root };
			clipPath.pop_back();  // "_root.listMenu." -> "_root.listMenu"
			RE::GFxValue root, clip, bounds;
			if (!movie->GetVariable(&root, "_root") || !movie->GetVariable(&clip, clipPath.c_str()) || !clip.IsDisplayObject()) {
				return;
			}
			clip.Invoke("getBounds", &bounds, &root, 1);
			RE::GFxValue yMin, yMax;
			if (!bounds.IsObject() || !bounds.GetMember("yMin", &yMin) || !bounds.GetMember("yMax", &yMax) || !yMin.IsNumber() || !yMax.IsNumber() ||
				yMax.GetNumber() - yMin.GetNumber() < 1.0) {
				if (a_tries > 0) {  // not laid out yet
					Later(std::chrono::milliseconds(30), [a_kind, a_instance, a_tries]() {
						SKSE::GetTaskInterface()->AddUITask([a_kind, a_instance, a_tries]() { Center(a_kind, a_instance, a_tries - 1); });
					});
				}
				return;
			}
			RE::GFxValue::DisplayInfo info;
			root.GetDisplayInfo(&info);
			const auto rect = movie->GetVisibleFrameRect();
			const double screenMid = (rect.top + rect.bottom) / 2.0;
			// getBounds(_root) is in _root's own space; add _root's current offset to get stage space.
			const double clipMid = (yMin.GetNumber() + yMax.GetNumber()) / 2.0 + info.GetY();
			const double newY = info.GetY() + (screenMid - clipMid);
			RE::GFxValue::DisplayInfo move;
			move.SetY(newY);
			root.SetDisplayInfo(move);
			logger::debug("UIExtensions: {} centred ({} -> {})", contract.swf, info.GetY(), newY);
		}

		void Fill(Kind a_kind, std::uint64_t a_instance)
		{
			if (!g_pending.open || g_pending.kind != a_kind || g_pending.instance != 0) {
				return;  // not ours (another mod's menu), or already filled
			}
			g_pending.instance = a_instance;
			auto* ui = RE::UI::GetSingleton();
			auto  movie = ui ? ui->GetMovieView(kCustomMenu) : nullptr;
			if (!movie) {
				logger::warn("UIExtensions: {} loaded but CustomMenu has no movie", ContractOf(a_kind).swf);
				return;
			}
			if (a_kind == Kind::Text) {
				// UITextEntryMenu.psc OnLoadMenu: setTextEntryMenuText(initial text).
				logger::debug("UIExtensions: filling text entry instance {}", a_instance);
				const RE::GFxValue text(g_pending.text.c_str());
				Invoke(movie.get(), kText, "setTextEntryMenuText", { &text, 1 });
				Center(a_kind, a_instance, 10);
				return;
			}
			logger::debug("UIExtensions: filling list instance {} with {} entries", a_instance, g_pending.entries.size());
			// Same calls, same order and format as UIListMenu.psc OnLoadMenu. Strings outlive the Invoke.
			std::vector<std::string> encoded;
			encoded.reserve(g_pending.entries.size());
			for (std::size_t i = 0; i < g_pending.entries.size(); ++i) {
				const auto& e = g_pending.entries[i];
				// id and callback are both the entry index; the select event reports the id.
				encoded.push_back(std::format("{};;{};;{};;{};;{}", Sanitize(e.text), e.parent, i, i, e.hasChildren ? 1 : 0));
			}
			std::vector<RE::GFxValue> args;
			args.reserve(encoded.size());
			for (const auto& s : encoded) {
				args.emplace_back(s.c_str());
			}
			Invoke(movie.get(), kList, "LM_AddTreeEntries", args);
			const RE::GFxValue sorting(false);
			Invoke(movie.get(), kList, "LM_SetSortingEnabled", { &sorting, 1 });
			// The list lays its rows out over the next frames: measure after that.
			Later(std::chrono::milliseconds(30), [a_kind, a_instance]() {
				SKSE::GetTaskInterface()->AddUITask([a_kind, a_instance]() { Center(a_kind, a_instance, 10); });
			});
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
				if (g_pending.open && g_pending.serial == a_serial && g_pending.instance == 0) {
					logger::warn("UIExtensions: {} did not open (is UIExtensions installed correctly?)", ContractOf(g_pending.kind).swf);
					Finish(-1);
				}
			});
		}

		// Opens the custom menu once the previous one (ours closing after a choice, or another mod's) is
		// gone: polls every 50 ms for up to 2 s.
		void TryOpen(std::uint64_t a_serial, int a_retries)
		{
			if (!g_pending.open || g_pending.serial != a_serial) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			if (ui && ui->IsMenuOpen(kCustomMenu)) {
				if (a_retries <= 0) {
					logger::warn("UIExtensions: another custom menu stayed open; list not shown");
					Finish(-1);
					return;
				}
				Later(std::chrono::milliseconds(50), [a_serial, a_retries]() { TryOpen(a_serial, a_retries - 1); });
				return;
			}
			if (!OpenCustomMenu(ContractOf(g_pending.kind).swf)) {
				logger::warn("UIExtensions: could not open {}", ContractOf(g_pending.kind).swf);
				Finish(-1);
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
				const auto& c = ContractOf(*kind);
				logger::debug("UIExtensions event {} (str \"{}\", num {}, last instance {})", name, a_event->strArg.c_str(), a_event->numArg, g_instance);
				if (name == c.load) {
					const auto instance = ++g_instance;
					tasks->AddUITask([k = *kind, instance]() { Fill(k, instance); });
				} else if (name == c.result && *kind == Kind::List) {
					// UIListMenu.psc: strArg is the entry id, numArg -1 means "back".
					std::int32_t      index = -1;
					const std::string str{ a_event->strArg.c_str() };
					if (a_event->numArg != -1.0f) {
						std::from_chars(str.data(), str.data() + str.size(), index);
					}
					const auto instance = g_instance;
					tasks->AddTask([instance, index]() { Complete(instance, index); });
				} else if (name == c.result) {
					// UITextEntryMenu.psc: strArg is the entered text.
					const auto instance = g_instance;
					tasks->AddTask([instance, text = std::string(a_event->strArg.c_str())]() { Complete(instance, 0, text); });
				} else {
					// A result is reported before the menu closes; only a close without one is a cancel. Run
					// a frame later so a result event queued in the same frame wins.
					const auto instance = g_instance;
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

	bool OpenList(std::vector<ListEntry> a_entries, ChoiceCallback a_callback)
	{
		if (!IsInstalled() || g_pending.open || a_entries.empty()) {
			return false;
		}
		if (a_entries.size() > kMaxEntries) {
			logger::warn("UIExtensions: list has {} entries, only {} are shown", a_entries.size(), kMaxEntries);
			a_entries.resize(kMaxEntries);
		}
		g_pending = {};
		g_pending.open = true;
		g_pending.serial = ++g_serial;
		g_pending.entries = std::move(a_entries);
		g_pending.callback = std::move(a_callback);
		logger::debug("UIExtensions: list request {} ({} entries)", g_pending.serial, g_pending.entries.size());
		TryOpen(g_pending.serial, 40);
		return true;
	}

	bool OpenTextEntry(std::string a_initial, TextCallback a_callback)
	{
		if (!IsInstalled() || g_pending.open) {
			return false;
		}
		g_pending = {};
		g_pending.open = true;
		g_pending.kind = Kind::Text;
		g_pending.serial = ++g_serial;
		g_pending.text = std::move(a_initial);
		g_pending.textCallback = std::move(a_callback);
		logger::debug("UIExtensions: text entry request {}", g_pending.serial);
		TryOpen(g_pending.serial, 40);
		return true;
	}
}
