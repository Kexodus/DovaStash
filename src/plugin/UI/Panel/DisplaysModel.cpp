#include "UI/Panel/DisplaysModel.h"

#include "Events/EventSinks.h"
#include "Game/ArmorIndex.h"
#include "Game/FormUtil.h"
#include "UI/DisplayMenu.h"
#include "UI/Labels.h"
#include "UI/Panel/Strings.h"

#include <mutex>

namespace dova::ui::panel
{
	namespace
	{
		constexpr std::int64_t kRefreshMs = 3000;
		constexpr std::size_t  kMaxNames = 4;  // item names in one cell before "+N"
		constexpr float        kAimReach = 250.0f;  // as the hotkey's (Controller kDisplayReach)

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::string Capitalize(std::string a_text)
		{
			if (!a_text.empty() && a_text[0] >= 'a' && a_text[0] <= 'z') {
				a_text[0] = static_cast<char>(a_text[0] - 'a' + 'A');
			}
			return a_text;
		}

		// "Rack", "Mannequin", "Plaque", "Display case", "Bookshelf"; a kind added later goes by its own name.
		std::string KindName(core::DisplayKind a_kind)
		{
			switch (a_kind) {
			case core::DisplayKind::Rack:
				return text::kDispKindRack;
			case core::DisplayKind::Mannequin:
				return text::kDispKindMannequin;
			case core::DisplayKind::Item:
				return text::kDispKindItem;
			case core::DisplayKind::Case:
				return text::kDispKindCase;
			case core::DisplayKind::Bookshelf:
				return text::kDispKindBookshelf;
			default:
				return Capitalize(std::string(core::DisplayKindName(a_kind)));
			}
		}

		// "racks", "display cases", "bookshelves"; a kind added later by English rules.
		std::string KindPlural(core::DisplayKind a_kind)
		{
			switch (a_kind) {
			case core::DisplayKind::Rack:
				return text::kDispKindRacks;
			case core::DisplayKind::Mannequin:
				return text::kDispKindMannequins;
			case core::DisplayKind::Item:
				return text::kDispKindPlaques;
			case core::DisplayKind::Case:
				return text::kDispKindCases;
			case core::DisplayKind::Bookshelf:
				return text::kDispKindBookshelves;
			default:
				break;
			}
			auto name = core::ToLower(KindName(a_kind));
			if (name.ends_with('f')) {
				name.pop_back();
				return name + "ves";
			}
			if (name.ends_with('s') || name.ends_with('x') || name.ends_with("ch") || name.ends_with("sh")) {
				return name + "es";
			}
			return name + "s";
		}

		std::string ObjectName(const RE::TESForm* a_form)
		{
			const char* name = a_form ? a_form->GetName() : nullptr;
			return name && *name ? std::string(name) : std::string{};
		}

		std::string FormName(const core::FormRef& a_form)
		{
			auto name = ObjectName(game::LookupForm(a_form));
			return name.empty() ? a_form.ToString() : name;
		}

		// "Ebony Cuirass, Ebony Boots +2".
		std::string Names(const std::vector<std::string>& a_names)
		{
			std::string out;
			for (std::size_t i = 0; i < a_names.size(); ++i) {
				if (i == kMaxNames) {
					out += std::format(" +{}", a_names.size() - i);
					break;
				}
				out += (i ? ", " : "") + a_names[i];
			}
			return out;
		}

		void Append(std::string& a_out, const std::string& a_part)
		{
			if (!a_part.empty()) {
				a_out += (a_out.empty() ? "" : "; ") + a_part;
			}
		}
	}

	DisplaysModel& DisplaysModel::Get()
	{
		static DisplaysModel model;
		return model;
	}

	std::shared_ptr<const DisplaysSnapshot> DisplaysModel::Snapshot() const
	{
		std::scoped_lock lock(_lock);
		return _snapshot;
	}

