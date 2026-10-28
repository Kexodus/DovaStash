#include "UI/Panel/KeyCapture.h"

#include "Input/InputHandler.h"
#include "UI/Panel/Strings.h"

#include <fstream>
#include <limits>
#include <mutex>

// The vendored header uses std::filesystem without including it and includes <windows.h>
// (WIN32_LEAN_AND_MEAN / NOMINMAX come from CMake). Keep it the last include of this file.
#include <filesystem>

#include <SKSEMenuFramework.h>

namespace dova::ui::panel::key_capture
{
	namespace
	{
		constexpr std::int32_t kNoResult = std::numeric_limits<std::int32_t>::min();
		constexpr std::int32_t kEscape = RE::BSKeyboardDevice::Key::kEscape;  // keyboard scan code
		constexpr std::int32_t kGamepadB = 277;                                // SKSE code (Input/InputHandler.cpp)
		constexpr std::int64_t kStaleMs = 500;                                 // the page has not been drawn for this long

		std::mutex                g_lock;  // Begin / Stop (render and main thread)
		std::atomic<int>          g_target{ static_cast<int>(Target::None) };
		std::atomic<std::int64_t> g_lastSeen{ 0 };
		std::atomic<std::int32_t> g_swallowUp{ kNoResult };  // the bound key, until its release went by
		Target                    g_resultFor{ Target::None };  // g_lock
		std::int32_t              g_result{ kNoResult };        // g_lock
		bool                      g_toggleOff{ false };         // g_lock: we switched the framework's own key off
		bool                      g_toggleWas{ true };          // g_lock: its state before
		// g_lock: the game's gameplay control a key bound in this session also has ("Activate"), per target.
		std::map<Target, std::pair<std::int32_t, std::string>> g_clash;

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		Target Current()
		{
			return static_cast<Target>(g_target.load());
		}

		// Ends the wait (a_code: the key caught, or kNoResult) and gives the framework its key back.
		void Stop(std::int32_t a_code, std::string_view a_why)
		{
			std::scoped_lock lock(g_lock);
			const auto target = Current();
			if (target == Target::None) {
				return;
			}
			g_target = static_cast<int>(Target::None);
			if (a_code != kNoResult) {
				g_resultFor = target;
				g_result = a_code;
			}
			if (g_toggleOff) {
				SKSEMenuFramework::SetHotkeyEnabled(g_toggleWas);
				g_toggleOff = false;
			}
			logger::debug("Hotkey capture: {} ({})", a_why, a_code == kNoResult ? "nothing bound"s : input::KeyName(a_code));
		}

		bool Stale()
		{
			return NowMs() - g_lastSeen.load() > kStaleMs;
		}

		// The game's gameplay control on SKSE key code `a_code` ("Activate"), "" when none (main thread: ControlMap).
		std::string GameControlOf(std::int32_t a_code)
		{
			auto* controls = RE::ControlMap::GetSingleton();
			if (!controls) {
				return {};
			}
			if (a_code >= 1 && a_code <= 255) {
				return std::string(controls->GetUserEventName(static_cast<std::uint32_t>(a_code), RE::INPUT_DEVICE::kKeyboard));
			}
			if (a_code >= 256 && a_code <= 265) {
				return std::string(controls->GetUserEventName(static_cast<std::uint32_t>(a_code - 256), RE::INPUT_DEVICE::kMouse));
			}
			if (a_code >= 266 && a_code <= 281) {
				// Gamepad buttons are bit masks (BSWin32GamepadDevice::Key), the triggers 0x9 and 0xA.
				for (std::uint32_t i = 0; i < 18; ++i) {
					const auto id = i < 16 ? 1u << i : 0x9u + (i - 16);
					if (input::ToSKSEKeyCode(RE::INPUT_DEVICE::kGamepad, id) == a_code) {
						return std::string(controls->GetUserEventName(id, RE::INPUT_DEVICE::kGamepad));
					}
				}
			}
			return {};
		}

		// The SKSE Menu Framework's own open/close keys ([General] ToggleKey / ToggleKeyGamePad in
		// Data/SKSE/Plugins/SKSEMenuFramework.ini; F1 and LB when unreadable; none when its mode is OFF). Read once:
		// the framework reads its INI at startup too.
		struct FrameworkKeys
		{
			std::int32_t keyboard{ 0x3B };  // F1
			std::int32_t gamepad{ 274 };    // LB
		};

