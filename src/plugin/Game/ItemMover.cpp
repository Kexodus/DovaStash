#include "Game/ItemMover.h"

#include "Game/FormUtil.h"

namespace dova::game
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr auto kSuppressGrace = std::chrono::milliseconds(750);

		std::atomic<int>                g_suppressDepth{ 0 };
		std::atomic<Clock::rep>         g_suppressUntil{ 0 };

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

		for (const auto& move : a_moves) {
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

			// For a plain-remainder move, remember the protected stacks so we can detect if the engine
			// ever took from one of them instead.
			std::vector<RE::ExtraDataList*> protectedBefore;
			if (!stack->extra) {
				protectedBefore = ProtectedExtraLists(source, stack->object);
				// RemoveItem with a null extra list lets the engine pick which copies leave; it could take
				// the worn / favorited / quest copy. Until that is verified in game, never move the plain
				// remainder out of the player's inventory while a protected copy of the same object exists.
				if (!protectedBefore.empty() && source->GetFormID() == kPlayerRefID) {
					logger::info("Move skipped: {} x{} left in the player inventory because another copy is worn, favorited or a quest item",
						Describe(stack->object), move.count);
					++report.skipped;
					continue;
				}
			}

			const auto sourceBefore = LiveCount(source, stack->object);
			const auto targetBefore = LiveCount(target, stack->object);

			const auto reason = target->GetFormID() == kPlayerRefID ? RE::ITEM_REMOVE_REASON::kRemove : RE::ITEM_REMOVE_REASON::kStoreInContainer;
			source->RemoveItem(stack->object, move.count, reason, stack->extra, target);

			const auto sourceAfter = LiveCount(source, stack->object);
			const auto targetAfter = LiveCount(target, stack->object);
			const auto removed = sourceBefore - sourceAfter;
			const auto added = targetAfter - targetBefore;

			if (removed != move.count || added != move.count) {
				++report.errors;
				logger::error("Move mismatch: {} x{} {} -> {}: source {} -> {}, target {} -> {}",
					Describe(stack->object), move.count, EndpointName(source), EndpointName(target), sourceBefore, sourceAfter, targetBefore, targetAfter);
			}
			if (!protectedBefore.empty()) {
				const auto protectedAfter = ProtectedExtraLists(source, stack->object);
				for (auto* xList : protectedBefore) {
					if (std::ranges::find(protectedAfter, xList) == protectedAfter.end()) {
						++report.errors;
						logger::error("Move of plain {} x{} disturbed a worn/favorited/quest stack in {}", Describe(stack->object), move.count, EndpointName(source));
						break;
					}
				}
			}

			if (added > 0) {
				report.items += added;
				++report.stacks;
				report.targets.insert(target->GetFormID());
				report.sources.insert(source->GetFormID());
			} else {
				++report.skipped;
			}
			logger::debug("Moved {} x{} ({}) {} -> {}", Describe(stack->object), move.count, stack->extra ? "extra stack" : "plain",
				EndpointName(source), EndpointName(target));
		}

		logger::info("ItemMover: moved {} items in {} stacks from {} to {} containers ({} skipped, {} errors)",
			report.items, report.stacks, report.sources.size(), report.targets.size(), report.skipped, report.errors);
		return report;
	}
}
