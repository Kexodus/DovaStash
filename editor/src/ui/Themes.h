#pragma once

// Colour themes (docs/EDITOR-UX.md §3.1): the Dova Stash tokens are the default; the other built-ins are
// palettes inspired by the VCL styles xEdit offers (colour cues only, no style art), split into dark and light.
// The player's own themes are JSON files in %LOCALAPPDATA%\DovaStashEditor\Themes and <exe dir>\Themes:
//   { "name": "Carbon", "dark": true, "colours": { "bg0": "#141414", "text": "#e6e6e6", ... } }
// A colour a file leaves out comes from Dova Stash (dark) or Light (light). Theme.cpp turns one into the tokens.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dova::editor::ui::w
{
	struct ThemeColours
	{
		std::string   name;
		bool          dark{ true };
		std::uint32_t bg0{}, bg1{}, bg2{}, bg3{}, line{};          // 0xRRGGBB
		std::uint32_t text{}, muted{}, disabled{}, accent{}, accentDim{}, select{};
		std::uint32_t ok{}, warn{}, error{}, info{};
		bool                  user{ false };  // read from a file (Setup's "Yours")
		std::filesystem::path file;           // that file
	};

	// Dova Stash first (the default), then the dark themes, then the light ones.
	[[nodiscard]] const std::vector<ThemeColours>& BuiltInThemes();
	// The player's files, %LOCALAPPDATA% first, then next to the exe, by name. Bad files are skipped; so is a file
	// whose name is already taken (a built-in or an earlier file).
	[[nodiscard]] std::vector<ThemeColours> LoadUserThemes();
	// The built-ins and the player's files, read once; RescanThemes reads the folders again (Setup's list opening).
	[[nodiscard]] const std::vector<ThemeColours>& AllThemes();
	void                                           RescanThemes();
	// By name, ignoring case; nullptr when there is none.
	[[nodiscard]] const ThemeColours* FindTheme(std::string_view a_name);

	[[nodiscard]] std::filesystem::path       UserThemesDir();  // %LOCALAPPDATA%\DovaStashEditor\Themes
	[[nodiscard]] std::optional<ThemeColours> ParseTheme(std::string_view a_json);
	[[nodiscard]] std::string                 ThemeJson(const ThemeColours& a_colours);
	bool                                      SaveThemeFile(const ThemeColours& a_colours, const std::filesystem::path& a_path);
}
