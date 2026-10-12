#include "Icons/IconService.h"

#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Home/Store.h"
#include "Sort/SortEngine.h"

#include <fstream>
#include <sstream>

namespace dova::icons
{
	namespace
	{
		constexpr auto kSkyUIConfig = "Interface/skyui/config.txt"sv;

		struct State
		{
			std::string     itemSource{ core::kSkyUIItemIcons };
			std::string     categorySource{ core::kSkyUICategoryIcons };
		};

		State& Get()
		{
			static State state;
			return state;
		}

		// A loose file wins over archives, even when empty (that is how patches blank out a rule file).
		std::optional<std::string> ReadResource(const std::string& a_relative)
		{
			std::error_code ec;
			const auto      loose = std::filesystem::path("Data") / a_relative;
			if (std::filesystem::is_regular_file(loose, ec)) {
				std::ifstream in(loose, std::ios::binary);
				if (!in) {
					return std::nullopt;
				}
				std::ostringstream ss;
				ss << in.rdbuf();
				return std::move(ss).str();
			}

			RE::BSResourceNiBinaryStream stream{ a_relative };
			if (!stream.good() || !stream.stream) {
				return std::nullopt;
			}
			const std::uint32_t size = stream.stream->totalSize;
			std::string         text(size, '\0');
			if (size > 0 && !stream.read(text.data(), size)) {
				return std::nullopt;
			}
			return text;
		}

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t' || a_text.front() == '\r')) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		// SkyUI config.txt: "[Section]" headers, "key = value ; comment", strings in single quotes.
		struct SkyUIAppearance
		{
			std::optional<std::string> itemSource;
			std::optional<std::string> categorySource;
			std::optional<bool>        noColor;
		};

		SkyUIAppearance ReadSkyUIAppearance()
		{
			SkyUIAppearance out;
			const auto      text = ReadResource(std::string(kSkyUIConfig));
			if (!text) {
				return out;
			}
			std::string_view section;
			std::size_t      pos = 0;
			while (pos < text->size()) {
				auto end = text->find('\n', pos);
				if (end == std::string::npos) {
					end = text->size();
				}
				auto line = Trim(std::string_view(*text).substr(pos, end - pos));
				pos = end + 1;
				if (line.empty() || line.front() == ';') {
					continue;
				}
				if (line.front() == '[') {
					const auto close = line.find(']');
					section = close == std::string_view::npos ? line.substr(1) : line.substr(1, close - 1);
					continue;
				}
				if (!core::IEquals(section, "Appearance")) {
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string_view::npos) {
					continue;
				}
				const auto key = Trim(line.substr(0, eq));
				auto       value = Trim(line.substr(eq + 1));
				if (!value.empty() && (value.front() == '\'' || value.front() == '"')) {
					const auto quote = value.front();
					const auto close = value.find(quote, 1);
					value = close == std::string_view::npos ? value.substr(1) : value.substr(1, close - 1);
				} else if (const auto comment = value.find(';'); comment != std::string_view::npos) {
					value = Trim(value.substr(0, comment));
				}
				if (core::IEquals(key, "icons.item.source")) {
					out.itemSource = std::string(value);
				} else if (core::IEquals(key, "icons.category.source")) {
					out.categorySource = std::string(value);
				} else if (core::IEquals(key, "icons.item.noColor")) {
					out.noColor = core::IEquals(value, "true");
				}
			}
			return out;
		}

		std::string Normalize(std::string a_source)
		{
			std::ranges::replace(a_source, '\\', '/');
			return a_source;
		}

		// Data-file icons name SkyUI's stock SWFs; use the ones the player's SkyUI is configured with.
		core::IconRef WithPlayerSources(core::IconRef a_icon)
		{
			const auto& state = Get();
			if (core::IEquals(a_icon.source, core::kSkyUIItemIcons)) {
				a_icon.source = state.itemSource;
			} else if (core::IEquals(a_icon.source, core::kSkyUICategoryIcons)) {
				a_icon.source = state.categorySource;
			}
			return a_icon;
		}

		// Binding pattern -> category id whose icon stands for it ("a.b.*" -> "a.b", "*" -> "").
		std::string_view PatternCategory(std::string_view a_pattern)
		{
			if (a_pattern == "*") {
				return {};
			}
			if (a_pattern.ends_with(".*")) {
				a_pattern.remove_suffix(2);
			}
			return a_pattern;
		}
	}