	void DisplaysModel::RequestRefresh(bool a_now, bool a_rescan)
	{
		if (a_rescan) {
			_rescan = true;
		}
		if (!a_now && Snapshot() && NowMs() - _lastMs.load() < kRefreshMs) {
			return;
		}
		if (_queued.exchange(true)) {
			return;
		}
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			_queued = false;
			return;
		}
		tasks->AddTask([this]() {
			_queued = false;
			Rebuild();
		});
	}

	void DisplaysModel::Post(std::function<void()> a_action)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}
		tasks->AddTask([this, action = std::move(a_action)]() {
			if (action) {
				action();
			}
			Rebuild();
		});
	}

	void DisplaysModel::Rebuild()
	{
		_lastMs = NowMs();
		auto snap = std::make_shared<DisplaysSnapshot>();
		snap->serial = ++_serial;
		const auto publish = [&]() {
			std::scoped_lock lock(_lock);
			_snapshot = std::move(snap);
		};

		auto* home = home::HomeRegistry::Get().Current();
		auto* player = game::Player();
		auto* here = player ? player->GetParentCell() : nullptr;
		if (!home || !here) {
			_scannedCells.clear();
			_found.clear();
			publish();
			return;
		}
		snap->inHome = true;
		snap->enabled = home->Enabled();
		snap->broken = home->layoutBroken;
		snap->multiCell = home->MultiCell();
		snap->cellId = home->cellId;
		snap->homeName = home->name;
		if (const auto* area = snap->multiCell ? home->HereArea() : nullptr) {
			snap->hereArea = area->name;
		}

		// Where to look: the player's cell, then the home's other cells that are loaded (attached). Scanned
		// again only when that changes or on Refresh.
		std::vector<RE::FormID> cells{ here->GetFormID() };
		for (const auto& area : home->areas) {
			if (area.id == 0 || area.id == here->GetFormID()) {
				continue;
			}
			const auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(area.id);
			if (cell && cell->IsAttached()) {
				cells.push_back(area.id);
			}
		}
		if (_rescan.exchange(false) || cells != _scannedCells) {
			_scannedCells = cells;
			_found.clear();
			_fits.clear();
			for (const auto id : cells) {
				for (auto& d : game::FindDisplays(RE::TESForm::LookupByID<RE::TESObjectCELL>(id))) {
					_found.push_back(std::move(d));
				}
			}
			logger::debug("Displays page: {} display(s) in {} loaded cell(s) of {}", _found.size(), cells.size(), home->name);
		}

		const auto*                             layout = home->layout ? &*home->layout : nullptr;
		std::shared_ptr<const game::ArmorIndex> index;  // only once a mannequin names a set (built lazily)
		std::set<core::FormRef>                 seen;

		const auto addRow = [&](const std::optional<game::FoundDisplay>& a_found, const core::FormRef& a_ref, RE::TESObjectREFR* a_object,
								const core::DisplayBinding* a_binding) {
			DisplayRow row;
			row.ref = a_ref;
			row.refText = a_ref.ToString();
			row.kind = a_found ? std::optional(a_found->kind) : a_binding ? a_binding->kind : std::nullopt;
			row.kindName = row.kind ? KindName(*row.kind) : std::string{};
			if (a_found) {
				auto it = _fits.find(a_found->ref);
				if (it == _fits.end()) {
					it = _fits.emplace(a_found->ref, game::FitOf(*a_found)).first;
				}
				row.fit = it->second;
			} else if (row.kind) {
				row.fit = core::DisplayFit{ *row.kind, std::nullopt, true };  // not loaded here: what any display of its kind holds
			}
			row.loaded = a_found.has_value();
			row.here = row.loaded && a_object && a_object->GetParentCell() == here;
			row.available = row.loaded && game::DisplayAvailable(*a_found);
			if (snap->multiCell && a_object) {
				if (const auto* area = home::AreaOf(*home, a_object)) {
					row.area = area->name;
				}
			}
			row.name = a_binding && !a_binding->label.empty() ? a_binding->label : a_object ? game::RefName(a_object) : std::string{};
			if (row.name.empty()) {
				row.name = row.refText;
			}
			row.listed = a_binding != nullptr;
			row.ignored = a_binding && a_binding->ignored;
			row.used = row.listed && !row.ignored;
			row.mannequin = row.kind == core::DisplayKind::Mannequin;

			// What it is made for (plaques, cases) or prefers (racks, mannequins): the entry's items win, as
			// when it is filled.
			std::optional<game::FoundDisplay> state = a_found;
			if (a_binding) {
				row.categories = a_binding->categories;
				row.only = a_binding->only && !a_binding->items.empty();
				row.hasSet = a_binding->set.has_value();
				row.set = a_binding->set;
				if (!a_binding->items.empty()) {
					if (state) {
						state->items.clear();
					}
					for (const auto& f : a_binding->items) {
						row.madeFor.push_back(FormName(f));
						if (auto* form = state ? game::LookupForm(f) : nullptr; form && form->As<RE::TESBoundObject>()) {
							state->items.push_back(form->As<RE::TESBoundObject>());
						}
					}
				}
			}
			if (row.madeFor.empty() && a_found) {
				for (const auto* item : a_found->items) {
					if (auto name = ObjectName(item); !name.empty()) {
						row.madeFor.push_back(std::move(name));
					}
				}
			}
			if (row.mannequin) {
				if (row.hasSet) {
					if (!index) {
						index = game::GetArmorIndex();
					}
					row.setName = game::SetName(*index, *a_binding->set);
				} else {
					row.setName = text::kAuto;
				}
			}
			for (const auto& p : row.categories) {
				row.categoriesText += (row.categoriesText.empty() ? "" : ", ") + PatternShortLabel(p);
			}

			// What it takes, in one line.
			const auto madeFor = Names(row.madeFor);
			const auto preferred = row.madeFor.empty() ? std::string{} : std::vformat(row.only ? text::kDispOnly : text::kDispPrefers, std::make_format_args(madeFor));
			switch (row.kind.value_or(core::DisplayKind::Item)) {
			case core::DisplayKind::Rack:
				Append(row.takesText, row.categoriesText.empty() ? std::string(text::kDispAnyWeapon) : row.categoriesText);
				Append(row.takesText, preferred);
				break;
			case core::DisplayKind::Mannequin:
				Append(row.takesText, row.hasSet ? std::format(text::kDispSet, row.setName) : row.categoriesText.empty() ? std::string(text::kDispAnyArmor) : row.categoriesText);
				if (!row.hasSet) {
					Append(row.takesText, preferred);
				}
				break;
			case core::DisplayKind::Item:
			case core::DisplayKind::Case:
				Append(row.takesText, Names(row.madeFor));
				break;
			default:
				Append(row.takesText, row.categoriesText);
				Append(row.takesText, Names(row.madeFor));
				break;
			}

			// What it holds now.
			if (state) {
				row.full = game::DisplayFullFor(*state, a_binding);
				std::vector<std::string> holds;
				for (const auto* item : game::DisplayHolds(*state)) {
					if (auto name = ObjectName(item); !name.empty()) {
						holds.push_back(std::move(name));
					}
				}
				if (!holds.empty()) {
					row.holdsText = Names(holds);
				} else if (row.full) {
					// A placed plaque shows its one item; a rack's weapon is not looked up.
					row.holdsText = row.kind == core::DisplayKind::Item && row.madeFor.size() == 1 ? row.madeFor.front() : std::string(text::kDispFull);
				} else {
					row.holdsText = text::kDispEmpty;
				}
				row.empty = holds.empty() && !row.full;
			}

			row.haystack = core::ToLower(std::format("{}\n{}\n{}\n{}\n{}\n{}", row.name, row.kindName, row.area, row.takesText, row.holdsText, row.refText));
			for (const auto& m : row.madeFor) {
				row.haystack += '\n' + core::ToLower(m);
			}
			seen.insert(a_ref);
			snap->rows.push_back(std::move(row));
		};

		for (const auto& cached : _found) {
			auto* object = RE::TESForm::LookupByID<RE::TESObjectREFR>(cached.ref);
			const auto ref = game::ToFormRef(object);
			if (!ref || seen.contains(*ref)) {
				continue;
			}
			auto d = cached;
			switch (d.kind) {
			case core::DisplayKind::Rack:
			case core::DisplayKind::Mannequin:
			case core::DisplayKind::Case:
				break;  // their state is read from the reference itself
			default:
				// A plaque's "placed" flag (and any later kind's state) lives in its script: read it again.
				if (auto fresh = game::InspectDisplay(object)) {
					d = std::move(*fresh);
				}
				break;
			}
			if (d.kind == core::DisplayKind::Case && layout) {
				// A container the layout uses for storage is a container, not a case.
				const auto* container = layout->FindContainer(*ref);
				if (layout->IsMaster(*ref) || (container && !container->ignored)) {
					continue;
				}
			}
			addRow(d, *ref, object, layout ? layout->FindDisplay(*ref) : nullptr);
		}
		// The layout's entries not found above: declared with `kind` (the scripts do not reveal them), or in an
		// area that is not loaded.
		if (layout) {
			for (const auto& b : layout->displays) {
				if (seen.contains(b.ref)) {
					continue;
				}
				auto*                             object = game::LookupRef(b.ref);
				const auto*                       cell = object ? object->GetParentCell() : nullptr;
				std::optional<game::FoundDisplay> d;
				if (b.kind && cell && std::ranges::find(cells, cell->GetFormID()) != cells.end()) {
					d = game::DeclaredDisplay(object, *b.kind);
				}
				addRow(d, b.ref, object, &b);
			}
		}

		// The display the player looks at: resolved like the hotkey's target (DisplayAt reads scripts, so only when the
		// crosshair target changes); with nothing in the crosshair, an unnamed mannequin in view.
		const auto handle = events::CrosshairTarget();
		if (handle != _aimedHandle || !handle) {
			_aimedHandle = handle;
			_aimed.reset();
			auto  hold = game::RefFromHandle(handle);
			auto* target = hold.get();
			if (!target && here) {
				target = game::DisplayInView(here, kAimReach);
			}
			if (target && target->GetParentCell() == here) {
				if (const auto found = game::DisplayAt(target)) {
					_aimed = game::ToFormRef(RE::TESForm::LookupByID<RE::TESObjectREFR>(found->ref));
				}
			}
		}
		if (_aimed) {
			if (const auto it = std::ranges::find(snap->rows, *_aimed, &DisplayRow::ref); it != snap->rows.end()) {
				snap->aimedRow = static_cast<std::size_t>(it - snap->rows.begin());
			}
		}

		std::map<core::DisplayKind, DisplayKindInfo> kinds;
		for (const auto& row : snap->rows) {
			if (!row.kind) {
				continue;
			}
			auto& info = kinds[*row.kind];
			info.kind = *row.kind;
			++info.total;
			if (!row.listed && row.loaded) {
				++info.unlisted;
				info.unlistedRefs.push_back(row.ref);
			}
			if (row.ignored) {
				++info.ignored;
			}
		}
		for (auto& [kind, info] : kinds) {
			info.name = KindName(kind);
			info.plural = KindPlural(kind);
			snap->kinds.push_back(std::move(info));
		}
		for (const auto& row : snap->rows) {
			if (!row.fit || snap->choices.contains(*row.fit)) {
				continue;
			}
			std::vector<DisplayChoice> choices;
			for (const auto& p : DisplayCategoryChoices(*row.fit)) {
				choices.push_back(DisplayChoice{ p, PatternLabel(p) });
			}
			if (!choices.empty()) {
				snap->choices.emplace(*row.fit, std::move(choices));
			}
		}
		publish();
	}
}
