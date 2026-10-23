#include "ui/State.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Log.h"
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
		if (memoryOnly) {
			return;
		}
		const auto text = ReadTextFile(path.empty() ? File() : path);
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
			if (j.contains("data") && j["data"].is_string()) {
				data = j["data"].get<std::string>();
			}
			author = j.value("author", std::string{});
			saveTarget = j.value("saveTarget", std::string{});
			if (j.contains("ui") && j["ui"].is_object()) {
				// Added with the one-workspace layout: older files have none, every key is optional.
				const auto& u = j["ui"];
				ui.railOpen = u.value("railOpen", ui.railOpen);
				ui.railWidth = u.value("railWidth", ui.railWidth);
				ui.inspectorOpen = u.value("inspectorOpen", ui.inspectorOpen);
				ui.inspectorWidth = u.value("inspectorWidth", ui.inspectorWidth);
				ui.drawerOpen = u.value("drawerOpen", ui.drawerOpen);
				ui.drawerHeight = u.value("drawerHeight", ui.drawerHeight);
				ui.view = std::clamp(u.value("view", ui.view), 0, 1);
				ui.textScale = u.value("textScale", ui.textScale);
				if (ui.textScale != 0.0f && (ui.textScale < 1.0f || ui.textScale > 2.5f)) {
					ui.textScale = 0.0f;
				}
				if (u.contains("theme") && u["theme"].is_string()) {
					ui.theme = u["theme"].get<std::string>();
				}
			}
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
		if (memoryOnly) {
			return;
		}
		nlohmann::ordered_json j;
		if (mo2) {
			j["mo2"] = *mo2;
		}
		if (profile) {
			j["profile"] = *profile;
		}
		if (data) {
			j["data"] = *data;
		}
		j["author"] = author;
		if (!saveTarget.empty()) {
			j["saveTarget"] = saveTarget;
		}
		const UiLayout& u = uiOnDisk ? *uiOnDisk : ui;
		j["ui"] = { { "railOpen", u.railOpen }, { "railWidth", u.railWidth }, { "inspectorOpen", u.inspectorOpen }, { "inspectorWidth", u.inspectorWidth },
			{ "drawerOpen", u.drawerOpen }, { "drawerHeight", u.drawerHeight }, { "view", u.view }, { "textScale", u.textScale } };
		if (!u.theme.empty()) {
			j["ui"]["theme"] = u.theme;
		}
		auto& hs = j["homes"] = nlohmann::ordered_json::object();
		for (const auto& [cell, r] : homes) {
			hs[cell] = { { "name", r.name }, { "stage", static_cast<int>(r.stage) }, { "saved", r.savedPath }, { "updated", r.updated } };
		}
		std::error_code ec;
		const auto      file = path.empty() ? File() : path;
		fs::create_directories(file.parent_path(), ec);
		const auto tmp = fs::path(file.native() + L".tmp");
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) {
				return;
			}
			out << j.dump(1, '\t') << "\n";
		}
		fs::rename(tmp, file, ec);
		if (ec) {
			// Replacing can fail while another program holds the old file: remove it, then move the new one in.
			std::error_code ec2;
			fs::remove(file, ec2);
			fs::rename(tmp, file, ec2);
			if (ec2) {
				Log("state: could not write {}: {}", PathToUtf8(file), ec2.message());
			}
		}
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
