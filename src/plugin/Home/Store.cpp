#include "Home/Store.h"

#include "Game/FormUtil.h"

#include <cctype>
#include <charconv>
#include <ctime>
#include <fstream>
#include <sstream>

namespace dova::home
{
	namespace
	{
		std::filesystem::path Root()
		{
			return std::filesystem::path{ Store::kRoot };
		}

		std::vector<core::FormRef> ResolveFormList(const core::FormRef& a_ref)
		{
			std::vector<core::FormRef> out;
			auto*                      form = game::LookupForm(a_ref);
			auto*                      list = form ? form->As<RE::BGSListForm>() : nullptr;
			if (!list) {
				return out;
			}
			for (auto* member : list->forms) {
				if (auto ref = game::ToFormRef(member)) {
					out.push_back(std::move(*ref));
				}
			}
			if (list->scriptAddedTempForms) {
				for (const auto id : *list->scriptAddedTempForms) {
					if (auto ref = game::ToFormRef(RE::TESForm::LookupByID(id))) {
						out.push_back(std::move(*ref));
					}
				}
			}
			return out;
		}
	}

	Store& Store::Get()
	{
		static Store store;
		return store;
	}

	void Store::LogDiagnostics(const core::Diagnostics& a_diags, std::string_view a_context)
	{
		for (const auto& d : a_diags.Items()) {
			if (d.severity == core::Diagnostic::Severity::Error) {
				logger::error("{}: {}", a_context, d.ToString());
			} else {
				logger::warn("{}: {}", a_context, d.ToString());
			}
		}
	}

	void Store::Load()
	{
		LoadCategories();
		LoadTemplates();
		LoadGlobalKeep();
	}

	void Store::LoadCategories()
	{
		core::Diagnostics diags;
		try {
			auto defs = core::LoadCategoryDirectory(Root() / "Categories", diags);
			_categories = core::CategorySet::Compile(std::move(defs), ResolveFormList, diags);
		} catch (const std::exception& e) {
			logger::error("Categories: failed to load: {}", e.what());
			_categories = {};
		}
		LogDiagnostics(diags, "Categories");
		logger::info("Categories: {} loaded ({} errors, {} warnings)", _categories.Size(), diags.ErrorCount(), diags.WarningCount());
		_categoryDiags = std::move(diags);
		if (_categories.Size() == 0) {
			logger::warn("Categories: none loaded; everything will be treated as homeless (master container)");
		}
	}

	void Store::LoadTemplates()
	{
		core::Diagnostics diags;
		try {
			_templates = core::LoadTemplateDirectory(Root() / "Templates", diags);
			// LoadTemplateDirectory already ran ValidateTemplate; only the category check is left.
			for (const auto& t : _templates) {
				core::ValidateLayoutCategories(t.layout, _categories, t.source, diags);
			}
		} catch (const std::exception& e) {
			logger::error("Templates: failed to load: {}", e.what());
			_templates.clear();
		}
		LogDiagnostics(diags, "Templates");
		logger::info("Templates: {} loaded ({} errors, {} warnings)", _templates.size(), diags.ErrorCount(), diags.WarningCount());
		_templateDiags = std::move(diags);
	}

	bool Store::SaveGlobalKeep(core::KeepConfig a_keep)
	{
		const auto      path = Root() / "Settings.json";
		nlohmann::json  doc = nlohmann::json::object();
		std::error_code ec;
		if (std::filesystem::exists(path, ec)) {
			// Never overwrite a file we cannot read: the player may have other settings in it.
			try {
				std::ifstream     in(path, std::ios::binary);
				std::stringstream buffer;
				buffer << in.rdbuf();
				doc = nlohmann::json::parse(buffer.str(), nullptr, true, true);
			} catch (const std::exception& e) {
				logger::error("Keep defaults: not saved, {} cannot be read ({}). Fix or delete it first.", path.generic_string(), e.what());
				return false;
			}
			if (!doc.is_object()) {
				logger::error("Keep defaults: not saved, {} is not a JSON object. Fix or delete it first.", path.generic_string());
				return false;
			}
		}
		doc["keep"] = core::KeepConfigToJson(a_keep);
		// Write a temporary file, then replace: a crash mid-write never leaves a half-written file.
		// (JSON comments in an existing file are not kept: nlohmann::json does not preserve them.)
		try {
			std::filesystem::create_directories(path.parent_path(), ec);
			auto temp = path;
			temp += ".tmp";
			{
				std::ofstream out(temp, std::ios::binary | std::ios::trunc);
				out << doc.dump(2) << '\n';
				if (!out) {
					logger::error("Keep defaults: could not write {}", temp.generic_string());
					return false;
				}
			}
			std::filesystem::rename(temp, path);
		} catch (const std::exception& e) {
			logger::error("Keep defaults: could not write {}: {}", path.generic_string(), e.what());
			return false;
		}
		_globalKeep = std::move(a_keep);
		_settingsDiags.Clear();
		logger::info("Keep defaults: saved to {} (always {}, never {}, quotas {})", path.generic_string(), _globalKeep.always.size(),
			_globalKeep.never.size(), _globalKeep.quotas.size());
		return true;
	}

