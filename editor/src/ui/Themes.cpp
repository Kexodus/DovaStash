// The built-in colour themes and the player's theme files (ui/Themes.h, docs/EDITOR-UX.md §3.1).

#include "ui/Themes.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <fstream>
#include <initializer_list>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <nlohmann/json.hpp>

#include "Util.h"
#include "dova/core/FormRef.h"

namespace fs = std::filesystem;

namespace dova::editor::ui::w
{
	namespace
	{
		struct Key
		{
			const char*                  name;
			std::uint32_t ThemeColours::*field;
		};

		// The file's keys, in the order they are written.
		constexpr std::array<Key, 15> kKeys{ { { "bg0", &ThemeColours::bg0 }, { "bg1", &ThemeColours::bg1 }, { "bg2", &ThemeColours::bg2 },
			{ "bg3", &ThemeColours::bg3 }, { "line", &ThemeColours::line }, { "text", &ThemeColours::text }, { "muted", &ThemeColours::muted },
			{ "disabled", &ThemeColours::disabled }, { "accent", &ThemeColours::accent }, { "accentDim", &ThemeColours::accentDim },
			{ "select", &ThemeColours::select }, { "ok", &ThemeColours::ok }, { "warn", &ThemeColours::warn }, { "error", &ThemeColours::error },
			{ "info", &ThemeColours::info } } };

		ThemeColours Make(const char* a_name, bool a_dark, std::initializer_list<std::uint32_t> a_values)
		{
			ThemeColours c;
			c.name = a_name;
			c.dark = a_dark;
			std::size_t i = 0;
			for (const std::uint32_t v : a_values) {
				if (i < kKeys.size()) {
					c.*(kKeys[i++].field) = v;
				}
			}
			return c;
		}

