#pragma once

namespace dova
{
	// User settings from Data/SKSE/Plugins/DovaStash.ini. Every key has an in-code default, so a
	// missing file or key is never an error. Read once at plugin load; main thread only.
	class Settings
	{
	public:
		static constexpr auto kPath = "Data/SKSE/Plugins/DovaStash.ini"sv;

		// Sentinel for "no gamepad hotkey".
		static constexpr std::int32_t kNoKey = -1;

		struct General
		{
			// spdlog level: 0 trace, 1 debug, 2 info, 3 warn, 4 error, 5 critical, 6 off.
			std::int32_t logLevel{ 2 };
		};

		struct Input
		{
			// DirectInput scan code. Default 0x25 (37) = K.
			// Vanilla Skyrim's PC control map (Interface/Controls/PC/controlmap.txt) binds
			// W A S D, E, R, Q, Tab, T, I, P, J, M, F, Z, C, Shift, Ctrl, Alt, Space, CapsLock,
			// '/', F5, F9, 1-8 (favorites), Esc and '~' (console). K is unbound there, is not one
			// of the letters other mods most often take by default (G, H, N, V), and a scan of
			// the SKSE plugin INIs in a ~2000-mod MO2 list found no other binding of code 37.
			std::int32_t hotkey{ 0x25 };

			// SKSE gamepad key code (266-281), or kNoKey. Disabled by default because every
			// gamepad button already has a vanilla action; a combo/hold scheme arrives with the
			// input sink in M1.
			std::int32_t gamepadHotkey{ kNoKey };
		};

		struct Templates
		{
			// DESIGN §4.1: auto-enable a matching template on first entry (off by default).
			bool autoEnable{ false };
		};

		struct Sorting
		{
			// DESIGN §8.1: closing the master container distributes its contents.
			bool distributeOnMasterClose{ true };
			// DESIGN §8.1 Learn: items you put into a managed container by hand teach their destination.
			bool learn{ true };
		};

		struct UIOptions
		{
			// DESIGN §10.1: 0 = auto (SKSE Menu Framework panel when installed and not VR, else native
			// message boxes), 1 = always message boxes, 2 = Menu Framework whenever it is loaded.
			std::int32_t mode{ 0 };
		};

		[[nodiscard]] static Settings& GetSingleton() noexcept;

		// Loads kPath (relative to the game's working directory). Logs what it read.
		void Load();

		[[nodiscard]] const General&   GetGeneral() const noexcept { return _general; }
		[[nodiscard]] const Input&     GetInput() const noexcept { return _input; }
		[[nodiscard]] const Templates& GetTemplates() const noexcept { return _templates; }
		[[nodiscard]] const Sorting&   GetSorting() const noexcept { return _sorting; }
		[[nodiscard]] const UIOptions& GetUI() const noexcept { return _ui; }

	private:
		Settings() = default;

		General   _general;
		Input     _input;
		Templates _templates;
		Sorting   _sorting;
		UIOptions _ui;
	};
}
