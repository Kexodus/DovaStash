#include "ui/State.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Util.h"

namespace fs = std::filesystem;

namespace dova::editor::ui
{
	const char* HomeStageName(HomeStage a_stage)
	{
		switch (a_stage) {
		case HomeStage::Drafted:
			return "drafted";
		case HomeStage::Reviewed:
			return "reviewed";
		case HomeStage::Validated:
			return "validated";
		case HomeStage::Saved:
			return "saved";
		default:
			return "to do";
		}
	}

	fs::path EditorState::File()
	{
		wchar_t*    local = nullptr;
		std::size_t len = 0;
		fs::path    base;
		// Tests and screenshots keep their own state (DOVASTASH_EDITOR_STATE = a file path).
		if (_wdupenv_s(&local, &len, L"DOVASTASH_EDITOR_STATE") == 0 && local) {
			fs::path file = local;
			std::free(local);
			return file;
		}
		if (_wdupenv_s(&local, &len, L"LOCALAPPDATA") == 0 && local) {
			base = local;
			std::free(local);
		} else {
			base = fs::temp_directory_path();
		}
		return base / L"DovaStashEditor" / L"state.json";
	}

	void EditorState::Load()
	{
		const auto text = ReadTextFile(File());
		if (!text) {
			return;
		}
		try {
			const auto j = nlohmann::json::parse(*text, nullptr, true, true);
			if (j.contains("mo2") && j["mo2"].is_string()) {
				mo2 = j["mo2"].get<std::string>();
			}
			if (j.contains("profile") && j["profile"].is_string()) {
				profile = j["profile"].get<std::string>();
			}
			author = j.value("author", std::string{});
			if (j.contains("homes") && j["homes"].is_object()) {
				for (const auto& [cell, h] : j["homes"].items()) {
					HomeRecord r;
					r.name = h.value("name", std::string{});
					r.stage = static_cast<HomeStage>(std::clamp(h.value("stage", 0), 0, 4));
					r.savedPath = h.value("saved", std::string{});
					r.updated = h.value("updated", std::string{});
					homes[cell] = std::move(r);
				}
			}
		} catch (const std::exception&) {
			// A broken state file is not worth an error: start fresh (it is rewritten on the next change).
		}
	}

	void EditorState::Save() const
	{
		nlohmann::ordered_json j;
		if (mo2) {
			j["mo2"] = *mo2;
		}
		if (profile) {
			j["profile"] = *profile;
		}
		j["author"] = author;
		auto& hs = j["homes"] = nlohmann::ordered_json::object();
		for (const auto& [cell, r] : homes) {
			hs[cell] = { { "name", r.name }, { "stage", static_cast<int>(r.stage) }, { "saved", r.savedPath }, { "updated", r.updated } };
		}
		std::error_code ec;
		const auto      path = File();
		fs::create_directories(path.parent_path(), ec);
		const auto tmp = fs::path(path.native() + L".tmp");
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) {
				return;
			}
			out << j.dump(1, '\t') << "\n";
		}
		fs::rename(tmp, path, ec);
	}

	void EditorState::Advance(const std::string& a_cell, const std::string& a_name, HomeStage a_stage, const std::string& a_savedPath, bool a_force)
	{
		auto& r = homes[a_cell];
		if (!a_name.empty()) {
			r.name = a_name;
		}
		if (!a_force && static_cast<int>(a_stage) <= static_cast<int>(r.stage) && a_savedPath.empty()) {
			return;
		}
		if (a_force || static_cast<int>(a_stage) > static_cast<int>(r.stage)) {
			r.stage = a_stage;
		}
		if (!a_savedPath.empty()) {
			r.savedPath = a_savedPath;
		}
		try {
			r.updated = std::format("{:%Y-%m-%d %H:%M}", std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now()) });
		} catch (const std::exception&) {
			r.updated = std::format("{:%Y-%m-%d %H:%M}", std::chrono::floor<std::chrono::minutes>(std::chrono::system_clock::now()));
		}
		Save();
	}

	HomeStage EditorState::StageOf(const std::string& a_cell) const
	{
		const auto it = homes.find(a_cell);
		return it == homes.end() ? HomeStage::None : it->second.stage;
	}
}