		std::vector<ThemeColours> MakeBuiltIns()
		{
			// Order of the values: bg0 bg1 bg2 bg3 line, text muted disabled, accent accentDim select, ok warn error info.
			// Body text is at least 7:1 on bg0 to bg2; captions, links and status hues at least 4.5:1. Light themes
			// take darker status hues and accents so the text on a filled accent (bg0) stays readable. `select` is the
			// plan's highlight: the viewport stays dark in every theme (the 3D view's own sky), so it stays white.
			return {
				// The press kit's brand (presskit/README.md, Style): black, white and neutral grays.
				Make("Dova Stash", true, { 0x000000, 0x0e0e0e, 0x1e1e1e, 0x2a2a2a, 0x3c3c3c, 0xd4d4d4, 0x9c9c9c, 0x6a6a6a, 0xffffff, 0x9c9c9c, 0xffffff,
											 0x7fbf7f, 0xe0b04a, 0xd9634c, 0x6fa8dc }),
				// Dark.
				Make("Windows10 Dark", true, { 0x1f1f1f, 0x2b2b2b, 0x383838, 0x444444, 0x545454, 0xf2f2f2, 0xb4b4b4, 0x7f7f7f, 0x4cb0ff, 0x2f7fc0, 0xffffff,
												 0x7cc77c, 0xe8b64c, 0xf8886f, 0x6cb4f0 }),
				Make("Carbon", true, { 0x141414, 0x1c1c1c, 0x282828, 0x333333, 0x444444, 0xe6e6e6, 0xa8a8a8, 0x707070, 0xf29b38, 0xb06a20, 0xffffff, 0x84c47a,
										 0xe6c04a, 0xef6a52, 0x70ade6 }),
				Make("Charcoal Dark Slate", true, { 0x1e2226, 0x262b31, 0x323840, 0x3d444d, 0x4f5863, 0xe4e8ec, 0xa7b0ba, 0x737c86, 0x9cc0e8, 0x6585aa,
													  0xffffff, 0x86c58a, 0xe3b65a, 0xf28a74, 0x7cb2e6 }),
				Make("Tablet Dark", true, { 0x000000, 0x0a0a0a, 0x1a1a1a, 0x2c2c2c, 0x6a6a6a, 0xffffff, 0xd0d0d0, 0x8a8a8a, 0x5cc8ff, 0x2f8fc8, 0xffffff,
											  0x8ee08e, 0xffcc4d, 0xff7a66, 0x7ec4ff }),
				Make("Cyan Night", true, { 0x0c1621, 0x121f2c, 0x1a2b3b, 0x233749, 0x304a60, 0xe0eef6, 0x9fb6c6, 0x66808f, 0x3fd6e6, 0x2496a6, 0xffffff,
											 0x7ccf9a, 0xe6c25c, 0xee7366, 0x6db8f0 }),
				Make("Golden Graphite", true, { 0x1b1b1b, 0x242424, 0x303030, 0x3b3b3b, 0x4c4c4c, 0xe8e4dc, 0xaca598, 0x77726a, 0xe0b44c, 0xa07c2c, 0xffffff,
												  0x8cc77f, 0xf2994a, 0xf47878, 0x76b0e2 }),
				Make("Ruby Graphite", true, { 0x1b1b1b, 0x242424, 0x303030, 0x3b3b3b, 0x4c4c4c, 0xe8e4e4, 0xaca4a4, 0x787070, 0xf27484, 0xb84e5c, 0xffffff,
												0x86c784, 0xe6b84e, 0xf08048, 0x74aee6 }),
				// Light.
				Make("Windows10", false, { 0xf3f3f3, 0xe9e9e9, 0xffffff, 0xdde9f5, 0xc4c4c4, 0x1a1a1a, 0x595959, 0x9a9a9a, 0x0062ad, 0x5a8fc2, 0xffffff,
											 0x1b712f, 0x8a5a00, 0xb3261e, 0x0f62a8 }),
				Make("Light", false, { 0xf4f4f4, 0xeaeaea, 0xffffff, 0xe0e0e0, 0xc2c2c2, 0x161616, 0x575757, 0x9a9a9a, 0x1e1e1e, 0x6a6a6a, 0xffffff, 0x1f7030,
										 0x8a5c00, 0xb02a20, 0x1f5f9e }),
				Make("Tablet Light", false, { 0xffffff, 0xf2f2f2, 0xffffff, 0xe3edf6, 0x8c8c8c, 0x000000, 0x3a3a3a, 0x7a7a7a, 0x004f8f, 0x4f7fae, 0xffffff,
												0x15692a, 0x7a4e00, 0xa01e14, 0x0a5394 }),
				Make("Aqua Light Slate", false, { 0xe6ebef, 0xdce3e9, 0xf6f8fa, 0xd2e4ea, 0xa8b5c0, 0x17212a, 0x4c5a66, 0x8a96a0, 0x006b80, 0x4f8f9e, 0xffffff,
													0x1b6c30, 0x875900, 0xad2a20, 0x155f9c }),
				Make("Lavender Classico", false, { 0xf1eff6, 0xe6e2ef, 0xfbfafd, 0xe3dcf2, 0xc5bdd6, 0x1d1828, 0x554d66, 0x958ca6, 0x5a3fa0, 0x8a77bd, 0xffffff,
													 0x247434, 0x875800, 0xad2a24, 0x2a5ea6 }),
				Make("Emerald Light Slate", false, { 0xe9edeb, 0xdee4e1, 0xf7f9f8, 0xd6e8de, 0xaab8b1, 0x16201b, 0x4b5953, 0x8a9690, 0x0a7048, 0x4a8a6c,
													   0xffffff, 0x1b6f30, 0x855700, 0xab2a22, 0x1d5f99 }),
			};
		}

		std::optional<std::uint32_t> ParseHex(std::string_view a_text)
		{
			a_text = Trim(a_text);
			if (!a_text.empty() && a_text.front() == '#') {
				a_text.remove_prefix(1);
			}
			if (a_text.size() != 6) {
				return std::nullopt;
			}
			std::uint32_t v = 0;
			for (const char c : a_text) {
				int d = 0;
				if (c >= '0' && c <= '9') {
					d = c - '0';
				} else if (c >= 'a' && c <= 'f') {
					d = c - 'a' + 10;
				} else if (c >= 'A' && c <= 'F') {
					d = c - 'A' + 10;
				} else {
					return std::nullopt;
				}
				v = (v << 4) | static_cast<std::uint32_t>(d);
			}
			return v;
		}

		fs::path ExeThemesDir()
		{
			wchar_t     exe[MAX_PATH]{};
			const DWORD n = ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			if (n == 0 || n >= MAX_PATH) {
				return {};
			}
			return fs::path(std::wstring(exe, n)).parent_path() / L"Themes";
		}

		std::vector<ThemeColours>& Cache()
		{
			static std::vector<ThemeColours> s_all;
			return s_all;
		}

		bool g_scanned{ false };

		const ThemeColours* BuiltIn(std::string_view a_name)
		{
			const auto& all = BuiltInThemes();
			const auto  it = std::ranges::find_if(all, [&](const ThemeColours& a_t) { return core::IEquals(a_t.name, a_name); });
			return it != all.end() ? &*it : nullptr;
		}
	}

