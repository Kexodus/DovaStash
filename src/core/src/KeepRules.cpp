#include "dova/core/KeepRules.h"

#include <algorithm>
#include <format>
#include <limits>
#include <map>
#include <utility>

namespace dova::core
{
	KeepConfig DefaultKeepConfig()
	{
		KeepConfig cfg;
		cfg.always = { "equipped", "favorited", "quest", kGoldForm.ToString(), kLockpickForm.ToString(), kTorchForm.ToString() };
		return cfg;
	}

	bool RestockApplies(const KeepConfig& a_global, const KeepConfig* a_layoutKeep)
	{
		const auto any = [](const KeepConfig& a_keep) { return std::ranges::any_of(a_keep.quotas, [](const QuotaRule& a_q) { return a_q.restock; }); };
		if (a_layoutKeep && any(*a_layoutKeep)) {
			return true;
		}
		return (!a_layoutKeep || a_layoutKeep->Inherits()) && any(a_global);
	}

	namespace
	{
		std::vector<KeepSelector> CompileSelectors(const std::vector<std::string>& a_list, const std::string& a_where, Diagnostics& a_diags)
		{
			std::vector<KeepSelector> out;
			for (const auto& e : a_list) {
				auto sel = KeepSelector::Parse(e);
				if (sel.kind == KeepSelector::Kind::Invalid) {
					const auto err = e.find('|') != std::string::npos ? FormRefError(e) : std::string();  // a form reference was meant
					a_diags.Warn(a_where, "ignoring keep entry \"" + e + "\" (not a token, form reference or category pattern)" + (err.empty() ? err : ": " + err));
					continue;
				}
				out.push_back(std::move(sel));
			}
			return out;
		}

		KeepRules::Layer CompileLayer(const KeepConfig& a_cfg, const std::string& a_where, Diagnostics& a_diags)
		{
			KeepRules::Layer layer;
			layer.always = CompileSelectors(a_cfg.always, a_where, a_diags);
			layer.never = CompileSelectors(a_cfg.never, a_where, a_diags);
			for (const auto& q : a_cfg.quotas) {
				KeepRules::Quota quota;
				quota.category = q.category;
				if (q.keywordsAny) {
					std::vector<std::string> kws;
					for (const auto& k : *q.keywordsAny) {
						kws.push_back(ToLower(k));
					}
					quota.keywordsAny = std::move(kws);
				}
				quota.forms = q.forms;
				quota.quota = std::max(0, q.quota);
				quota.restock = q.restock;
				layer.quotas.push_back(std::move(quota));
			}
			return layer;
		}

		struct ItemView
		{
			const ItemInfo*            item{ nullptr };
			std::optional<std::string> primary;
			std::vector<std::string>   keywords;  // lower-cased
		};

		bool PatternMatchesItem(std::string_view a_pattern, const ItemView& a_view)
		{
			if (a_view.primary) {
				return CategoryPatternMatches(a_pattern, *a_view.primary);
			}
			return a_pattern == "*";
		}

		bool SelectorMatches(const KeepSelector& a_sel, const ItemView& a_view)
		{
			const auto& item = *a_view.item;
			switch (a_sel.kind) {
			case KeepSelector::Kind::Equipped:
				return item.equipped;
			case KeepSelector::Kind::Favorited:
				return item.favorited;
			case KeepSelector::Kind::Quest:
				return item.questItem;
			case KeepSelector::Kind::Form:
				return item.form == a_sel.form;
			case KeepSelector::Kind::Category:
				return PatternMatchesItem(a_sel.pattern, a_view);
			case KeepSelector::Kind::Invalid:
			default:
				return false;
			}
		}

		std::string SelectorLabel(const KeepSelector& a_sel)
		{
			switch (a_sel.kind) {
			case KeepSelector::Kind::Equipped:
				return "equipped";
			case KeepSelector::Kind::Favorited:
				return "favorited";
			case KeepSelector::Kind::Quest:
				return "quest";
			case KeepSelector::Kind::Form:
				return a_sel.form.ToString();
			case KeepSelector::Kind::Category:
				return a_sel.pattern;
			case KeepSelector::Kind::Invalid:
			default:
				return "?";
			}
		}

		std::string QuotaLabel(const KeepRules::Quota& a_q)
		{
			std::string out = "quota";
			if (a_q.category) {
				out += " " + *a_q.category;
			}
			if (a_q.keywordsAny) {
				for (const auto& k : *a_q.keywordsAny) {
					out += " " + k;
				}
			}
			if (a_q.forms) {
				for (const auto& f : *a_q.forms) {
					out += " " + f.ToString();
				}
			}
			return out + " " + std::to_string(a_q.quota);
		}

