#include "Icons/IconService.h"

#include "Game/FormUtil.h"
#include "Game/ItemAdapter.h"
#include "Home/Store.h"

#include <fstream>
#include <sstream>

namespace dova::icons
{
	namespace
	{
		// I4 reads "SKSE/Plugins/InventoryInjector/<plugin>.json" through the game's resource system
		// (loose files, then archives). Paths below are relative to Data/.
		constexpr auto kI4RulesDir = "SKSE/Plugins/InventoryInjector"sv;
		constexpr auto kI4Dll = "Data/SKSE/Plugins/InventoryInjector.dll"sv;
		constexpr auto kSkyUIConfig = "Interface/skyui/config.txt"sv;

		struct State
		{
			core::IconRules rules;
			std::string     itemSource{ core::kSkyUIItemIcons };
			std::string     categorySource{ core::kSkyUICategoryIcons };
			std::size_t     files{ 0 };
		};

		State& Get()
		{
			static State state;
			return state;
		}

		// Through the game's resource system: loose files (MO2's VFS included) and BSAs.
		bool ResourceExists(const std::string& a_relative)
		{
			RE::BSResourceNiBinaryStream stream{ a_relative };
			return stream.good();
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

		// "Skyrim.esm" -> "Skyrim.json" (I4: path(fileName).replace_extension("json")).
		std::string RuleFileName(std::string_view a_plugin)
		{
			const auto dot = a_plugin.rfind('.');
			return std::string(dot == std::string_view::npos ? a_plugin : a_plugin.substr(0, dot)) + ".json";
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

		bool CanRepresent(const core::ItemInfo& a_item)
		{
			return a_item.playable && !a_item.name.empty() && a_item.count > 0;
		}

		// Most common item (summed over stacks of the same base form) among `a_items` accepted by
		// `a_filter`. Ties go to the lower form reference, so the choice is stable.
		template <class Filter>
		const core::ItemInfo* MostCommon(const std::vector<core::ItemInfo>& a_items, Filter&& a_filter)
		{
			struct Tally
			{
				const core::ItemInfo* item{ nullptr };
				std::int64_t          count{ 0 };
			};
			std::map<std::string, Tally> tallies;
			for (const auto& item : a_items) {
				if (!CanRepresent(item) || !a_filter(item)) {
					continue;
				}
				// Dynamic forms have no FormRef: tell them apart by type and name.
				const auto key = item.form.IsValid() ? core::ToLower(item.form.ToString()) :
				                                       "~dynamic|" + std::to_string(static_cast<int>(item.type)) + "|" + item.name;
				auto& tally = tallies[key];
				if (!tally.item) {
					tally.item = &item;
				}
				tally.count += item.count;
			}
			const core::ItemInfo* best = nullptr;
			std::int64_t          bestCount = 0;
			for (const auto& [key, tally] : tallies) {  // key order: ties keep the first
				if (tally.count > bestCount) {
					best = tally.item;
					bestCount = tally.count;
				}
			}
			return best;
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
		state.rules.SetDefaultItemSource(state.itemSource);
		state.rules.SetNoIconColors(appearance.noColor.value_or(false));
		logger::info("Icons: SkyUI item icons '{}', category icons '{}', item icon colours {}", state.itemSource,
			state.categorySource, appearance.noColor.value_or(false) ? "off" : "on");

		std::error_code ec;
		if (!std::filesystem::exists(std::filesystem::path(kI4Dll), ec)) {
			logger::info("Icons: Inventory Interface Information Injector not installed; using SkyUI's default icons");
			return;
		}
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			logger::error("Icons: TESDataHandler unavailable; I4 rules not loaded");
			return;
		}

		// I4's iconSource check: the SWF must exist under Interface/ (loose or archived).
		state.rules.SetIconSourceValidator([](std::string_view a_source) {
			return ResourceExists("Interface/" + std::string(a_source));
		});

		core::Diagnostics diags;
		try {
			for (auto* file : dataHandler->files) {
				// I4 CustomDataManager::LoadConfigs: every checked (active) plugin, in load order.
				if (!file || !file->recordFlags.all(RE::TESFile::RecordFlag::kChecked)) {
					continue;
				}
				const auto name = RuleFileName(file->GetFilename());
				const auto text = ReadResource(std::string(kI4RulesDir) + "/" + name);
				if (!text) {
					continue;
				}
				const auto before = state.rules.Size();
				state.rules.AddFileText(*text, name, diags);
				++state.files;
				logger::debug("Icons: {} I4 rules from {}", state.rules.Size() - before, name);
			}
		} catch (const std::exception& e) {
			logger::error("Icons: failed to load I4 rules: {}", e.what());
		}
		home::Store::LogDiagnostics(diags, "Icons");
		logger::info("Icons: {} I4 rules loaded from {} files ({} errors, {} warnings)", state.rules.Size(), state.files,
			diags.ErrorCount(), diags.WarningCount());
	}

	bool HasI4Rules() noexcept
	{
		return Get().rules.Size() > 0;
	}

	std::size_t RuleCount() noexcept
	{
		return Get().rules.Size();
	}

	core::IconRef ForItem(const core::ItemInfo& a_item)
	{
		return Get().rules.Resolve(a_item);
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
		const auto&                           categories = home::Store::Get().Categories();
		std::map<std::string, std::int32_t> votes;
		for (const auto& [item, container] : a_layout.learned) {
			if (!(container == *ref)) {
				continue;
			}
			auto* form = game::LookupForm(item);
			auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
			core::ItemInfo info;
			if (!object || !game::FillBaseInfo(info, object)) {
				continue;
			}
			if (const auto category = categories.Classify(info)) {
				++votes[*category];
			}
		}
		const auto best = std::ranges::max_element(votes, {}, [](const auto& a_vote) { return a_vote.second; });
		if (best != votes.end()) {
			return ForCategory(best->first);
		}
		return {};
	}
}