		const FrameworkKeys& FrameworkToggleKeys()
		{
			static const FrameworkKeys keys = []() {
				FrameworkKeys out;
				std::ifstream file("Data/SKSE/Plugins/SKSEMenuFramework.ini");
				if (!file) {
					logger::debug("Hotkey capture: SKSEMenuFramework.ini not readable; its keys are taken as F1 and LB");
					return out;
				}
				const auto trim = [](std::string a_text) {
					const auto first = a_text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
					const auto last = a_text.find_last_not_of(" \t\r\n");
					return first == std::string::npos ? std::string{} : a_text.substr(first, last - first + 1);
				};
				const auto codeOf = [](std::string_view a_name, std::span<const input::KeyChoice> a_choices) -> std::optional<std::int32_t> {
					for (const auto& c : a_choices) {
						if (core::IEquals(c.name, a_name)) {
							return c.code;
						}
					}
					return std::nullopt;
				};
				std::string line;
				std::string section;
				while (std::getline(file, line)) {
					line = trim(line);
					if (line.starts_with('[')) {
						section = line;
						continue;
					}
					const auto eq = line.find('=');
					if (section != "[General]" || line.starts_with(';') || eq == std::string::npos) {
						continue;
					}
					const auto key = trim(line.substr(0, eq));
					const auto value = trim(line.substr(eq + 1));
					if (core::IEquals(key, "ToggleKey")) {
						out.keyboard = codeOf(value, input::KeyboardChoices()).value_or(out.keyboard);
					} else if (core::IEquals(key, "ToggleKeyGamePad")) {
						out.gamepad = codeOf(value, input::GamepadChoices()).value_or(out.gamepad);
					} else if (core::IEquals(key, "ToggleMode") && core::IEquals(value, "OFF")) {
						out.keyboard = 0;
					} else if (core::IEquals(key, "ToggleModeGamePad") && core::IEquals(value, "OFF")) {
						out.gamepad = 0;
					}
				}
				logger::debug("Hotkey capture: the SKSE Menu Framework opens with {} and {}", out.keyboard ? input::KeyName(out.keyboard) : "nothing"s,
					out.gamepad ? input::KeyName(out.gamepad) : "nothing"s);
				return out;
			}();
			return keys;
		}

		// What else `a_code` does: the game control, and the framework's own key (bound after the wait, one press
		// would open both windows). "Activate", "the SKSE Menu Framework key", "Left Attack/Block and the SKSE Menu
		// Framework key", or "".
		std::string ClashOf(std::int32_t a_code)
		{
			auto       clash = GameControlOf(a_code);
			const auto keys = FrameworkToggleKeys();
			if (a_code != 0 && (a_code == keys.keyboard || a_code == keys.gamepad)) {
				clash += clash.empty() ? text::kFrameworkKey : text::kFrameworkKeyAlso;
			}
			return clash;
		}

		bool Accepts(Target a_target, std::int32_t a_code)
		{
			switch (a_target) {
			case Target::Keyboard:
				return (a_code >= 1 && a_code <= 255 && a_code != kEscape) || (a_code >= 258 && a_code <= 263);
			case Target::Gamepad:
				return a_code >= 266 && a_code <= 281 && a_code != kGamepadB;
			default:
				return false;
			}
		}

		// A key press the wait accepts: bound anyway, but the page says what else the key does (main thread:
		// ControlMap). Its release is swallowed.
		void Bind(Target a_target, std::int32_t a_code)
		{
			{
				std::scoped_lock lock(g_lock);
				g_clash[a_target] = { a_code, ClashOf(a_code) };
			}
			g_swallowUp = a_code;
			Stop(a_code, "bound");
		}

