#include "Sort/DisplayFiller.h"

#include "Controller.h"
#include "Game/FormUtil.h"
#include "Game/ItemMover.h"
#include "Home/Store.h"
#include "Log.h"
#include "Settings.h"

namespace dova::display
{
	namespace
	{
		constexpr auto kTick = std::chrono::milliseconds(100);
		constexpr int  kRackTicks = 50;     // wait up to 5 s for a dropped weapon to get its 3D
		constexpr int  kConfirmTicks = 20;  // wait up to 2 s for a rack's trigger to notice the weapon
		constexpr int  kPlaqueTicks = 30;   // wait up to 3 s for a plaque's script

		std::atomic<bool>    g_busy{ false };
		std::set<RE::FormID> g_failed;  // racks and plaques that did not take their item (this session)

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
			RE::ObjectRefHandle dropped;  // Rack: the weapon in the world
			bool                done{ false };
			bool                broughtToPlayer{ false };  // Item: came from a container
			int                 reactivations{ 0 };
			std::int32_t        before{ 0 };
		};

		// Where a weapon from a container is dropped before it goes up: in the open in front of the player,
		// like the equipped weapon WeaponRackActivateSCRIPT drops (DropObject).
		RE::NiPoint3 InFrontOfPlayer()
		{
			auto*      player = game::Player();
			auto       spot = player->GetPosition();
			const auto yaw = player->data.angle.z;
			spot.x += std::sin(yaw) * 60.0f;
			spot.y += std::cos(yaw) * 60.0f;
			spot.z += 80.0f;
			return spot;
		}

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
				}
				NextRack(0);
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
				g_failed.erase(a_step.display.ref);
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

			// Racks, one at a time, the way WeaponRackActivateSCRIPT places the equipped weapon: drop the very
			// copy in the open, wait for its 3D, make it keyframed and move it onto the trigger's pivot node.
			// Then the script's bookkeeping (game::DisableRack, game::RecordRackItem): the activator is
			// disabled (that is how the game knows the rack holds something, and lets the player take the
			// weapon down). If the activator stays enabled, the weapon is taken back.
			void NextRack(std::size_t a_index)
			{
				if (!StillHere()) {
					return Abort();
				}
				if (a_index >= _racks.size()) {
					return NextPlaque(0);
				}
				auto& s = _racks[a_index];
				auto* player = game::Player();
				auto* source = Ref(s.stack.owner);
				auto* rack = Ref(s.display.ref);
				auto* trigger = Ref(s.display.trigger);
				auto* root = trigger ? trigger->Get3D() : nullptr;
				auto* node = root ? root->GetObjectByName(s.pivot) : nullptr;
				if (!source || !rack || !node || rack->IsDisabled() || !Movable(s, source)) {
					if (!node && source && rack) {
						logger::warn("Display skipped: {} has no {} node for {}", game::Describe(rack), s.pivot, s.item);
					}
					s.done = true;
					++_skipped;
					return NextRack(a_index + 1);
				}
				{
					game::ItemMover::SuppressScope suppress;
					if (source == player) {
						s.dropped = player->RemoveItem(s.stack.object, 1, RE::ITEM_REMOVE_REASON::kDropping, s.stack.extra, nullptr);
					} else {
						const auto spot = InFrontOfPlayer();
						s.dropped = source->RemoveItem(s.stack.object, 1, RE::ITEM_REMOVE_REASON::kDropping, s.stack.extra, nullptr, &spot);
					}
				}
				if (!s.dropped.get()) {
					s.done = true;
					++_errors;
					logger::error("Display: dropping {} from {} for {} gave no reference (see the inventory journal)", s.item, Holder(s.stack.owner),
						game::Describe(rack));
					return NextRack(a_index + 1);
				}
				Later([self = shared_from_this(), a_index]() { self->WaitFor3D(a_index, 1); });
			}

			void WaitFor3D(std::size_t a_index, int a_tick)
			{
				if (!StillHere()) {
					return Abort();
				}
				auto& s = _racks[a_index];
				auto  ref = s.dropped.get();
				auto* trigger = Ref(s.display.trigger);
				if (!ref || !trigger) {
					s.done = true;
					++_errors;
					logger::error("Display: {} for {} disappeared before it was placed", s.item, game::Describe(Ref(s.display.ref)));
					return NextRack(a_index + 1);
				}
				if (!ref->Is3DLoaded()) {
					if (a_tick < kRackTicks) {
						Later([self = shared_from_this(), a_index, a_tick]() { self->WaitFor3D(a_index, a_tick + 1); });
						return;
					}
					logger::warn("Display: {} had no 3D after {} s, so it can't be fixed on {}", s.item, kRackTicks / 10, game::Describe(Ref(s.display.ref)));
					TakeBack(s);
					return NextRack(a_index + 1);
				}
				ref->SetMotionType(RE::hkpMotion::MotionType::kKeyframed, true);
				ref->MoveToNode(trigger, s.pivot);
				Later([self = shared_from_this(), a_index]() { self->ConfirmRack(a_index, 1); });
			}

			// A tick after the move (like the script's own short wait, so the trigger events of the move
			// are past), the bookkeeping the rack's script does; a rack without that script must have its
			// trigger disable the activator.
			void ConfirmRack(std::size_t a_index, int a_tick)
			{
				if (!StillHere()) {
					return Abort();
				}
				auto& s = _racks[a_index];
				auto* rack = Ref(s.display.ref);
				if (a_tick == 1) {
					game::DisableRack(s.display);
				}
				if (rack && rack->IsDisabled()) {
					if (auto ref = s.dropped.get()) {
						game::RecordRackItem(s.display, ref.get());
					}
					s.done = true;
					Placed(s);
					return NextRack(a_index + 1);
				}
				if (a_tick < kConfirmTicks) {
					Later([self = shared_from_this(), a_index, a_tick]() { self->ConfirmRack(a_index, a_tick + 1); });
					return;
				}
				g_failed.insert(s.display.ref);
				logger::warn("Display: {} did not take {} within {} s (its activator is still enabled), so the game would not let you take it down; "
							 "Deposit and Sort all leave it alone for the rest of this session",
					game::Describe(rack), s.item, kConfirmTicks / 10);
				TakeBack(s);
				NextRack(a_index + 1);
			}

			// A weapon that could not go up properly goes into the player's inventory (with its extra data).
			void TakeBack(Step& a_step)
			{
				a_step.done = true;
				++_errors;
				auto ref = a_step.dropped.get();
				auto* player = game::Player();
				if (!ref || !player) {
					return;
				}
				player->PickUpObject(ref.get(), 1, false, false);
				++_takenBack;
				logger::info("Display: {} is in your inventory now", a_step.item);
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
				logger::warn("Display: {} did not take {} within {} s; Deposit and Sort all leave it alone for the rest of this session",
					game::Describe(plaque), s.item, kPlaqueTicks / 10);
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
				// A weapon dropped for a rack and not up yet must not stay on the floor.
				for (auto& s : _racks) {
					if (!s.done && s.dropped.get()) {
						TakeBack(s);
					}
				}
				logger::info("Displays: the player left the home; {} items were put up, the rest stays where it is", _placed);
				g_busy = false;
			}

			void Finish()
			{
				g_busy = false;
				logger::info("Displays: put {} items on display ({} skipped, {} errors)", _placed, _skipped, _errors);
				if (_placed > 0 || _errors > 0) {
					auto message = std::format("Dova Stash: put {} item{} on display", _placed, _placed == 1 ? "" : "s");
					if (_takenBack > 0) {
						message += std::format(" ({} couldn't go up and {} in your inventory)", _takenBack, _takenBack == 1 ? "is" : "are");
					} else if (_errors > 0) {
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
			std::int32_t      _takenBack{ 0 };
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

		// The displays the layout uses: its `displays` entries that are not ignored (turned on with the hotkey,
		// or listed by a template). An entry can change the items of what the scripts revealed, or add a
		// display they don't reveal (it then needs `kind`).
		std::vector<game::FoundDisplay> UsedDisplays(const core::Layout& a_layout, const std::vector<game::FoundDisplay>& a_found)
		{
			std::vector<game::FoundDisplay> out;
			for (const auto& b : a_layout.displays) {
				if (b.ignored) {
					continue;
				}
				auto* ref = game::LookupRef(b.ref);
				if (!ref) {
					logger::debug("Displays: layout entry {} does not resolve (plugin missing?)", b.ref.ToString());
					continue;
				}
				if (std::ranges::find(out, ref->GetFormID(), &game::FoundDisplay::ref) != out.end()) {
					continue;
				}
				const auto it = std::ranges::find(a_found, ref->GetFormID(), &game::FoundDisplay::ref);
				std::optional<game::FoundDisplay> d;
				if (it != a_found.end()) {
					d = *it;
					if (b.kind && *b.kind != d->kind && log::FirstTime(std::format("display-kind:{:08X}", ref->GetFormID()))) {
						logger::warn("Displays: layout says {} is a {}, its script makes it a {}; using {}", game::Describe(ref),
							core::DisplayKindName(*b.kind), core::DisplayKindName(d->kind), core::DisplayKindName(d->kind));
					}
				} else {
					d = b.kind ? game::DeclaredDisplay(ref, *b.kind) : std::nullopt;
					if (!d) {
						if (log::FirstTime(std::format("display-entry:{:08X}", ref->GetFormID()))) {
							logger::warn("Displays: layout entry {} ({}) is no display Dova Stash can fill{}", b.ref.ToString(), game::Describe(ref),
								b.kind ? "" : " (give it a \"kind\")");
						}
						continue;
					}
				}
				if (!b.items.empty()) {
					d->items.clear();
					for (const auto& f : b.items) {
						if (auto* form = game::LookupForm(f); form && form->As<RE::TESBoundObject>()) {
							d->items.push_back(form->As<RE::TESBoundObject>());
						}
					}
				}
				out.push_back(std::move(*d));
			}
			return out;
		}
	}

	bool Busy() noexcept
	{
		return g_busy.load();
	}

	Prepared Prepare(const home::Home& a_home, RE::TESObjectCELL* a_cell, std::vector<core::DisplayStock> a_stocks, std::optional<RE::FormID> a_only)
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
		if (std::ranges::none_of(layout.displays, [](const core::DisplayBinding& b) { return !b.ignored; })) {
			return out;  // no display turned on: nothing to look for
		}
		auto found = UsedDisplays(layout, game::FindDisplays(a_cell));
		std::erase_if(found, [&](const game::FoundDisplay& d) {
			if (a_only && d.ref != *a_only) {
				return true;
			}
			if (g_failed.contains(d.ref) && !a_only) {
				return true;  // "Fill now" (a_only) tries it again
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
			if (const auto* binding = layout.FindDisplay(spot.ref)) {
				spot.categories = binding->categories;
			}
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
		if (std::ranges::any_of(out.spots, [](const core::DisplaySpot& a_spot) { return !a_spot.categories.empty(); })) {
			const auto& categories = home::Store::Get().Categories();
			for (auto& stock : out.stocks) {
				stock.categories = categories.MatchAll(stock.item);
			}
		}
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
