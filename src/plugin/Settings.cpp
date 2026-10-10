#include "Settings.h"

#include <SimpleIni.h>

namespace dova
{
	namespace
	{
		std::int32_t GetInt(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, std::int32_t a_default)
		{
			return static_cast<std::int32_t>(a_ini.GetLongValue(a_section, a_key, a_default));
		}

		bool GetBool(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, bool a_default)
		{
			return a_ini.GetBoolValue(a_section, a_key, a_default);
		}

		// Keyboard scan codes are 1..255 (0xFF), mouse buttons 256..265, gamepad 266..281 (SKSE input
		// code space). Anything else is treated as "use the default".
		bool IsValidKeyCode(std::int32_t a_code) noexcept
		{
			return a_code > 0 && a_code <= 281;
		}
	}

	Settings& Settings::GetSingleton() noexcept
	{
		static Settings singleton;
		return singleton;
	}

	void Settings::Load()
	{
		const std::filesystem::path path{ kPath };

		CSimpleIniA ini;
		ini.SetUnicode();
		const auto rc = ini.LoadFile(path.string().c_str());
		if (rc < 0) {
			logger::info("Settings: {} not found or unreadable (code {}), using defaults", kPath, static_cast<int>(rc));
		}

		const General   defGeneral{};
		const Input     defInput{};
		const Templates defTemplates{};
		const Sorting   defSorting{};
		const UIOptions defUI{};

		_general.logLevel = GetInt(ini, "General", "iLogLevel", defGeneral.logLevel);

		_input.hotkey = GetInt(ini, "Input", "iHotkey", defInput.hotkey);
		if (!IsValidKeyCode(_input.hotkey)) {
			logger::warn("Settings: iHotkey={} is not a valid key code, using default {}", _input.hotkey, defInput.hotkey);
			_input.hotkey = defInput.hotkey;
		}

		_input.gamepadHotkey = GetInt(ini, "Input", "iGamepadHotkey", defInput.gamepadHotkey);
		if (_input.gamepadHotkey != kNoKey && (_input.gamepadHotkey < 266 || _input.gamepadHotkey > 281)) {
			logger::warn("Settings: iGamepadHotkey={} is not a gamepad key code (266-281), disabling", _input.gamepadHotkey);
			_input.gamepadHotkey = kNoKey;
		}

		_templates.autoEnable = GetBool(ini, "General", "bAutoEnableTemplates", defTemplates.autoEnable);

		_sorting.distributeOnMasterClose = GetBool(ini, "Sorting", "bDistributeOnMasterClose", defSorting.distributeOnMasterClose);
		_sorting.learn = GetBool(ini, "Sorting", "bLearn", defSorting.learn);

		_ui.mode = GetInt(ini, "UI", "iMode", defUI.mode);
		if (_ui.mode < 0 || _ui.mode > 2) {
			logger::warn("Settings: iMode={} is not 0, 1 or 2, using default {}", _ui.mode, defUI.mode);
			_ui.mode = defUI.mode;
		}

		logger::info("Settings: iLogLevel={} iHotkey={} iGamepadHotkey={} bAutoEnableTemplates={} bDistributeOnMasterClose={} bLearn={} iMode={}",
			_general.logLevel, _input.hotkey, _input.gamepadHotkey, _templates.autoEnable, _sorting.distributeOnMasterClose, _sorting.learn, _ui.mode);
	}
}
