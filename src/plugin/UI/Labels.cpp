#include "UI/Labels.h"

#include "Game/FormUtil.h"
#include "Home/Store.h"

namespace dova::ui
{
	namespace
	{
		std::string Capitalize(std::string a_text)
		{
			if (!a_text.empty() && a_text[0] >= 'a' && a_text[0] <= 'z') {
				a_text[0] = static_cast<char>(a_text[0] - 'a' + 'A');
			}
			return a_text;
		}
	}

	std::string RootOf(std::string_view a_id)
	{
		const auto dot = a_id.find('.');
		return std::string(a_id.substr(0, dot));
	}

	std::string CategoryName(std::string_view a_id)
	{
		const auto* cat = home::Store::Get().Categories().Find(a_id);
		return cat && !cat->name.empty() ? cat->name : Capitalize(std::string(a_id));
	}

	std::string PatternLabel(std::string_view a_pattern)
	{
		if (a_pattern == "*") {
			return "Everything";
		}
		if (a_pattern.ends_with(".*")) {
			return "All " + CategoryName(a_pattern.substr(0, a_pattern.size() - 2));
		}
		return CategoryName(a_pattern);
	}

	std::string PatternShortLabel(std::string_view a_pattern)
	{
		if (a_pattern == "*") {
			return "Everything";
		}
		if (a_pattern.ends_with(".*")) {
			a_pattern.remove_suffix(2);
		}
		return CategoryName(a_pattern);
	}

	std::vector<std::string> RootCategories()
	{
		std::vector<std::string> roots;
		for (const auto& c : home::Store::Get().Categories().Categories()) {
			auto root = RootOf(c.id);
			if (std::ranges::find(roots, root) == roots.end()) {
				roots.push_back(std::move(root));
			}
		}
		return roots;
	}

	std::vector<std::string> ChildCategories(const std::string& a_root)
	{
		std::vector<std::string> out;
		for (const auto& c : home::Store::Get().Categories().Categories()) {
			if (c.id != a_root && RootOf(c.id) == a_root) {
				out.push_back(c.id);
			}
		}
		return out;
	}

	std::int32_t CountMatching(const std::map<std::string, std::int32_t>& a_counts, std::string_view a_pattern)
	{
		std::int32_t n = 0;
		for (const auto& [id, count] : a_counts) {
			if (!id.empty() && core::CategoryPatternMatches(a_pattern, id)) {
				n += count;
			}
		}
		return n;
	}

	bool HasPattern(const core::ContainerBinding* a_binding, std::string_view a_pattern)
	{
		return a_binding && std::ranges::any_of(a_binding->categories, [&](const std::string& p) { return core::IEquals(p, a_pattern); });
	}

	bool Covers(const core::ContainerBinding* a_binding, std::string_view a_id)
	{
		return a_binding && std::ranges::any_of(a_binding->categories, [&](const std::string& p) { return core::CategoryPatternMatches(p, a_id); });
	}

	ContainerRole DescribeContainer(RE::TESObjectREFR* a_ref)
	{
		ContainerRole role;
		auto*         home = home::HomeRegistry::Get().Peek();
		if (!a_ref || !home || !home->layout || a_ref->GetParentCell() == nullptr || a_ref->GetParentCell()->GetFormID() != home->cellId) {
			return role;
		}
		const auto ref = game::ToFormRef(a_ref);
		if (!ref) {
			return role;
		}
		const auto& layout = *home->layout;
		if (layout.IsMaster(*ref)) {
			role.kind = ContainerRole::Kind::Master;
			return role;
		}
		for (const auto& [item, container] : layout.learned) {
			if (container == *ref) {
				++role.learned;
			}
		}
		role.binding = layout.FindContainer(*ref);
		if (!role.binding) {
			role.kind = ContainerRole::Kind::Unmanaged;
		} else if (role.binding->ignored) {
			role.kind = ContainerRole::Kind::Ignored;
		} else {
			role.kind = ContainerRole::Kind::Bound;
		}
		return role;
	}

	std::string RoleSummary(const ContainerRole& a_role, std::size_t a_maxPatterns)
	{
		switch (a_role.kind) {
		case ContainerRole::Kind::Master:
			return "Master";
		case ContainerRole::Kind::Bound:
			{
				const auto& patterns = a_role.binding->categories;
				std::string out;
				const auto  shown = std::min(patterns.size(), a_maxPatterns);
				for (std::size_t i = 0; i < shown; ++i) {
					out += (i == 0 ? "" : ", ") + PatternShortLabel(patterns[i]);
				}
				if (patterns.size() > shown) {
					out += std::format(" +{}", patterns.size() - shown);
				}
				if (out.empty() && a_role.learned > 0) {
					out = std::format("{} learned item{}", a_role.learned, a_role.learned == 1 ? "" : "s");
				}
				return out;
			}
		default:
			return {};
		}
	}
}
