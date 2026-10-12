#include "Sort/DisplayFiller.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Game/ItemMover.h"
#include "Log.h"
#include "Settings.h"

namespace dova::display
{
	namespace
	{
		constexpr auto kTick = std::chrono::milliseconds(100);
		constexpr int  kRackTicks = 50;    // wait up to 5 s for dropped weapons to get their 3D
		constexpr int  kPlaqueTicks = 30;  // wait up to 3 s for a plaque's script

		std::atomic<bool>    g_busy{ false };
		std::set<RE::FormID> g_failed;  // plaques that did not take their item (this session)

		void Later(std::function<void()> a_fn)
		{
			std::thread([fn = std::move(a_fn)]() mutable {
				std::this_thread::sleep_for(kTick);
				SKSE::GetTaskInterface()->AddTask(std::move(fn));
			}).detach();
		}

		RE::TESObjectREFR* Ref(RE::FormID a_id)
		{
			return a_id == game::kPlayerRefID ? game::Player() : RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
		}

		std::string Holder(RE::FormID a_id)
		{
			return a_id == game::kPlayerRefID ? "<player>" : game::Describe(Ref(a_id));
		}

		// A copy the plaque's script could take instead of the plain one it was given: the script removes
		// "one of this item", and the copy it takes is destroyed (the plaque recreates a plain one later).
		bool HasSpecialCopy(RE::TESObjectREFR* a_holder, RE::TESBoundObject* a_object)
		{
			for (auto* x : game::ExtraStacks(a_holder, a_object)) {
				if (game::IsProtectedExtraList(x) || x->HasType<RE::ExtraHealth>() || x->HasType<RE::ExtraTextDisplayData>() ||
					x->HasType<RE::ExtraEnchantment>() || x->HasType<RE::ExtraPoison>()) {
					return true;
				}
			}
			return false;
		}

		struct Step
		{
			game::FoundDisplay  display;
			game::StackRef      stack;
			std::string         item;   // "Iron Sword" (logs)
			std::string         pivot;  // Rack
			RE::ObjectRefHandle dropped;
			bool                done{ false };
			bool                broughtToPlayer{ false };  // Item: came from a container
			int                 reactivations{ 0 };
			std::int32_t        before{ 0 };
		};

		class Job : public std::enable_shared_from_this<Job>
		{
		public:
			Job(RE::FormID a_cellId, std::vector<Step> a_steps) :
				_cellId(a_cellId)
			{
				for (auto& s : a_steps) {
					switch (s.display.kind) {
					case core::DisplayKind::Rack:
						_racks.push_back(std::move(s));
						break;
					case core::DisplayKind::Item:
						_plaques.push_back(std::move(s));
						break;
					default:
						_direct.push_back(std::move(s));
						break;
					}
				}
			}

			void Start()
			{
				{
					game::ItemMover::SuppressScope suppress;
					for (auto& s : _direct) {
						MoveInto(s);
					}
					for (auto& s : _racks) {
						Drop(s);
					}
				}
				PollRacks(0);
			}

		private:
			bool StillHere() const
			{
				auto* player = game::Player();
				auto* cell = player ? player->GetParentCell() : nullptr;
				return cell && cell->GetFormID() == _cellId;
			}

			bool Movable(const Step& a_step, RE::TESObjectREFR* a_source) const
			{
				const auto live = game::FindLiveStack(a_source, a_step.stack.object, a_step.stack.extra);
				if (!live.found || live.count < 1 || live.isProtected) {
					logger::info("Display skipped: {} is no longer in {} (or is worn, favorited or a quest item)", a_step.item, Holder(a_step.stack.owner));
					return false;
				}
				return true;
			}

			void Placed(const Step& a_step)
			{
				++_placed;
				logger::debug("Display: {} from {} on {} ({})", a_step.item, Holder(a_step.stack.owner), game::Describe(Ref(a_step.display.ref)),
					core::DisplayKindName(a_step.display.kind));
			}

			// Mannequins and cases: a plain transfer into the display's inventory; its script does the rest.
			void MoveInto(Step& a_step)
			{
				a_step.done = true;
				auto* source = Ref(a_step.stack.owner);
				auto* target = Ref(a_step.display.ref);
				if (!source || !target || !Movable(a_step, source)) {
					++_skipped;
					return;
				}
				const auto before = game::LiveCount(target, a_step.stack.object);
				source->RemoveItem(a_step.stack.object, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, a_step.stack.extra, target);
				if (game::LiveCount(target, a_step.stack.object) != before + 1) {
					++_errors;
					logger::error("Display: moving {} from {} to {} did not arrive as expected (see the inventory journal)", a_step.item,
						Holder(a_step.stack.owner), game::Describe(target));
					return;
				}
				Placed(a_step);
			}

