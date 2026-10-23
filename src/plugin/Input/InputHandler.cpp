#include "Input/InputHandler.h"

#include "Controller.h"
#include "Events/EventSinks.h"
#include "Game/FormUtil.h"
#include "Settings.h"

namespace dova::input
{
	namespace
	{
		using Control = RE::BSEventNotifyControl;
		using Pad = RE::BSWin32GamepadDevice::Key;

		std::optional<std::int32_t> GamepadCode(std::uint32_t a_idCode)
		{
			// Same mapping SKSE uses for its gamepad key codes (266 = DPad Up ... 281 = RT).
			switch (a_idCode) {
			case Pad::kUp:
				return 266;
			case Pad::kDown:
				return 267;
			case Pad::kLeft:
				return 268;
			case Pad::kRight:
				return 269;
			case Pad::kStart:
				return 270;
			case Pad::kBack:
				return 271;
			case Pad::kLeftThumb:
				return 272;
			case Pad::kRightThumb:
				return 273;
			case Pad::kLeftShoulder:
				return 274;
			case Pad::kRightShoulder:
				return 275;
			case Pad::kA:
				return 276;
			case Pad::kB:
				return 277;
			case Pad::kX:
				return 278;
			case Pad::kY:
				return 279;
			case Pad::kLeftTrigger:
				return 280;
			case Pad::kRightTrigger:
				return 281;
			default:
				return std::nullopt;
			}
		}

