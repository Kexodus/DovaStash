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

		float GetFloat(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, float a_default)
		{
			return static_cast<float>(a_ini.GetDoubleValue(a_section, a_key, a_default));
		}

		// One INI key: section, name, and how to read/write/compare it in Settings::Values. The single
		// table keeps Load, Save and change detection in step.
		struct Key
		{
			const char* section;
			const char* name;
			std::function<void(const CSimpleIniA&, Settings::Values&, const Settings::Values&)> read;
			std::function<std::string(const Settings::Values&)>                                 write;
		};

		template <class T>
		std::string Str(T a_value)
		{
			if constexpr (std::is_same_v<T, bool>) {
				return a_value ? "true" : "false";
			} else {
				return std::format("{}", a_value);
			}
		}

#define DOVA_INT(SECTION, NAME, FIELD)                                                                                                   \
	Key { SECTION, NAME, [](const CSimpleIniA& i, Settings::Values& v, const Settings::Values& d) { v.FIELD = GetInt(i, SECTION, NAME, d.FIELD); }, \
		[](const Settings::Values& v) { return Str(v.FIELD); } }
#define DOVA_BOOL(SECTION, NAME, FIELD)                                                                                                   \
	Key { SECTION, NAME, [](const CSimpleIniA& i, Settings::Values& v, const Settings::Values& d) { v.FIELD = GetBool(i, SECTION, NAME, d.FIELD); }, \
		[](const Settings::Values& v) { return Str(v.FIELD); } }
#define DOVA_FLOAT(SECTION, NAME, FIELD)                                                                                                    \
	Key { SECTION, NAME, [](const CSimpleIniA& i, Settings::Values& v, const Settings::Values& d) { v.FIELD = GetFloat(i, SECTION, NAME, d.FIELD); }, \
		[](const Settings::Values& v) { return Str(v.FIELD); } }

		const std::vector<Key>& Keys()
		{
			static const std::vector<Key> keys{
				DOVA_INT("General", "iLogLevel", general.logLevel),
				DOVA_BOOL("General", "bAutoEnableTemplates", templates.autoEnable),
				DOVA_INT("Input", "iHotkey", input.hotkey),
				DOVA_INT("Input", "iGamepadHotkey", input.gamepadHotkey),
				DOVA_BOOL("Sorting", "bDistributeOnMasterClose", sorting.distributeOnMasterClose),
				DOVA_BOOL("Sorting", "bSortOnArrival", sorting.sortOnArrival),
				DOVA_BOOL("Sorting", "bLearn", sorting.learn),
				DOVA_INT("UI", "iMode", ui.mode),
				DOVA_BOOL("UI", "bCenterMenus", ui.centerMenus),
				DOVA_BOOL("Integrations", "bQuickLoot", integrations.quickLoot),
				DOVA_BOOL("Integrations", "bQuickLootItemTags", integrations.quickLootItemTags),
				DOVA_BOOL("Integrations", "bQuickLootTitleIcon", integrations.quickLootTitleIcon),
				DOVA_BOOL("Integrations", "bActivatePrompt", integrations.activatePrompt),
				DOVA_BOOL("Overlay", "bEnabled", overlay.enabled),
				DOVA_FLOAT("Overlay", "fRadius", overlay.radius),
				DOVA_INT("Overlay", "iMaxIcons", overlay.maxIcons),
				DOVA_FLOAT("Overlay", "fIconSize", overlay.iconSize),
				DOVA_BOOL("Overlay", "bRequireLineOfSight", overlay.requireLineOfSight),
			};
			return keys;
		}

#undef DOVA_INT
#undef DOVA_BOOL
#undef DOVA_FLOAT
	}

	Settings& Settings::GetSingleton() noexcept
	{
		static Settings singleton;
		return singleton;
	}

	void Settings::Validate(Values& a_values)
	{
		const Values def{};
		auto&        v = a_values;
		if (!IsValidKeyCode(v.input.hotkey)) {
			logger::warn("Settings: iHotkey={} is not a valid key code, using default {}", v.input.hotkey, def.input.hotkey);
			v.input.hotkey = def.input.hotkey;
		}
		if (v.input.gamepadHotkey != kNoKey && (v.input.gamepadHotkey < 266 || v.input.gamepadHotkey > 281)) {
			logger::warn("Settings: iGamepadHotkey={} is not a gamepad key code (266-281), disabling", v.input.gamepadHotkey);
			v.input.gamepadHotkey = kNoKey;
		}
		if (v.general.logLevel < 0 || v.general.logLevel > 6) {
			logger::warn("Settings: iLogLevel={} is not 0-6, using default {}", v.general.logLevel, def.general.logLevel);
			v.general.logLevel = def.general.logLevel;
		}
		if (v.ui.mode < 0 || v.ui.mode > 3) {
			logger::warn("Settings: iMode={} is not 0-3, using default {}", v.ui.mode, def.ui.mode);
			v.ui.mode = def.ui.mode;
		}
		v.overlay.radius = std::clamp(v.overlay.radius, 100.0f, 10000.0f);
		v.overlay.maxIcons = std::clamp(v.overlay.maxIcons, 1, 32);
		v.overlay.iconSize = std::clamp(v.overlay.iconSize, 8.0f, 256.0f);
	}

	void Settings::Log(std::string_view a_prefix) const
	{
		std::string line;
		for (const auto& key : Keys()) {
			line += std::format("{}{}={}", line.empty() ? "" : " ", key.name, key.write(_v));
		}
		logger::info("{}: {}", a_prefix, line);
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

		const Values def{};
		Values       values{};
		for (const auto& key : Keys()) {
			key.read(ini, values, def);
		}
		Validate(values);
		_v = values;
		Log("Settings");
	}

	Settings::Values Settings::Get() const
	{
		return _v;
	}

	bool Settings::Save() const
	{
		const std::filesystem::path path{ kPath };
		CSimpleIniA                 ini;
		ini.SetUnicode();
		ini.LoadFile(path.string().c_str());  // keeps the file's comments and any keys we don't know
		for (const auto& key : Keys()) {
			ini.SetValue(key.section, key.name, key.write(_v).c_str());
		}
		const auto rc = ini.SaveFile(path.string().c_str());
		if (rc < 0) {
			logger::error("Settings: could not write {} (code {})", kPath, static_cast<int>(rc));
			return false;
		}
		return true;
	}

	std::vector<std::string> Settings::Update(Values a_values)
	{
		Validate(a_values);
		std::vector<std::string> changed;
		for (const auto& key : Keys()) {
			const auto before = key.write(_v);
			const auto after = key.write(a_values);
			if (before != after) {
				changed.push_back(std::format("{} {} -> {}", key.name, before, after));
			}
		}
		if (changed.empty()) {
			return changed;
		}
		_v = a_values;
		for (const auto& c : changed) {
			logger::info("Settings: {}", c);
		}
		if (Save()) {
			logger::info("Settings: saved to {}", kPath);
		}
		return changed;
	}
}