			// Racks: drop the copy at the pivot node (WeaponRackActivateSCRIPT drops the equipped weapon).
			void Drop(Step& a_step)
			{
				auto* source = Ref(a_step.stack.owner);
				auto* rack = Ref(a_step.display.ref);
				auto* trigger = Ref(a_step.display.trigger);
				auto* root = trigger ? trigger->Get3D() : nullptr;
				auto* node = root ? root->GetObjectByName(a_step.pivot) : nullptr;
				if (!source || !rack || !node || rack->IsDisabled() || !Movable(a_step, source)) {
					if (!node && source && rack) {
						logger::warn("Display skipped: {} has no {} node for {}", game::Describe(rack), a_step.pivot, a_step.item);
					}
					a_step.done = true;
					++_skipped;
					return;
				}
				RE::NiPoint3 angle;
				node->world.rotate.ToEulerAnglesXYZ(angle);
				const auto position = node->world.translate;
				const auto before = game::LiveCount(source, a_step.stack.object);
				a_step.dropped = source->RemoveItem(a_step.stack.object, 1, RE::ITEM_REMOVE_REASON::kDropping, a_step.stack.extra, nullptr, &position, &angle);
				const auto after = game::LiveCount(source, a_step.stack.object);
				if (!a_step.dropped.get()) {
					a_step.done = true;
					++_errors;
					logger::error("Display: dropping {} from {} for {} gave no reference (count {} -> {})", a_step.item, Holder(a_step.stack.owner),
						game::Describe(rack), before, after);
				}
			}

			void Settle(Step& a_step, bool a_loaded)
			{
				a_step.done = true;
				auto  ref = a_step.dropped.get();
				auto* trigger = Ref(a_step.display.trigger);
				if (!ref || !trigger) {
					++_errors;
					logger::error("Display: {} for {} disappeared before it was placed", a_step.item, game::Describe(Ref(a_step.display.ref)));
					return;
				}
				if (a_loaded) {
					ref->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true);
				} else {
					logger::warn("Display: {} has no 3D after {} s; placed without fixing it in place", a_step.item, kRackTicks / 10);
				}
				ref->MoveToNode(trigger, a_step.pivot);
				Placed(a_step);
			}

			void PollRacks(int a_tick)
			{
				if (!StillHere()) {
					return Abort();
				}
				bool waiting = false;
				for (auto& s : _racks) {
					if (s.done) {
						continue;
					}
					auto ref = s.dropped.get();
					if (ref && ref->Is3DLoaded()) {
						Settle(s, true);
					} else if (a_tick >= kRackTicks) {
						Settle(s, false);
					} else {
						waiting = true;
					}
				}
				if (waiting) {
					Later([self = shared_from_this(), a_tick]() { self->PollRacks(a_tick + 1); });
					return;
				}
				NextPlaque(0);
			}

			// Plaques: one at a time, activated for the player with the item in the inventory.
			void NextPlaque(std::size_t a_index)
			{
				if (!StillHere()) {
					return Abort();
				}
				if (a_index >= _plaques.size()) {
					return Finish();
				}
				auto& s = _plaques[a_index];
				auto* player = game::Player();
				auto* plaque = Ref(s.display.ref);
				auto* source = Ref(s.stack.owner);
				if (!plaque || !source || !Movable(s, source)) {
					++_skipped;
					return NextPlaque(a_index + 1);
				}
				if (HasSpecialCopy(player, s.stack.object)) {
					logger::info("Display skipped: you carry a tempered, renamed, enchanted, worn or favorited {}; {} could take that copy, so place it yourself",
						s.item, game::Describe(plaque));
					++_skipped;
					return NextPlaque(a_index + 1);
				}
				if (source != player) {
					game::ItemMover::SuppressScope suppress;
					const auto                     before = game::LiveCount(player, s.stack.object);
					source->RemoveItem(s.stack.object, 1, RE::ITEM_REMOVE_REASON::kRemove, s.stack.extra, player);
					if (game::LiveCount(player, s.stack.object) != before + 1) {
						++_errors;
						logger::error("Display: could not take {} from {} for {}", s.item, Holder(s.stack.owner), game::Describe(plaque));
						return NextPlaque(a_index + 1);
					}
					s.broughtToPlayer = true;
				}
				s.before = game::LiveCount(player, s.stack.object);
				plaque->ActivateRef(player, 0, nullptr, 0, false);
				Later([self = shared_from_this(), a_index]() { self->PollPlaque(a_index, 1); });
			}

