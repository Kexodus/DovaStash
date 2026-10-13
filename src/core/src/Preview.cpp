#include "dova/core/Preview.h"

#include <algorithm>
#include <set>

namespace dova::core
{
	const PreviewContainer* PreviewResult::Find(const FormRef& a_ref) const
	{
		const auto it = std::ranges::find_if(byContainer, [&](const PreviewContainer& c) { return c.ref == a_ref; });
		return it == byContainer.end() ? nullptr : &*it;
	}

	PreviewResult PreviewWhereItemsGo(const Layout& a_layout, const CategorySet& a_categories, std::span<const ItemInfo> a_items, std::span<const DisplaySpot> a_spots)
	{
		PreviewResult out;
		if (a_layout.master) {
			out.byContainer.push_back({ *a_layout.master, true, {}, 0, {} });
		}
		for (const auto& c : a_layout.containers) {
			if (!c.ignored && !out.Find(c.ref)) {
				out.byContainer.push_back({ c.ref, false, {}, 0, {} });
			}
		}

		// Displays take one copy each of what they are given (Deposit fills them first).
		std::vector<std::int32_t> left(a_items.size(), 0);
		for (std::size_t i = 0; i < a_items.size(); ++i) {
			left[i] = std::max(a_items[i].count, 0);
		}
		if (!a_spots.empty()) {
			std::vector<DisplayStock> stocks;
			std::vector<std::size_t>  stockItem;
			for (std::size_t i = 0; i < a_items.size(); ++i) {
				if (!SortPlanner::CanMove(a_items[i])) {
					continue;
				}
				DisplayStock s;
				s.from = ContainerRef::Player();
				s.item = a_items[i];
				s.available = a_items[i].count;
				s.categories = a_categories.MatchAll(a_items[i]);
				stocks.push_back(std::move(s));
				stockItem.push_back(i);
			}
			for (const auto& a : PlanDisplays(a_spots, stocks, {})) {
				const auto item = stockItem[a.stock];
				out.onDisplays.push_back({ a_spots[a.spot].ref, item });
				left[item] -= a.count;
			}
		}

		const SortPlanner planner(a_layout, a_categories);
		for (std::size_t i = 0; i < a_items.size(); ++i) {
			const auto& item = a_items[i];
			if (!SortPlanner::CanMove(item)) {
				out.neverMoved.push_back(i);
				continue;
			}
			if (left[i] <= 0) {
				continue;  // all on displays
			}
			const auto dest = planner.ResolveDestination(item);
			if (!dest) {
				out.noDestination.push_back(i);
				continue;
			}
			auto it = std::ranges::find_if(out.byContainer, [&](const PreviewContainer& c) { return c.ref == dest->container; });
			if (it == out.byContainer.end()) {
				out.byContainer.push_back({ dest->container, a_layout.IsMaster(dest->container), {}, 0, {} });
				it = std::prev(out.byContainer.end());
			}
			PreviewEntry e;
			e.item = i;
			e.count = left[i];
			e.reason = dest->reason;
			e.category = dest->reason == Destination::Reason::Category ? dest->category : a_categories.Classify(item).value_or(std::string{});
			it->total += e.count;
			it->perCategory[e.category] += e.count;
			if (dest->reason == Destination::Reason::Master) {
				out.toMaster += e.count;
			} else {
				out.toContainers += e.count;
			}
			it->items.push_back(std::move(e));
		}
		return out;
	}
}
