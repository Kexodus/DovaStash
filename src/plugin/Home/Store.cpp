#include "Home/Store.h"

#include "Game/FormUtil.h"
#include "Log.h"

#include <cctype>
#include <charconv>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace dova::home
{
	namespace
	{
		std::filesystem::path Root()
		{
			return std::filesystem::path{ Store::kRoot };
		}

		std::string Utf8(const std::filesystem::path& a_path)
		{
			try {
				const auto text = a_path.generic_u8string();
				return std::string(text.begin(), text.end());
			} catch (...) {
				return "(unprintable path)"s;
			}
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
		BackupExistingLayouts();
		LoadHomeCellIndex();
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

	void Store::ReloadTemplates()
	{
		const auto before = _templates.size();
		LoadTemplates();
		logger::debug("Templates: rescanned ({} before, {} now)", before, _templates.size());
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
			IndexHomeCells(a_cell, &*layout);  // the file may have been edited by hand since Load()
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
			IndexHomeCells(a_cell, &a_layout);
		} else {
			logger::error("Saving {} failed", path.generic_string());
		}
		// The copy outside Data is kept even when this write failed: it then holds the only saved state.
		BackupHome(a_cell, a_layout);
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
			const auto stamp = Timestamp();
			for (int i = 0; i < 100; ++i) {
				auto candidate = a_path;
				candidate += i == 0 ? std::format(".{}.bak", stamp) : std::format(".{}-{}.bak", stamp, i);
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
			const auto               prefix = Utf8(a_path.filename()) + ".";
			std::vector<std::string> names;
			std::error_code          ec;
			for (auto it = std::filesystem::directory_iterator(a_path.parent_path(), ec); !ec && it != std::filesystem::directory_iterator();
				 it.increment(ec)) {
				const auto name = Utf8(it->path().filename());
				// "<file>.YYYYMMDD-HHMMSS[-n].bak": a digit right after the prefix tells it from "<file>.bak".
				if (name.size() > prefix.size() + 4 && name.starts_with(prefix) && name.ends_with(".bak") &&
					std::isdigit(static_cast<unsigned char>(name[prefix.size()]))) {
					names.push_back(name);
				}
			}
			if (names.size() <= a_keep) {
				return;
			}
			const auto pathOf = [&](const std::string& a_name) {
				return a_path.parent_path() / std::filesystem::path(std::u8string(a_name.begin(), a_name.end()));
			};
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
				std::filesystem::remove(pathOf(names[i]), removeEc);
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
				logger::error("{}: cannot check {}: {}", a_context, Utf8(a_path), ec.message());
				return false;
			}
			if (!exists) {
				return true;
			}
			const auto backup = NewBackupPath(a_path);
			if (backup.empty()) {
				logger::error("{}: no free backup name for {}", a_context, Utf8(a_path));
				return false;
			}
			if (a_move) {
				std::filesystem::rename(a_path, backup, ec);
			} else {
				std::filesystem::copy_file(a_path, backup, std::filesystem::copy_options::none, ec);
			}
			if (ec) {
				logger::error("{}: could not back up {} to {}: {}", a_context, Utf8(a_path), Utf8(backup), ec.message());
				return false;
			}
			logger::info("{}: {} backed up to {}", a_context, Utf8(a_path.filename()), Utf8(backup));
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
		const bool ok = MakeBackup(LayoutPath(a_cell), true, "Reset");
		if (ok) {
			IndexHomeCells(a_cell, nullptr);  // its extra cells belong to no home any more
		}
		return ok;
	}

	bool Store::BackupFile(const std::filesystem::path& a_path, std::string_view a_context)
	{
		return MakeBackup(a_path, false, a_context);
	}

	// --- home backups outside Data ----------------------------------------------------------------------

	namespace
	{
		struct BackupJob
		{
			core::Layout layout;
			bool         keepHistory{ false };  // move the current latest copy to history first
		};

		// One writer thread at a time, started on demand; the newest layout per file wins.
		struct BackupQueue
		{
			std::mutex                                  lock;
			std::map<std::filesystem::path, BackupJob>  pending;
			std::set<std::string>                       historyKept;  // files whose copy went to history this session
			bool                                        running{ false };
		};

		BackupQueue& Queue()
		{
			static BackupQueue queue;
			return queue;
		}

		void BackupFailed(std::string_view a_what)
		{
			if (log::FirstTime("home-backup-failed")) {
				logger::warn("Home backup: {} (only the copy outside Data; the setup file under Data is not affected)", a_what);
			} else {
				logger::debug("Home backup: {}", a_what);
			}
		}

		void WriteHomeBackup(const std::filesystem::path& a_path, const BackupJob& a_job)
		{
			if (a_job.keepHistory && !MakeBackup(a_path, false, "Home backup")) {
				BackupFailed(std::format("could not keep a history copy of {}", Utf8(a_path)));
			}
			if (core::SaveLayoutFile(a_path, a_job.layout)) {
				logger::debug("Home backup: wrote {}", Utf8(a_path));
			} else {
				BackupFailed(std::format("could not write {}", Utf8(a_path)));
			}
		}

		void RunBackupQueue()
		{
			auto& queue = Queue();
			for (;;) {
				std::filesystem::path path;
				BackupJob             job;
				{
					std::scoped_lock guard(queue.lock);
					if (queue.pending.empty()) {
						queue.running = false;
						return;
					}
					auto node = queue.pending.extract(queue.pending.begin());
					path = std::move(node.key());
					job = std::move(node.mapped());
				}
				try {
					WriteHomeBackup(path, job);
				} catch (const std::exception& e) {
					BackupFailed(std::format("{}: {}", Utf8(path), e.what()));
				} catch (...) {
					BackupFailed(std::format("{}: unknown error", Utf8(path)));
				}
			}
		}

		// Seconds since 1970 of a file's last write. MSVC's file clock counts 100 ns ticks from 1601 (FILETIME);
		// converted by hand, as clock_cast would load the time zone database.
		std::optional<std::int64_t> WriteTime(const std::filesystem::path& a_path)
		{
			std::error_code ec;
			const auto      time = std::filesystem::last_write_time(a_path, ec);
			if (ec) {
				return std::nullopt;
			}
			constexpr std::int64_t kFrom1601To1970 = 11644473600;
			return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count() - kFrom1601To1970;
		}

		std::string LocalDate(std::int64_t a_time)
		{
			const auto time = static_cast<std::time_t>(a_time);
			std::tm    local{};
			if (localtime_s(&local, &time) != 0) {
				return "?"s;
			}
			char buffer[32]{};
			std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
			return buffer;
		}

		std::optional<Store::HomeBackup> DescribeBackup(const std::filesystem::path& a_path, bool a_history)
		{
			std::error_code ec;
			if (!std::filesystem::is_regular_file(a_path, ec) || ec) {
				return std::nullopt;
			}
			Store::HomeBackup backup;
			backup.path = a_path;
			backup.history = a_history;
			backup.time = WriteTime(a_path).value_or(0);
			backup.date = backup.time > 0 ? LocalDate(backup.time) : "?"s;
			return backup;
		}
	}

	std::string Store::PathText(const std::filesystem::path& a_path)
	{
		return Utf8(a_path);
	}

	const std::optional<std::filesystem::path>& Store::BackupDir()
	{
		static const std::optional<std::filesystem::path> dir = []() -> std::optional<std::filesystem::path> {
			auto path = SKSE::log::log_directory();  // Documents/My Games/<edition>/SKSE, as for the log
			if (!path) {
				logger::warn("Home backup: Documents not found; setups are only saved under Data");
				return std::nullopt;
			}
			*path /= "DovaStash";
			*path /= "Backups";
			logger::info("Home backup: copies go to {}", Utf8(*path));
			return path;
		}();
		return dir;
	}

	void Store::BackupHome(const core::FormRef& a_cell, const core::Layout& a_layout) const
	{
		const auto& dir = BackupDir();
		if (!dir) {
			return;
		}
		auto path = *dir / core::LayoutFileName(a_cell);
		auto copy = a_layout;
		copy.cell = a_cell;

		auto&            queue = Queue();
		std::scoped_lock guard(queue.lock);
		const bool       firstThisSession = queue.historyKept.insert(core::LayoutFileName(a_cell)).second;
		auto&            job = queue.pending[path];
		job.layout = std::move(copy);
		job.keepHistory = job.keepHistory || firstThisSession;
		if (!queue.running) {
			queue.running = true;
			try {
				std::thread(RunBackupQueue).detach();
			} catch (const std::exception& e) {
				queue.running = false;
				queue.pending.clear();
				BackupFailed(std::format("no writer thread: {}", e.what()));
			}
		}
	}

	void Store::BackupExistingLayouts() const
	{
		const auto& dir = BackupDir();
		if (!dir) {
			return;
		}
		std::size_t     copied = 0;
		std::error_code ec;
		for (auto it = std::filesystem::directory_iterator(Root() / "Layouts", ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			std::error_code fileEc;
			if (!it->is_regular_file(fileEc) || it->path().extension() != ".json") {
				continue;
			}
			const auto target = *dir / it->path().filename();
			if (std::filesystem::exists(target, fileEc) || fileEc) {
				continue;  // never replace a copy (it may be newer than a file restored by hand)
			}
			std::filesystem::create_directories(*dir, fileEc);
			auto temp = target;
			temp += ".tmp";
			std::filesystem::copy_file(it->path(), temp, std::filesystem::copy_options::overwrite_existing, fileEc);
			if (!fileEc) {
				std::filesystem::rename(temp, target, fileEc);
			}
			if (fileEc) {
				std::error_code ignored;
				std::filesystem::remove(temp, ignored);
				BackupFailed(std::format("could not copy {} to {}: {}", Utf8(it->path()), Utf8(target), fileEc.message()));
				continue;
			}
			++copied;
		}
		if (copied > 0) {
			logger::info("Home backup: {} setup file(s) that had no copy yet copied to {}", copied, Utf8(*dir));
		}
	}

	void Store::KeepHomeBackupHistoryNext(const core::FormRef& a_cell) const
	{
		auto&            queue = Queue();
		std::scoped_lock guard(queue.lock);
		queue.historyKept.erase(core::LayoutFileName(a_cell));
	}

	std::optional<Store::HomeBackup> Store::LatestHomeBackup(const core::FormRef& a_cell) const
	{
		const auto& dir = BackupDir();
		return dir ? DescribeBackup(*dir / core::LayoutFileName(a_cell), false) : std::nullopt;
	}

	std::vector<Store::HomeBackup> Store::HomeBackups(const core::FormRef& a_cell) const
	{
		std::vector<HomeBackup> out;
		const auto&             dir = BackupDir();
		if (!dir) {
			return out;
		}
		const auto latest = *dir / core::LayoutFileName(a_cell);
		if (auto backup = DescribeBackup(latest, false)) {
			out.push_back(std::move(*backup));
		}
		// History: "<file>.YYYYMMDD-HHMMSS[-n].bak" (MakeBackup). A copy keeps the time its setup was saved.
		const auto      prefix = core::LayoutFileName(a_cell) + ".";
		std::error_code ec;
		std::vector<HomeBackup> history;
		for (auto it = std::filesystem::directory_iterator(*dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			const auto name = Utf8(it->path().filename());
			if (name.size() > prefix.size() + 4 && name.starts_with(prefix) && name.ends_with(".bak") &&
				std::isdigit(static_cast<unsigned char>(name[prefix.size()]))) {
				if (auto backup = DescribeBackup(it->path(), true)) {
					history.push_back(std::move(*backup));
				}
			}
		}
		std::ranges::sort(history, [](const HomeBackup& a_lhs, const HomeBackup& a_rhs) {
			return a_lhs.time != a_rhs.time ? a_lhs.time > a_rhs.time : a_lhs.path > a_rhs.path;
		});
		out.insert(out.end(), std::make_move_iterator(history.begin()), std::make_move_iterator(history.end()));
		return out;
	}

	std::optional<core::Layout> Store::LoadHomeBackup(const core::FormRef& a_cell, const std::filesystem::path& a_path) const
	{
		core::Diagnostics           diags;
		std::optional<core::Layout> layout;
		try {
			layout = core::LoadLayoutFile(a_path, "Backups/" + Utf8(a_path.filename()), diags);
		} catch (const std::exception& e) {
			logger::error("Home backup {}: {}", Utf8(a_path), e.what());
		}
		LogDiagnostics(diags, "Home backup");
		if (!layout) {
			logger::error("Home backup {} could not be parsed; it is not used (and left as it is)", Utf8(a_path));
			return std::nullopt;
		}
		if (layout->cell && *layout->cell != a_cell) {
			logger::warn("Home backup {} declares cell {}; it is used for {} (the file name decides)", Utf8(a_path), layout->cell->ToString(), a_cell.ToString());
		}
		layout->cell = a_cell;
		core::Diagnostics catDiags;
		core::ValidateLayoutCategories(*layout, _categories, Utf8(a_path.filename()), catDiags);
		LogDiagnostics(catDiags, "Home backup");
		return layout;
	}
}