			void PollPlaque(std::size_t a_index, int a_tick)
			{
				if (!StillHere()) {
					return Abort();
				}
				auto&      s = _plaques[a_index];
				auto*      player = game::Player();
				auto*      plaque = Ref(s.display.ref);
				const auto now = game::LiveCount(player, s.stack.object);
				if (now < s.before) {
					Placed(s);
					return NextPlaque(a_index + 1);
				}
				if (now > s.before && plaque && s.reactivations == 0) {
					// It was full and handed its item back: activate again to put it back up.
					logger::debug("Display: {} gave back {} (it was full); activating it again", game::Describe(plaque), s.item);
					++s.reactivations;
					s.before = now;
					plaque->ActivateRef(player, 0, nullptr, 0, false);
					Later([self = shared_from_this(), a_index]() { self->PollPlaque(a_index, 1); });
					return;
				}
				if (a_tick < kPlaqueTicks) {
					Later([self = shared_from_this(), a_index, a_tick]() { self->PollPlaque(a_index, a_tick + 1); });
					return;
				}
				++_errors;
				g_failed.insert(s.display.ref);
				logger::warn("Display: {} did not take {} within {} s; it is left alone for the rest of this session", game::Describe(plaque), s.item,
					kPlaqueTicks / 10);
				if (s.broughtToPlayer) {
					if (auto* container = Ref(s.stack.owner)) {
						game::ItemMover::SuppressScope suppress;
						player->RemoveItem(s.stack.object, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, container);
						logger::info("Display: {} went back to {}", s.item, game::Describe(container));
					}
				}
				NextPlaque(a_index + 1);
			}

			void Abort()
			{
				logger::info("Displays: the player left the home; {} items were put up, the rest stays where it is", _placed);
				g_busy = false;
			}

			void Finish()
			{
				g_busy = false;
				logger::info("Displays: put {} items on display ({} skipped, {} errors)", _placed, _skipped, _errors);
				if (_placed > 0 || _errors > 0) {
					auto message = std::format("Dova Stash: put {} item{} on display", _placed, _placed == 1 ? "" : "s");
					if (_errors > 0) {
						message += std::format(" ({} failed, see log)", _errors);
					}
					Controller::Get().Notify(message);
				}
			}

			RE::FormID        _cellId;
			std::vector<Step> _direct;
			std::vector<Step> _racks;
			std::vector<Step> _plaques;
			std::int32_t      _placed{ 0 };
			std::int32_t      _skipped{ 0 };
			std::int32_t      _errors{ 0 };
		};

		std::optional<core::FormRef> RefOf(RE::FormID a_id)
		{
			return game::ToFormRef(RE::TESForm::LookupByID(a_id));
		}

		std::vector<core::FormRef> Refs(const std::vector<RE::TESBoundObject*>& a_forms)
		{
			std::vector<core::FormRef> out;
			for (auto* f : a_forms) {
				if (auto r = game::ToFormRef(f)) {
					out.push_back(std::move(*r));
				}
			}
			return out;
		}