		bool GameplayAcceptsHotkey()
		{
			if (!game::Player() || !game::Player()->GetParentCell()) {
				return false;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused()) {
				return false;
			}
			for (const auto& menu : ui->menuStack) {
				if (menu && menu->menuFlags.any(RE::IMenu::Flag::kPausesGame, RE::IMenu::Flag::kUsesCursor, RE::IMenu::Flag::kModal)) {
					return false;
				}
			}
			if (ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
				return false;
			}
			if (const auto* controls = RE::ControlMap::GetSingleton(); controls && controls->GetRuntimeData().textEntryCount > 0) {
				return false;
			}
			return true;
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			Control ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_events) {
					return Control::kContinue;
				}
				const auto& input = Settings::GetSingleton().GetInput();
				for (auto* event = *a_events; event; event = event->next) {
					if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
						continue;
					}
					const auto* button = event->AsButtonEvent();
					if (!button || !button->IsDown()) {
						continue;
					}
					const auto code = ToSKSEKeyCode(event->GetDevice(), button->GetIDCode());
					if (!code || (*code != input.hotkey && *code != input.gamepadHotkey)) {
						continue;
					}
					if (!GameplayAcceptsHotkey()) {
						logger::debug("Hotkey ignored (menu open / paused / text input)");
						break;
					}
					const auto target = events::CrosshairTarget();
					SKSE::GetTaskInterface()->AddTask([target]() { Controller::Get().OnHotkey(target); });
					break;
				}
				return Control::kContinue;
			}
		};

		InputSink g_inputSink;
	}

	std::optional<std::int32_t> ToSKSEKeyCode(RE::INPUT_DEVICE a_device, std::uint32_t a_idCode)
	{
		switch (a_device) {
		case RE::INPUT_DEVICE::kKeyboard:
			return static_cast<std::int32_t>(a_idCode);
		case RE::INPUT_DEVICE::kMouse:
			return 256 + static_cast<std::int32_t>(a_idCode);
		case RE::INPUT_DEVICE::kGamepad:
			return GamepadCode(a_idCode);
		default:
			return std::nullopt;
		}
	}

	std::span<const KeyChoice> KeyboardChoices()
	{
		// DirectInput scan codes. Letters/digits/F-keys/navigation first, then the rest. Keys missing here still
		// bind (Settings > Hotkey takes any key pressed); they show as "key 123".
		static constexpr std::array kKeys{
			KeyChoice{ 0x10, "Q" }, KeyChoice{ 0x11, "W" }, KeyChoice{ 0x12, "E" }, KeyChoice{ 0x13, "R" }, KeyChoice{ 0x14, "T" },
			KeyChoice{ 0x15, "Y" }, KeyChoice{ 0x16, "U" }, KeyChoice{ 0x17, "I" }, KeyChoice{ 0x18, "O" }, KeyChoice{ 0x19, "P" },
			KeyChoice{ 0x1E, "A" }, KeyChoice{ 0x1F, "S" }, KeyChoice{ 0x20, "D" }, KeyChoice{ 0x21, "F" }, KeyChoice{ 0x22, "G" },
			KeyChoice{ 0x23, "H" }, KeyChoice{ 0x24, "J" }, KeyChoice{ 0x25, "K" }, KeyChoice{ 0x26, "L" }, KeyChoice{ 0x2C, "Z" },
			KeyChoice{ 0x2D, "X" }, KeyChoice{ 0x2E, "C" }, KeyChoice{ 0x2F, "V" }, KeyChoice{ 0x30, "B" }, KeyChoice{ 0x31, "N" },
			KeyChoice{ 0x32, "M" }, KeyChoice{ 0x02, "1" }, KeyChoice{ 0x03, "2" }, KeyChoice{ 0x04, "3" }, KeyChoice{ 0x05, "4" },
			KeyChoice{ 0x06, "5" }, KeyChoice{ 0x07, "6" }, KeyChoice{ 0x08, "7" }, KeyChoice{ 0x09, "8" }, KeyChoice{ 0x0A, "9" },
			KeyChoice{ 0x0B, "0" }, KeyChoice{ 0x0C, "-" }, KeyChoice{ 0x0D, "=" }, KeyChoice{ 0x1A, "[" }, KeyChoice{ 0x1B, "]" },
			KeyChoice{ 0x27, ";" }, KeyChoice{ 0x28, "'" }, KeyChoice{ 0x2B, "\\" }, KeyChoice{ 0x33, "," }, KeyChoice{ 0x34, "." },
			KeyChoice{ 0x35, "/" }, KeyChoice{ 0x3B, "F1" }, KeyChoice{ 0x3C, "F2" }, KeyChoice{ 0x3D, "F3" }, KeyChoice{ 0x3E, "F4" },
			KeyChoice{ 0x3F, "F5" }, KeyChoice{ 0x40, "F6" }, KeyChoice{ 0x41, "F7" }, KeyChoice{ 0x42, "F8" }, KeyChoice{ 0x43, "F9" },
			KeyChoice{ 0x44, "F10" }, KeyChoice{ 0x57, "F11" }, KeyChoice{ 0x58, "F12" }, KeyChoice{ 0xC7, "Home" }, KeyChoice{ 0xCF, "End" },
			KeyChoice{ 0xC9, "Page Up" }, KeyChoice{ 0xD1, "Page Down" }, KeyChoice{ 0xD2, "Insert" }, KeyChoice{ 0xD3, "Delete" },
			KeyChoice{ 0xC8, "Up" }, KeyChoice{ 0xD0, "Down" }, KeyChoice{ 0xCB, "Left" }, KeyChoice{ 0xCD, "Right" },
			KeyChoice{ 0x47, "Num 7" }, KeyChoice{ 0x48, "Num 8" }, KeyChoice{ 0x49, "Num 9" }, KeyChoice{ 0x4B, "Num 4" },
			KeyChoice{ 0x4C, "Num 5" }, KeyChoice{ 0x4D, "Num 6" }, KeyChoice{ 0x4F, "Num 1" }, KeyChoice{ 0x50, "Num 2" },
			KeyChoice{ 0x51, "Num 3" }, KeyChoice{ 0x52, "Num 0" }, KeyChoice{ 0x4A, "Num -" }, KeyChoice{ 0x4E, "Num +" },
			KeyChoice{ 0x37, "Num *" }, KeyChoice{ 0xB5, "Num /" }, KeyChoice{ 0x9C, "Num Enter" }, KeyChoice{ 0x53, "Num ." },
			KeyChoice{ 0x0F, "Tab" }, KeyChoice{ 0x3A, "Caps Lock" }, KeyChoice{ 0x2A, "Left Shift" }, KeyChoice{ 0x36, "Right Shift" },
			KeyChoice{ 0x1D, "Left Ctrl" }, KeyChoice{ 0x9D, "Right Ctrl" }, KeyChoice{ 0x38, "Left Alt" }, KeyChoice{ 0xB8, "Right Alt" },
			KeyChoice{ 0x39, "Space" }, KeyChoice{ 0x1C, "Enter" }, KeyChoice{ 0x0E, "Backspace" }, KeyChoice{ 0x29, "~" },
			KeyChoice{ 0x45, "Num Lock" }, KeyChoice{ 0x46, "Scroll Lock" }, KeyChoice{ 0xC5, "Pause" }, KeyChoice{ 0xB7, "Print Screen" },
			KeyChoice{ 258, "Middle mouse" }, KeyChoice{ 259, "Mouse 4" }, KeyChoice{ 260, "Mouse 5" }, KeyChoice{ 261, "Mouse 6" },
			KeyChoice{ 262, "Mouse 7" }, KeyChoice{ 263, "Mouse 8" },
		};
		return kKeys;
	}

	std::span<const KeyChoice> GamepadChoices()
	{
		static constexpr std::array kButtons{
			KeyChoice{ 266, "D-pad Up" }, KeyChoice{ 267, "D-pad Down" }, KeyChoice{ 268, "D-pad Left" }, KeyChoice{ 269, "D-pad Right" },
			KeyChoice{ 270, "Start" }, KeyChoice{ 271, "Back" }, KeyChoice{ 272, "Left stick" }, KeyChoice{ 273, "Right stick" },
			KeyChoice{ 274, "LB" }, KeyChoice{ 275, "RB" }, KeyChoice{ 276, "A" }, KeyChoice{ 277, "B" }, KeyChoice{ 278, "X" },
			KeyChoice{ 279, "Y" }, KeyChoice{ 280, "LT" }, KeyChoice{ 281, "RT" },
		};
		return kButtons;
	}

	std::string KeyName(std::int32_t a_code)
	{
		for (const auto& k : KeyboardChoices()) {
			if (k.code == a_code) {
				return std::string(k.name);
			}
		}
		for (const auto& k : GamepadChoices()) {
			if (k.code == a_code) {
				return std::format("Gamepad {}", k.name);
			}
		}
		return std::format("key {}", a_code);
	}

	void RegisterInputSink()
	{
		if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) {
			manager->AddEventSink(&g_inputSink);
			logger::info("Input: hotkey sink registered (key {}, gamepad {})", Settings::GetSingleton().GetInput().hotkey,
				Settings::GetSingleton().GetInput().gamepadHotkey);
		} else {
			logger::error("Input: BSInputDeviceManager unavailable");
		}
	}
}
