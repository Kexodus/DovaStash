#include "dova/core/KeepRules.h"

#include <algorithm>
#include <format>
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

	namespace
	{
		std::vector<KeepSelector> CompileSelectors(const std::vector<std::string>& a_list, const std::string& a_where, Diagnostics& a_diags)
		{
			std::vector<KeepSelector> out;
			for (const auto& e : a_list) {
				auto sel = KeepSelector::Parse(e);
				if (sel.kind == KeepSelector::Kind::Invalid) {
					a_diags.Warn(a_where, "ignoring keep entry \"" + e + "\" (not a token, form reference or category pattern)");
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
		std::vector<std::int32_t> keep(n, 0);
		std::vector<std::string>  why(n);

		std::vector<ItemView> views(n);
		for (std::size_t i = 0; i < n; ++i) {
			views[i].item = &a_inventory[i];
			views[i].primary = a_categories.Classify(a_inventory[i]);
			for (const auto& k : a_inventory[i].keywords) {
				views[i].keywords.push_back(ToLower(k));
			}
		}

		std::vector<bool>                                                   keptAll(n, false);
		std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> claims;  // (layer, quota) -> stacks

		for (std::size_t i = 0; i < n; ++i) {
			const auto& item = a_inventory[i];
			if (item.count <= 0) {
				continue;
			}
			if (item.questItem || !item.playable || item.returns) {
				keptAll[i] = true;
				why[i] = item.questItem ? "quest item" : item.playable ? "comes back when stored" : "non-playable";
				continue;
			}
			bool decided = false;
			for (std::size_t l = 0; l < _layers.size() && !decided; ++l) {
				const auto& layer = _layers[l];
				if (const auto it = std::ranges::find_if(layer.always, [&](const KeepSelector& s) { return SelectorMatches(s, views[i]); });
					it != layer.always.end()) {
					keptAll[i] = true;
					decided = true;
					why[i] = std::format("always {} ({})", SelectorLabel(*it), layer.name);
				} else if (std::ranges::any_of(layer.never, [&](const KeepSelector& s) { return SelectorMatches(s, views[i]); })) {
					decided = true;
				} else {
					for (std::size_t q = 0; q < layer.quotas.size(); ++q) {
						if (QuotaMatches(layer.quotas[q], views[i])) {
							claims[{ l, q }].push_back(i);
							decided = true;
							break;
						}
					}
				}
			}
		}

		for (std::size_t i = 0; i < n; ++i) {
			if (keptAll[i]) {
				keep[i] = a_inventory[i].count;
			}
		}

		for (auto& [key, stacks] : claims) {
			const auto& quota = _layers[key.first].quotas[key.second];
			std::int64_t remaining = quota.quota;
			for (std::size_t i = 0; i < n; ++i) {
				if (keptAll[i] && QuotaMatches(quota, views[i])) {
					remaining -= a_inventory[i].count;
				}
			}
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
				why[i] = std::format("{} ({})", QuotaLabel(quota), _layers[key.first].name);
			}
		}
		if (a_why) {
			*a_why = std::move(why);
		}
		return keep;
	}
}