	void Load()
	{
		auto& state = Get();
		state = State{};

		const auto appearance = ReadSkyUIAppearance();
		if (appearance.itemSource && !appearance.itemSource->empty()) {
			state.itemSource = Normalize(*appearance.itemSource);
		}
		if (appearance.categorySource && !appearance.categorySource->empty()) {
			state.categorySource = Normalize(*appearance.categorySource);
		}
		logger::info("Icons: SkyUI item icons '{}', category icons '{}', item icon colours {}", state.itemSource,
			state.categorySource, appearance.noColor.value_or(false) ? "off" : "on");
	}

	core::IconRef ForCategory(std::string_view a_categoryId)
	{
		const auto& categories = home::Store::Get().Categories();
		auto        id = core::ToLower(PatternCategory(a_categoryId));
		while (!id.empty()) {
			if (const auto* category = categories.Find(id); category && category->icon && !category->icon->Empty()) {
				return WithPlayerSources(*category->icon);
			}
			const auto dot = id.rfind('.');
			if (dot == std::string::npos) {
				break;
			}
			id.resize(dot);
		}
		return {};
	}

	core::IconRef ForPattern(std::string_view a_pattern)
	{
		if (a_pattern == "*") {
			return WithPlayerSources(core::IconRef{ std::string(core::kSkyUICategoryIcons), "inv_all", std::nullopt });
		}
		return ForCategory(a_pattern);
	}

	IconRow RowForContainer(const core::Layout& a_layout, RE::TESObjectREFR* a_container, std::size_t a_max)
	{
		IconRow    row;
		const auto ref = a_container ? game::ToFormRef(a_container) : std::nullopt;
		if (!ref) {
			return row;
		}
		if (a_layout.IsMaster(*ref)) {
			row.icons.push_back(ForPattern("*"));
			return row;
		}
		const auto* binding = a_layout.FindContainer(*ref);
		if (!binding || binding->ignored) {
			return row;
		}
		for (const auto& pattern : binding->categories) {
			auto icon = ForPattern(pattern);
			if (icon.Empty() || std::ranges::find(row.icons, icon) != row.icons.end()) {
				continue;  // no icon, or another binding already shows it
			}
			if (row.icons.size() >= a_max) {
				++row.extra;
				continue;
			}
			row.icons.push_back(std::move(icon));
		}
		if (row.icons.empty() && row.extra == 0) {  // learned items only
			if (auto icon = ForContainer(a_layout, a_container); !icon.Empty()) {
				row.icons.push_back(std::move(icon));
			}
		}
		return row;
	}

	core::IconRef ForContainer(const core::Layout& a_layout, RE::TESObjectREFR* a_container)
	{
		// Stable by design: the icon says what a container is *for*, so it comes from the layout (its
		// category bindings, or what it has learned), never from whatever happens to be inside it.
		const auto ref = a_container ? game::ToFormRef(a_container) : std::nullopt;
		if (!ref) {
			return {};
		}
		if (a_layout.IsMaster(*ref)) {
			// The inbox for everything: SkyUI's "All" category icon.
			return WithPlayerSources(core::IconRef{ std::string(core::kSkyUICategoryIcons), "inv_all", std::nullopt });
		}
		const auto* binding = a_layout.FindContainer(*ref);
		if (!binding || binding->ignored) {
			return {};
		}
		// 1. The first bound pattern (the binding's main purpose).
		for (const auto& pattern : binding->categories) {
			if (pattern == "*") {
				return WithPlayerSources(core::IconRef{ std::string(core::kSkyUICategoryIcons), "inv_all", std::nullopt });
			}
			if (auto icon = ForCategory(pattern); !icon.Empty()) {
				return icon;
			}
		}
		// 2. Learned-only containers: the category most of their learned items belong to.
		const auto votes = sort::LearnedByCategory(a_layout, *ref);
		const auto best = std::ranges::max_element(votes, {}, [](const auto& a_vote) { return a_vote.second; });
		if (best != votes.end()) {
			return ForCategory(best->first);
		}
		return {};
	}
}