		bool QuotaMatches(const KeepRules::Quota& a_q, const ItemView& a_view)
		{
			if (a_q.category && !PatternMatchesItem(*a_q.category, a_view)) {
				return false;
			}
			if (a_q.keywordsAny && std::ranges::none_of(*a_q.keywordsAny, [&](const std::string& k) { return std::ranges::find(a_view.keywords, k) != a_view.keywords.end(); })) {
				return false;
			}
			if (a_q.forms && std::ranges::find(*a_q.forms, a_view.item->form) == a_q.forms->end()) {
				return false;
			}
			return true;
		}

		ItemView MakeView(const ItemInfo& a_item, const CategorySet& a_categories)
		{
			ItemView view;
			view.item = &a_item;
			view.primary = a_categories.Classify(a_item);
			for (const auto& k : a_item.keywords) {
				view.keywords.push_back(ToLower(k));
			}
			return view;
		}

		// How Deposit sees an inventory: the views, the stacks kept whole (hard rule or `always`) with the rule
		// behind each, and the stacks each quota rule claims.
		struct Evaluation
		{
			std::vector<ItemView>                                                  views;
			std::vector<bool>                                                      keptAll;
			std::vector<std::string>                                               why;
			std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> claims;  // (layer, quota) -> stacks
		};

		Evaluation Evaluate(std::span<const KeepRules::Layer> a_layers, std::span<const ItemInfo> a_inventory, const CategorySet& a_categories)
		{
			const auto n = a_inventory.size();
			Evaluation ev;
			ev.views.reserve(n);
			for (const auto& item : a_inventory) {
				ev.views.push_back(MakeView(item, a_categories));
			}
			ev.keptAll.assign(n, false);
			ev.why.assign(n, {});

			for (std::size_t i = 0; i < n; ++i) {
				const auto& item = a_inventory[i];
				if (item.count <= 0) {
					continue;
				}
				if (item.questItem || !item.playable || item.returns) {
					ev.keptAll[i] = true;
					ev.why[i] = item.questItem ? "quest item" : item.playable ? "comes back when stored" : "non-playable";
					continue;
				}
				// Worn and favorited stacks are hard rules too (DESIGN §8.3): a layout keep without them still keeps them.
				if (item.equipped || item.favorited) {
					ev.keptAll[i] = true;
					ev.why[i] = item.equipped ? "worn item" : "favorited item";
					continue;
				}
				bool decided = false;
				for (std::size_t l = 0; l < a_layers.size() && !decided; ++l) {
					const auto& layer = a_layers[l];
					if (const auto it = std::ranges::find_if(layer.always, [&](const KeepSelector& s) { return SelectorMatches(s, ev.views[i]); });
						it != layer.always.end()) {
						ev.keptAll[i] = true;
						decided = true;
						ev.why[i] = std::format("always {} ({})", SelectorLabel(*it), layer.name);
					} else if (std::ranges::any_of(layer.never, [&](const KeepSelector& s) { return SelectorMatches(s, ev.views[i]); })) {
						decided = true;
					} else {
						for (std::size_t q = 0; q < layer.quotas.size(); ++q) {
							if (QuotaMatches(layer.quotas[q], ev.views[i])) {
								ev.claims[{ l, q }].push_back(i);
								decided = true;
								break;
							}
						}
					}
				}
			}
			return ev;
		}

		// Copies the player carries of a quota rule, as Deposit counts them against it: stacks kept whole that
		// match its selectors, plus the stacks it claims.
		std::int64_t Carried(const Evaluation& a_ev, std::span<const ItemInfo> a_inventory, const KeepRules::Quota& a_quota,
			const std::vector<std::size_t>* a_claimed)
		{
			std::int64_t have = 0;
			for (std::size_t i = 0; i < a_inventory.size(); ++i) {
				if (a_ev.keptAll[i] && a_inventory[i].count > 0 && QuotaMatches(a_quota, a_ev.views[i])) {
					have += a_inventory[i].count;
				}
			}
			if (a_claimed) {
				for (const auto i : *a_claimed) {
					have += a_inventory[i].count;
				}
			}
			return have;
		}
	}

	KeepRules KeepRules::Compile(const KeepConfig& a_global, const KeepConfig* a_layoutKeep, Diagnostics& a_diags)
	{
		KeepRules rules;
		if (a_layoutKeep) {
			rules._layers.push_back(CompileLayer(*a_layoutKeep, "keep (layout)", a_diags));
			rules._layers.back().name = "layout";
		}
		if (!a_layoutKeep || a_layoutKeep->Inherits()) {
			rules._layers.push_back(CompileLayer(a_global, "keep (global)", a_diags));
			rules._layers.back().name = "global";
		}
		return rules;
	}

