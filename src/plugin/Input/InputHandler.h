#pragma once

// Hotkey (DESIGN §11): a BSInputDeviceManager sink that watches for the configured keyboard key
// (iHotkey, DirectInput scan code) or gamepad button (iGamepadHotkey, SKSE code 266-281). On a press
// in normal gameplay it enqueues Controller::OnHotkey with the current crosshair target handle.
//
// Ignored while: the game is paused, any menu that pauses / uses the cursor / is modal is open
// (inventory, dialogue, console, message boxes...), text input is active, or the player is not loaded.
// Overlay menus that do neither (HUD, QuickLoot's LootMenu) do not block the hotkey.
//
// TODO(M4): hold-to-highlight managed containers (effect shaders, DESIGN §10.2).
// TODO(VR): sneak + activate on a container (vtable hook on activation, DESIGN §11) — VR has no
//           keyboard and no crosshair event; needs a VR tester.

namespace dova::input
{
	void RegisterInputSink();

	// SKSE key code space: keyboard = scan code, mouse = 256 + button, gamepad = 266 + index.
	[[nodiscard]] std::optional<std::int32_t> ToSKSEKeyCode(RE::INPUT_DEVICE a_device, std::uint32_t a_idCode);
}
