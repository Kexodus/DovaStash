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
