#include "Game/ItemMover.h"

#include "Game/FormUtil.h"

namespace dova::game
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr auto kSuppressGrace = std::chrono::milliseconds(750);
		constexpr auto kReturnWindow = std::chrono::seconds(5);

		std::atomic<int>                g_suppressDepth{ 0 };
		std::atomic<Clock::rep>         g_suppressUntil{ 0 };
		// Objects for which the engine took a special copy for a plain move: their plain copies stay beside a special
		// copy for the rest of the session. Per object, so one surprise does not stop every other item.
		std::mutex           g_plainLock;
		std::set<RE::FormID> g_plainUnsafe;

		bool PlainUnsafe(RE::FormID a_object)
		{
			std::scoped_lock lock(g_plainLock);
			return g_plainUnsafe.contains(a_object);
		}

		struct Stored
		{
			RE::FormID        object;
			RE::FormID        container;
			Clock::time_point when;
		};
		std::mutex           g_returnLock;
		std::vector<Stored>  g_stored;   // our recent moves into containers (kReturnWindow)
		std::set<RE::FormID> g_returns;  // objects that came back (this session)

		// Records a move of ours. A later container -> player change of the same object only counts as a
		// return when it undoes a store, never when it follows one of our own retrieves.
		void NoteMove(RE::FormID a_object, RE::FormID a_from, RE::FormID a_to)
		{
			const auto       now = Clock::now();
			std::scoped_lock lock(g_returnLock);
			std::erase_if(g_stored, [&](const Stored& s) {
				return now - s.when > kReturnWindow || (s.object == a_object && (s.container == a_to || s.container == a_from));
			});
			if (a_to != kPlayerRefID) {
				g_stored.push_back(Stored{ a_object, a_to, now });
			}
		}

		using ListCounts = std::vector<std::pair<RE::ExtraDataList*, std::int32_t>>;

		ListCounts Counts(const std::vector<RE::ExtraDataList*>& a_lists)
		{
			ListCounts out;
			for (auto* x : a_lists) {
				out.emplace_back(x, x->GetCount());
			}
			return out;
		}

		// After a plain move: every extra-data stack that was in the source must still be there, as large.
		bool SpecialCopiesStayed(RE::TESObjectREFR* a_source, RE::TESBoundObject* a_object, const ListCounts& a_before)
		{
			return std::ranges::all_of(a_before, [&](const auto& a_entry) {
				const auto live = FindLiveStack(a_source, a_object, a_entry.first);
				return live.found && live.count >= a_entry.second;
			});
		}

		// The engine took an extra-data copy for a plain move: moves every extra-data stack that is new (or
		// grew) in the target back to the source. Returns the number of items put back.
		std::int32_t PutBack(RE::TESObjectREFR* a_source, RE::TESObjectREFR* a_target, RE::TESBoundObject* a_object, const ListCounts& a_targetBefore)
		{
			std::int32_t back = 0;
			const auto   reason = a_source->GetFormID() == kPlayerRefID ? RE::ITEM_REMOVE_REASON::kRemove : RE::ITEM_REMOVE_REASON::kStoreInContainer;
			for (auto* x : SpecialStacks(a_target, a_object)) {
				const auto it = std::ranges::find_if(a_targetBefore, [x](const auto& a_entry) { return a_entry.first == x; });
				const auto extra = x->GetCount() - (it != a_targetBefore.end() ? it->second : 0);
				if (extra > 0) {
					a_target->RemoveItem(a_object, extra, reason, x, a_source);
					back += extra;
				}
			}
			return back;
		}

		std::string EndpointName(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				return "<none>";
			}
			return a_ref->GetFormID() == kPlayerRefID ? "<player>" : Describe(a_ref);
		}
	}

	void ItemMover::BeginSuppress() noexcept
	{
		++g_suppressDepth;
	}

	void ItemMover::EndSuppress() noexcept
	{
		g_suppressUntil = (Clock::now() + kSuppressGrace).time_since_epoch().count();
		--g_suppressDepth;
	}

	bool ItemMover::IsSuppressing() noexcept
	{
		return g_suppressDepth.load() > 0 || Clock::now().time_since_epoch().count() < g_suppressUntil.load();
	}

	bool ItemMover::NoteReturned(RE::FormID a_object, RE::FormID a_container)
	{
		const auto       now = Clock::now();
		std::scoped_lock lock(g_returnLock);
		const auto       it = std::ranges::find_if(g_stored, [&](const Stored& s) {
			return s.object == a_object && s.container == a_container && now - s.when <= kReturnWindow;
		});
		if (it == g_stored.end()) {
			return false;
		}
		g_stored.erase(it);
		return g_returns.insert(a_object).second;
	}

	bool ItemMover::Returns(RE::FormID a_object)
	{
		std::scoped_lock lock(g_returnLock);
		return g_returns.contains(a_object);
	}

	void ItemMover::NoteTaken(RE::FormID a_object, RE::FormID a_from)
	{
		NoteMove(a_object, a_from, kPlayerRefID);
	}

	MoveReport ItemMover::Execute(std::span<const core::Move> a_moves, const StackTable& a_stacks, const EndpointResolver& a_resolve)
	{
		MoveReport report;
		if (a_moves.empty()) {
			return report;
		}

		BeginSuppress();
		struct Guard
		{
			~Guard() { ItemMover::EndSuppress(); }
		} guard;

		// Extra-data stacks first: a plain-remainder move (RemoveItem with no extra list) lets the engine
		// pick which copies leave, so it must only run once no special copy of that object is left behind
		// by an earlier move in this batch (see the guard below).
		std::vector<const core::Move*> ordered;
		ordered.reserve(a_moves.size());
		for (const auto& move : a_moves) {
			ordered.push_back(&move);
		}
		std::ranges::stable_partition(ordered, [&](const core::Move* a_move) {
			const auto* s = a_stacks.Find(a_move->stackId);
			return s && s->extra != nullptr;
		});

		for (const auto* movePtr : ordered) {
			const auto& move = *movePtr;
			const auto* stack = a_stacks.Find(move.stackId);
			if (!stack || !stack->object || move.count <= 0) {
				logger::warn("Move skipped: unknown stack {} ({})", move.stackId, move.form.ToString());
				++report.skipped;
				continue;
			}

			auto* source = a_resolve(move.from);
			auto* target = a_resolve(move.to);
			if (!source || !target || source == target) {
				logger::warn("Move skipped: endpoint unavailable ({} -> {}) for {}", move.from.ToString(), move.to.ToString(), Describe(stack->object));
				++report.skipped;
				continue;
			}
			if (source->GetFormID() != stack->owner) {
				logger::error("Move skipped: stack {} belongs to {:08X}, not to source {}", move.stackId, stack->owner, EndpointName(source));
				++report.skipped;
				continue;
			}

			// Re-validate against the live inventory. Never move what we cannot positively find.
			const auto live = FindLiveStack(source, stack->object, stack->extra);
			if (!live.found || live.count < move.count) {
				logger::warn("Move skipped: {} x{} no longer in {} (live count {})", Describe(stack->object), move.count, EndpointName(source), live.count);
				++report.skipped;
				continue;
			}
			if (live.isProtected) {
				logger::info("Move skipped: {} is worn, favorited or a quest item", Describe(stack->object));
				++report.skipped;
				continue;
			}

			// Plain-remainder move (RemoveItem with no extra list) while extra-data copies of the object stay in
			// the source (worn, kept, tempered...; the ones that were leaving moved earlier in this batch). The
			// engine takes plain copies first, as the container menu does: checked after the move.
			ListCounts specialBefore;
			ListCounts targetSpecialBefore;
			if (!stack->extra) {
				if (const auto special = SpecialStacks(source, stack->object); !special.empty()) {
					if (PlainUnsafe(stack->object->GetFormID())) {
						logger::info("Move skipped: {} x{} left in {} because a tempered, enchanted, renamed or worn copy of it stays there",
							Describe(stack->object), move.count, EndpointName(source));
						++report.skipped;
						continue;
					}
					specialBefore = Counts(special);
					targetSpecialBefore = Counts(SpecialStacks(target, stack->object));
				}
			}

			const auto sourceBefore = LiveCount(source, stack->object);
			const auto targetBefore = LiveCount(target, stack->object);

			const auto reason = target->GetFormID() == kPlayerRefID ? RE::ITEM_REMOVE_REASON::kRemove : RE::ITEM_REMOVE_REASON::kStoreInContainer;
			source->RemoveItem(stack->object, move.count, reason, stack->extra, target);

			std::int32_t putBack = 0;
			if (!specialBefore.empty() && !SpecialCopiesStayed(source, stack->object, specialBefore)) {
				{
					std::scoped_lock lock(g_plainLock);
					g_plainUnsafe.insert(stack->object->GetFormID());
				}
				putBack = PutBack(source, target, stack->object, targetSpecialBefore);
				const auto text = std::format("Move of plain {} x{} {} -> {} took a tempered, enchanted, renamed or worn copy; put {} back. "
											  "Plain copies of it next to such a copy stay where they are for the rest of this session.",
					Describe(stack->object), move.count, EndpointName(source), EndpointName(target), putBack);
				if (SpecialCopiesStayed(source, stack->object, specialBefore)) {
					logger::warn("{}", text);  // every special copy is back where it was
				} else {
					++report.errors;
					logger::error("{}", text);
				}
			}

			const auto sourceAfter = LiveCount(source, stack->object);
			const auto targetAfter = LiveCount(target, stack->object);
			const auto removed = sourceBefore - sourceAfter;
			const auto added = targetAfter - targetBefore;

			if (removed != move.count - putBack || added != move.count - putBack) {
				++report.errors;
				logger::error("Move mismatch: {} x{} {} -> {}: source {} -> {}, target {} -> {}",
					Describe(stack->object), move.count, EndpointName(source), EndpointName(target), sourceBefore, sourceAfter, targetBefore, targetAfter);
			}
			if (added > 0) {
				NoteMove(stack->object->GetFormID(), source->GetFormID(), target->GetFormID());
				report.items += added;
				++report.stacks;
				report.targets.insert(target->GetFormID());
				report.sources.insert(source->GetFormID());
			} else {
				++report.skipped;
			}
			logger::debug("Moved {} x{} ({}) {} -> {}", Describe(stack->object), move.count,
				stack->extra ? "extra stack" : specialBefore.empty() ? "plain" : "plain, beside a special copy",
				EndpointName(source), EndpointName(target));
		}

		logger::info("ItemMover: moved {} items in {} stacks from {} to {} containers ({} skipped, {} errors)",
			report.items, report.stacks, report.sources.size(), report.targets.size(), report.skipped, report.errors);
		return report;
	}
}