		// Main thread, every input event (only while no ImGui item is active). True drops the event.
		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			const auto target = Current();
			const auto swallow = g_swallowUp.load();
			if (!a_event || (target == Target::None && swallow == kNoResult)) {
				return false;
			}
			const auto* button = a_event->AsButtonEvent();
			if (target == Target::None) {
				if (button && button->IsUp() && input::ToSKSEKeyCode(button->GetDevice(), button->GetIDCode()) == swallow) {
					g_swallowUp = kNoResult;
					return true;
				}
				return false;
			}
			if (Stale()) {
				Stop(kNoResult, "page closed");
				return false;
			}
			if (!button) {
				return a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kChar;  // typed text stays out of the page
			}
			const auto code = input::ToSKSEKeyCode(button->GetDevice(), button->GetIDCode());
			if (!code) {
				return false;
			}
			if (button->GetDevice() == RE::INPUT_DEVICE::kMouse && !Accepts(target, *code)) {
				return false;  // left / right click and the wheel stay with the page (clicking the button cancels)
			}
			if (!button->IsDown()) {
				return false;  // releases and holds of keys pressed before the wait reach the page as usual
			}
			if (*code == kEscape || *code == kGamepadB) {
				Stop(kNoResult, "cancelled");  // only seen if the framework ever stops handling them itself
				return true;
			}
			if (!Accepts(target, *code)) {
				return true;  // the other device's keys: ignored, and kept from the page
			}
			Bind(target, *code);
			return true;
		}

		void __stdcall OnEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (Current() == Target::None) {
				return;
			}
			if (a_type == SKSEMenuFramework::Model::kCloseMenu) {
				Stop(kNoResult, "window closed");
			} else if (a_type == SKSEMenuFramework::Model::kBeforeRender && Stale()) {
				Stop(kNoResult, "page closed");
			}
		}
	}

	void Register()
	{
		static bool done = false;  // main thread only
		if (done) {
			return;
		}
		done = true;
		static auto* input = SKSEMenuFramework::AddInputEvent(OnInput);  // live for the whole session
		static auto* event = SKSEMenuFramework::AddEvent(OnEvent, 0.0f);
		(void)input;
		(void)event;
	}

	void Begin(Target a_target)
	{
		if (a_target == Target::None) {
			Cancel();
			return;
		}
		std::scoped_lock lock(g_lock);
		g_lastSeen = NowMs();
		g_resultFor = Target::None;
		g_result = kNoResult;
		if (!g_toggleOff) {
			g_toggleWas = SKSEMenuFramework::IsHotkeyEnabled();
			SKSEMenuFramework::SetHotkeyEnabled(false);
			g_toggleOff = true;
		}
		g_target = static_cast<int>(a_target);
		logger::debug("Hotkey capture: waiting for a {}", a_target == Target::Gamepad ? "gamepad button" : "key");
	}

	void Cancel()
	{
		Stop(kNoResult, "cancelled");
	}

	Target Waiting()
	{
		return Current();
	}

	void KeepAlive()
	{
		g_lastSeen = NowMs();
	}

	void RefreshClashes(std::int32_t a_hotkey, std::int32_t a_gamepad)
	{
		if (!RE::ControlMap::GetSingleton()) {
			return;
		}
		auto keyboard = ClashOf(a_hotkey);
		auto gamepad = ClashOf(a_gamepad);
		std::scoped_lock lock(g_lock);
		g_clash[Target::Keyboard] = { a_hotkey, std::move(keyboard) };
		g_clash[Target::Gamepad] = { a_gamepad, std::move(gamepad) };
	}

	bool FrameworkKeyEnabled()
	{
		return SKSEMenuFramework::IsHotkeyEnabled();
	}

	bool FeedForTest(std::int32_t a_code)
	{
		const auto target = Current();
		if (target == Target::None) {
			return false;
		}
		g_lastSeen = NowMs();
		if (a_code == kEscape || a_code == kGamepadB) {
			Stop(kNoResult, "cancelled");
			return true;
		}
		if (!Accepts(target, a_code)) {
			return false;  // the other device's keys: the wait goes on
		}
		Bind(target, a_code);
		return true;
	}

	std::string Clash(Target a_target, std::int32_t a_code)
	{
		std::scoped_lock lock(g_lock);
		const auto       it = g_clash.find(a_target);
		return it != g_clash.end() && it->second.first == a_code ? it->second.second : std::string{};
	}

	std::optional<std::int32_t> TakeResult(Target a_target)
	{
		std::scoped_lock lock(g_lock);
		if (g_resultFor != a_target || g_result == kNoResult) {
			return std::nullopt;
		}
		const auto code = g_result;
		g_resultFor = Target::None;
		g_result = kNoResult;
		return code;
	}
}