	void Store::LoadGlobalKeep()
	{
		_globalKeep = core::DefaultKeepConfig();
		_settingsDiags.Clear();

		const auto path = Root() / "Settings.json";
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) {
			logger::info("Keep defaults: built-in (no {})", path.generic_string());
			return;
		}

		try {
			std::ifstream     in(path, std::ios::binary);
			std::stringstream buffer;
			buffer << in.rdbuf();
			const auto doc = nlohmann::json::parse(buffer.str(), nullptr, true, true);  // allow comments
			if (!doc.is_object() || !doc.contains("keep")) {
				logger::info("Keep defaults: built-in ({} has no \"keep\" object)", path.generic_string());
				return;
			}
			core::Diagnostics diags;
			auto              keep = core::ParseKeepConfig(doc.at("keep"), "Settings.json", diags);
			LogDiagnostics(diags, "Settings.json");
			_settingsDiags = diags;
			if (keep) {
				_globalKeep = std::move(*keep);
				logger::info("Keep defaults: loaded from Settings.json (always {}, never {}, quotas {})",
					_globalKeep.always.size(), _globalKeep.never.size(), _globalKeep.quotas.size());
			} else {
				logger::warn("Keep defaults: Settings.json keep block unusable, using built-in defaults");
			}
		} catch (const std::exception& e) {
			logger::error("Keep defaults: cannot read {}: {}; using built-in defaults", path.generic_string(), e.what());
		}
	}

	std::filesystem::path Store::LayoutPath(const core::FormRef& a_cell) const
	{
		return Root() / "Layouts" / core::LayoutFileName(a_cell);
	}

	std::optional<core::Layout> Store::LoadPlayerLayout(const core::FormRef& a_cell, bool& a_broken) const
	{
		a_broken = false;
		const auto      path = LayoutPath(a_cell);
		std::error_code ec;
		const bool      exists = std::filesystem::exists(path, ec);
		if (ec) {
			// Unknown state: treat as broken so a later save cannot overwrite a file we failed to see.
			logger::error("Layout {}: cannot check the file ({}); leaving it untouched", path.generic_string(), ec.message());
			a_broken = true;
			return std::nullopt;
		}
		if (!exists) {
			return std::nullopt;
		}
		core::Diagnostics diags;
		std::optional<core::Layout> layout;
		try {
			layout = core::LoadLayoutFile(path, "Layouts/" + core::LayoutFileName(a_cell), diags);
		} catch (const std::exception& e) {
			logger::error("Layout {}: {}", path.generic_string(), e.what());
		}
		LogDiagnostics(diags, "Layout");
		if (!layout) {
			// Do not touch the file: the player can fix it by hand. The home behaves as not set up,
			// and saving is refused for this cell (see HomeRegistry) so we never overwrite it.
			logger::error("Layout {} could not be parsed; leaving it untouched", path.generic_string());
			a_broken = true;
		} else {
			if (layout->cell && *layout->cell != a_cell) {
				logger::warn("Layout {} declares cell {} but is stored for {}; it is used (and saved) for {}", path.generic_string(),
					layout->cell->ToString(), a_cell.ToString(), a_cell.ToString());
			}
			layout->cell = a_cell;  // the file name decides which home a layout belongs to
			core::Diagnostics catDiags;
			core::ValidateLayoutCategories(*layout, _categories, path.filename().string(), catDiags);
			LogDiagnostics(catDiags, "Layout");
		}
		return layout;
	}

	bool Store::SavePlayerLayout(const core::FormRef& a_cell, const core::Layout& a_layout) const
	{
		const auto path = LayoutPath(a_cell);
		bool       ok = false;
		try {
			if (a_layout.cell && *a_layout.cell == a_cell) {
				ok = core::SaveLayoutFile(path, a_layout);
			} else {
				auto copy = a_layout;
				copy.cell = a_cell;
				ok = core::SaveLayoutFile(path, copy);
			}
		} catch (const std::exception& e) {
			logger::error("Saving {} failed: {}", path.generic_string(), e.what());
		}
		if (ok) {
			logger::info("Saved layout {}", path.generic_string());
		} else {
			logger::error("Saving {} failed", path.generic_string());
		}
		return ok;
	}

	namespace
	{
		std::string Timestamp()
		{
			const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			std::tm    local{};
			localtime_s(&local, &now);
			char buffer[32]{};
			std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
			return buffer;
		}

		// "<dir>/<file>.<timestamp>.bak", made unique if a backup was already taken this second.
		std::filesystem::path NewBackupPath(const std::filesystem::path& a_path)
		{
			const auto base = a_path.filename().string() + "." + Timestamp();
			for (int i = 0; i < 100; ++i) {
				const auto      name = i == 0 ? base + ".bak" : std::format("{}-{}.bak", base, i);
				auto            candidate = a_path.parent_path() / name;
				std::error_code ec;
				if (!std::filesystem::exists(candidate, ec) && !ec) {
					return candidate;
				}
			}
			return {};
		}

		// Deletes all but the newest `a_keep` timestamped backups of `a_path` (a legacy "<file>.bak" is left alone).
		void PruneBackups(const std::filesystem::path& a_path, std::size_t a_keep)
		{
			const auto               prefix = a_path.filename().string() + ".";
			std::vector<std::string> names;
			std::error_code          ec;
			for (auto it = std::filesystem::directory_iterator(a_path.parent_path(), ec); !ec && it != std::filesystem::directory_iterator();
				 it.increment(ec)) {
				const auto name = it->path().filename().string();
				// "<file>.YYYYMMDD-HHMMSS[-n].bak": a digit right after the prefix tells it from "<file>.bak".
				if (name.size() > prefix.size() + 4 && name.starts_with(prefix) && name.ends_with(".bak") &&
					std::isdigit(static_cast<unsigned char>(name[prefix.size()]))) {
					names.push_back(name);
				}
			}
			if (names.size() <= a_keep) {
				return;
			}
			// Oldest first: by timestamp, then by the "-n" suffix of backups taken within the same second
			// (a plain string sort would put "-1.bak" before ".bak").
			const auto key = [&](const std::string& a_name) {
				const auto stem = std::string_view(a_name).substr(prefix.size(), a_name.size() - prefix.size() - 4);  // drop ".bak"
				const auto stamp = stem.substr(0, std::min<std::size_t>(stem.size(), 15));                         // YYYYMMDD-HHMMSS
				int        n = 0;
				if (stem.size() > 16 && stem[15] == '-') {
					std::from_chars(stem.data() + 16, stem.data() + stem.size(), n);
				}
				return std::pair{ std::string(stamp), n };
			};
			std::ranges::sort(names, [&](const std::string& a_lhs, const std::string& a_rhs) { return key(a_lhs) < key(a_rhs); });
			for (std::size_t i = 0; i + a_keep < names.size(); ++i) {
				std::error_code removeEc;
				std::filesystem::remove(a_path.parent_path() / names[i], removeEc);
				if (removeEc) {
					logger::warn("Could not delete old backup {}: {}", names[i], removeEc.message());
				} else {
					logger::debug("Deleted old backup {}", names[i]);
				}
			}
		}

		// Moves (rename) or copies the layout file to a new timestamped backup. True if there is no file.
		bool MakeBackup(const std::filesystem::path& a_path, bool a_move, std::string_view a_context)
		{
			std::error_code ec;
			const bool      exists = std::filesystem::exists(a_path, ec);
			if (ec) {
				logger::error("{}: cannot check {}: {}", a_context, a_path.generic_string(), ec.message());
				return false;
			}
			if (!exists) {
				return true;
			}
			const auto backup = NewBackupPath(a_path);
			if (backup.empty()) {
				logger::error("{}: no free backup name for {}", a_context, a_path.generic_string());
				return false;
			}
			if (a_move) {
				std::filesystem::rename(a_path, backup, ec);
			} else {
				std::filesystem::copy_file(a_path, backup, std::filesystem::copy_options::none, ec);
			}
			if (ec) {
				logger::error("{}: could not back up {} to {}: {}", a_context, a_path.generic_string(), backup.generic_string(), ec.message());
				return false;
			}
			logger::info("{}: layout backed up to {}", a_context, backup.generic_string());
			PruneBackups(a_path, Store::kMaxBackups);
			return true;
		}
	}

	bool Store::BackupPlayerLayout(const core::FormRef& a_cell) const
	{
		return MakeBackup(LayoutPath(a_cell), false, "Backup");
	}

	bool Store::BackupAndRemovePlayerLayout(const core::FormRef& a_cell) const
	{
		return MakeBackup(LayoutPath(a_cell), true, "Reset");
	}
}