	bool KeepRules::AlwaysKeeps(const ItemInfo& a_item, const CategorySet& a_categories, std::string* a_why) const
	{
		auto why = [&](std::string a_text) {
			if (a_why) {
				*a_why = std::move(a_text);
			}
			return true;
		};
		if (a_item.questItem) {
			return why("quest item");
		}
		if (!a_item.playable) {
			return why("non-playable");
		}
		if (a_item.returns) {
			return why("comes back when stored");
		}
		if (a_item.equipped) {
			return why("worn item");
		}
		if (a_item.favorited) {
			return why("favorited item");
		}
		ItemView view;
		view.item = &a_item;
		view.primary = a_categories.Classify(a_item);
		for (const auto& layer : _layers) {
			if (const auto it = std::ranges::find_if(layer.always, [&](const KeepSelector& s) { return SelectorMatches(s, view); }); it != layer.always.end()) {
				return why(std::format("always {} ({})", SelectorLabel(*it), layer.name));
			}
			if (std::ranges::any_of(layer.never, [&](const KeepSelector& s) { return SelectorMatches(s, view); })) {
				return false;
			}
		}
		return false;
	}

	std::vector<std::int32_t> KeepRules::ComputeKeepCounts(std::span<const ItemInfo> a_inventory, const CategorySet& a_categories,
		std::vector<std::string>* a_why) const
	{
		const auto n = a_inventory.size();
		auto       ev = Evaluate(_layers, a_inventory, a_categories);
		std::vector<std::int32_t> keep(n, 0);

		for (std::size_t i = 0; i < n; ++i) {
			if (ev.keptAll[i]) {
				keep[i] = a_inventory[i].count;
			}
		}

		for (auto& [key, stacks] : ev.claims) {
			const auto& quota = _layers[key.first].quotas[key.second];
			std::int64_t remaining = quota.quota - Carried(ev, a_inventory, quota, nullptr);
			std::ranges::stable_sort(stacks, [&](std::size_t a_lhs, std::size_t a_rhs) {
				const auto& l = a_inventory[a_lhs];
				const auto& r = a_inventory[a_rhs];
				if (l.value != r.value) {
					return l.value > r.value;
				}
				if (l.count != r.count) {
					return l.count > r.count;
				}
				if (l.form != r.form) {
					return l.form < r.form;
				}
				return l.stackId < r.stackId;
			});
			for (const auto i : stacks) {
				if (remaining <= 0) {
					break;
				}
				const auto k = static_cast<std::int32_t>(std::min<std::int64_t>(remaining, a_inventory[i].count));
				keep[i] = k;
				remaining -= k;
				ev.why[i] = std::format("{} ({})", QuotaLabel(quota), _layers[key.first].name);
			}
		}
		if (a_why) {
			*a_why = std::move(ev.why);
		}
		return keep;
	}

	bool KeepRules::Restocks() const noexcept
	{
		return std::ranges::any_of(_layers, [](const Layer& a_layer) {
			return std::ranges::any_of(a_layer.quotas, [](const Quota& a_q) { return a_q.restock; });
		});
	}

	std::vector<KeepRules::RestockGoal> KeepRules::RestockGoals(std::span<const ItemInfo> a_inventory, const CategorySet& a_categories) const
	{
		std::vector<RestockGoal> goals;
		if (!Restocks()) {
			return goals;
		}
		const auto ev = Evaluate(_layers, a_inventory, a_categories);
		for (std::size_t l = 0; l < _layers.size(); ++l) {
			for (std::size_t q = 0; q < _layers[l].quotas.size(); ++q) {
				const auto& quota = _layers[l].quotas[q];
				if (!quota.restock) {
					continue;
				}
				const auto it = ev.claims.find({ l, q });
				const auto have = Carried(ev, a_inventory, quota, it != ev.claims.end() ? &it->second : nullptr);
				goals.push_back(RestockGoal{
					.rule = { l, q },
					.quota = quota.quota,
					.have = static_cast<std::int32_t>(std::min<std::int64_t>(have, std::numeric_limits<std::int32_t>::max())),
					.label = std::format("{} ({})", QuotaLabel(quota), _layers[l].name) });
			}
		}
		return goals;
	}

	std::optional<KeepRules::RuleId> KeepRules::RestockRuleFor(const ItemInfo& a_item, const CategorySet& a_categories) const
	{
		if (a_item.count <= 0 || a_item.questItem || !a_item.playable || a_item.returns) {
			return std::nullopt;
		}
		const auto view = MakeView(a_item, a_categories);
		for (std::size_t l = 0; l < _layers.size(); ++l) {
			const auto& layer = _layers[l];
			const bool  always = std::ranges::any_of(layer.always, [&](const KeepSelector& s) { return SelectorMatches(s, view); });
			if (!always && std::ranges::any_of(layer.never, [&](const KeepSelector& s) { return SelectorMatches(s, view); })) {
				return std::nullopt;
			}
			for (std::size_t q = 0; q < layer.quotas.size(); ++q) {
				if (QuotaMatches(layer.quotas[q], view)) {
					if (!layer.quotas[q].restock) {
						return std::nullopt;
					}
					return RuleId{ l, q };
				}
			}
		}
		return std::nullopt;
	}
}