	const std::vector<ThemeColours>& BuiltInThemes()
	{
		static const std::vector<ThemeColours> s_builtIns = MakeBuiltIns();
		return s_builtIns;
	}

	fs::path UserThemesDir()
	{
		wchar_t*    local = nullptr;
		std::size_t len = 0;
		fs::path    base;
		if (_wdupenv_s(&local, &len, L"LOCALAPPDATA") == 0 && local) {
			base = local;
			std::free(local);
		} else {
			base = fs::temp_directory_path();
		}
		return base / L"DovaStashEditor" / L"Themes";
	}

	std::optional<ThemeColours> ParseTheme(std::string_view a_json)
	{
		try {
			const auto j = nlohmann::json::parse(a_json, nullptr, true, true);
			if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
				return std::nullopt;
			}
			const std::string name(Trim(j["name"].get<std::string>()));
			if (name.empty() || name.size() > 64) {
				return std::nullopt;
			}
			const bool dark = j.value("dark", true);
			// What the file leaves out (or cannot be read) comes from the plainest theme of its kind.
			const auto* base = BuiltIn(dark ? "Dova Stash" : "Light");
			ThemeColours c = base ? *base : BuiltInThemes().front();
			c.name = name;
			c.dark = dark;
			c.user = true;
			c.file.clear();
			const auto it = j.find("colours") != j.end() ? j.find("colours") : j.find("colors");
			if (it == j.end() || !it->is_object()) {
				return std::nullopt;
			}
			for (const auto& k : kKeys) {
				if (const auto v = it->find(k.name); v != it->end() && v->is_string()) {
					if (const auto hex = ParseHex(v->get<std::string>())) {
						c.*(k.field) = *hex;
					}
				}
			}
			return c;
		} catch (const std::exception&) {
			return std::nullopt;
		}
	}

	std::string ThemeJson(const ThemeColours& a_colours)
	{
		nlohmann::ordered_json j;
		j["name"] = a_colours.name;
		j["dark"] = a_colours.dark;
		auto& cs = j["colours"] = nlohmann::ordered_json::object();
		for (const auto& k : kKeys) {
			cs[k.name] = std::format("#{:06x}", a_colours.*(k.field) & 0xFFFFFFu);
		}
		return j.dump(1, '\t') + "\n";
	}

	bool SaveThemeFile(const ThemeColours& a_colours, const fs::path& a_path)
	{
		std::error_code ec;
		fs::create_directories(a_path.parent_path(), ec);
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		if (!out) {
			return false;
		}
		out << ThemeJson(a_colours);
		return static_cast<bool>(out);
	}

	std::vector<ThemeColours> LoadUserThemes()
	{
		std::vector<ThemeColours> out;
		auto taken = [&](const std::string& a_name) {
			const auto same = [&](const ThemeColours& a_t) { return core::IEquals(a_t.name, a_name); };
			return std::ranges::any_of(BuiltInThemes(), same) || std::ranges::any_of(out, same);
		};
		for (const fs::path& dir : { UserThemesDir(), ExeThemesDir() }) {
			std::error_code ec;
			if (dir.empty() || !fs::is_directory(dir, ec)) {
				continue;
			}
			std::vector<fs::path> files;
			for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
				if (it->is_regular_file(ec) && IEndsWith(PathToUtf8(it->path()), ".json")) {
					files.push_back(it->path());
				}
			}
			std::ranges::sort(files);
			for (const auto& f : files) {
				const auto text = ReadTextFile(f);
				auto       theme = text ? ParseTheme(*text) : std::nullopt;
				if (!theme || taken(theme->name)) {
					continue;
				}
				theme->file = f;
				out.push_back(std::move(*theme));
			}
		}
		std::ranges::sort(out, [](const ThemeColours& a, const ThemeColours& b) { return core::ToLower(a.name) < core::ToLower(b.name); });
		return out;
	}

	const std::vector<ThemeColours>& AllThemes()
	{
		if (!g_scanned) {
			RescanThemes();
		}
		return Cache();
	}

	void RescanThemes()
	{
		g_scanned = true;
		auto& all = Cache();
		all = BuiltInThemes();
		auto user = LoadUserThemes();
		all.insert(all.end(), std::make_move_iterator(user.begin()), std::make_move_iterator(user.end()));
	}

	const ThemeColours* FindTheme(std::string_view a_name)
	{
		if (const auto* t = BuiltIn(a_name)) {
			return t;
		}
		for (const auto& t : AllThemes()) {
			if (core::IEquals(t.name, a_name)) {
				return &t;
			}
		}
		return nullptr;
	}
}