		// Layout `displays` entries: ignore, add, or change the items of what the scripts revealed.
		void ApplyLayout(const core::Layout& a_layout, std::vector<game::FoundDisplay>& a_found)
		{
			for (const auto& b : a_layout.displays) {
				auto* ref = game::LookupRef(b.ref);
				if (!ref) {
					logger::debug("Displays: layout entry {} does not resolve (plugin missing?)", b.ref.ToString());
					continue;
				}
				auto it = std::ranges::find(a_found, ref->GetFormID(), &game::FoundDisplay::ref);
				if (b.ignored) {
					if (it != a_found.end()) {
						a_found.erase(it);
					}
					continue;
				}
				if (it == a_found.end()) {
					auto declared = b.kind ? game::DeclaredDisplay(ref, *b.kind) : std::nullopt;
					if (!declared) {
						if (log::FirstTime(std::format("display-entry:{:08X}", ref->GetFormID()))) {
							logger::warn("Displays: layout entry {} ({}) is no display Dova Stash can fill{}", b.ref.ToString(), game::Describe(ref),
								b.kind ? "" : " (give it a \"kind\")");
						}
						continue;
					}
					a_found.push_back(std::move(*declared));
					it = std::prev(a_found.end());
				} else if (b.kind && *b.kind != it->kind && log::FirstTime(std::format("display-kind:{:08X}", ref->GetFormID()))) {
					logger::warn("Displays: layout says {} is a {}, its script makes it a {}; using {}", game::Describe(ref), core::DisplayKindName(*b.kind),
						core::DisplayKindName(it->kind), core::DisplayKindName(it->kind));
				}
				if (!b.items.empty()) {
					it->items.clear();
					for (const auto& f : b.items) {
						if (auto* form = game::LookupForm(f); form && form->As<RE::TESBoundObject>()) {
							it->items.push_back(form->As<RE::TESBoundObject>());
						}
					}
				}
			}
		}
	}

	bool Busy() noexcept
	{
		return g_busy.load();
	}

	Prepared Prepare(const home::Home& a_home, RE::TESObjectCELL* a_cell, std::vector<core::DisplayStock> a_stocks)
	{
		Prepared out;
		if (!Settings::GetSingleton().GetSorting().fillDisplays || !a_home.layout || !a_cell) {
			return out;
		}
		if (Busy()) {
			logger::info("Displays: still putting items up from the last sort; skipped this time");
			return out;
		}
		const auto& layout = *a_home.layout;
		auto        found = game::FindDisplays(a_cell);
		ApplyLayout(layout, found);
		std::erase_if(found, [&](const game::FoundDisplay& d) {
			if (g_failed.contains(d.ref)) {
				return true;
			}
			if (d.kind != core::DisplayKind::Case) {
				return false;
			}
			// A container the layout uses for storage is a container, not a case.
			const auto ref = RefOf(d.ref);
			const auto* binding = ref ? layout.FindContainer(*ref) : nullptr;
			return ref && (layout.IsMaster(*ref) || (binding && !binding->ignored));
		});
		if (found.empty()) {
			return out;
		}

		std::set<core::FormRef> shown;
		for (const auto id : game::LooseGear(a_cell)) {
			if (auto r = RefOf(id)) {
				shown.insert(std::move(*r));
			}
		}

		std::map<core::DisplayKind, std::pair<int, int>> counts;  // kind -> (available, empty)
		for (auto& d : found) {
			if (!game::DisplayAvailable(d)) {
				continue;
			}
			auto ref = RefOf(d.ref);
			if (!ref) {
				continue;
			}
			core::DisplaySpot spot;
			spot.ref = std::move(*ref);
			spot.kind = d.kind;
			spot.rack = d.rack;
			spot.items = Refs(d.items);
			spot.full = game::DisplayFull(d);
			const auto holds = game::DisplayHolds(d);
			spot.holds = Refs(holds);
			shown.insert(spot.holds.begin(), spot.holds.end());
			auto& [n, empty] = counts[d.kind];
			++n;
			empty += spot.full ? 0 : 1;
			out.spots.push_back(std::move(spot));
			out.found.push_back(d);
		}
		out.stocks = std::move(a_stocks);
		out.plan = core::PlanDisplays(out.spots, out.stocks, shown);

		std::string summary;
		for (const auto& [kind, n] : counts) {
			summary += std::format("{}{} {} ({} empty)", summary.empty() ? "" : ", ", n.first, core::DisplayKindName(kind), n.second);
		}
		logger::info("Displays in {}: {}; {} item{} to put up", a_home.name, summary.empty() ? "none usable" : summary, out.plan.size(),
			out.plan.size() == 1 ? "" : "s");
		for (const auto& a : out.plan) {
			const auto& stock = out.stocks[a.stock];
			logger::debug("Display plan: {} [{}] from {} -> {} ({})", stock.item.name, stock.item.form.ToString(), stock.from.ToString(),
				game::Describe(RE::TESForm::LookupByID(out.found[a.spot].ref)), core::DisplayKindName(out.found[a.spot].kind));
		}
		return out;
	}

	void Fill(RE::FormID a_cellId, const Prepared& a_prepared, const game::StackTable& a_stacks)
	{
		if (a_prepared.plan.empty() || Busy()) {
			return;
		}
		std::vector<Step> steps;
		for (const auto& a : a_prepared.plan) {
			const auto& stock = a_prepared.stocks[a.stock];
			const auto* stack = a_stacks.Find(stock.item.stackId);
			if (!stack || !stack->object) {
				logger::warn("Display skipped: unknown stack {} ({})", stock.item.stackId, stock.item.form.ToString());
				continue;
			}
			Step s;
			s.display = a_prepared.found[a.spot];
			s.stack = *stack;
			s.item = stock.item.name;
			s.pivot = core::RackPivot(stock.item);
			steps.push_back(std::move(s));
		}
		if (steps.empty()) {
			return;
		}
		g_busy = true;
		std::make_shared<Job>(a_cellId, std::move(steps))->Start();
	}
}
