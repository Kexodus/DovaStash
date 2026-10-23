#pragma once

// Press-to-bind for the Settings page's hotkeys: the page's button calls Begin(), the next key or button the
// player presses is caught here and handed back to the page (TakeResult), which applies and saves it.
//
// Input path: a Menu Framework input callback (SKSEMenuFramework::AddInputEvent). The framework runs it on the
// main thread for every input event, before the game and ImGui see them, and drops the events it returns
// true for. While waiting, every key / gamepad button press is kept from the page and the game (it never
// reaches our own hotkey sink either); the mouse's left and right buttons and wheel stay with the page, so
// clicking the button again cancels. The bound key's release is swallowed too.
//
// What the framework decides first (it handles these before any plugin callback):
// - Esc closes the whole window (no plugin can keep it open); closing cancels the wait.
// - Gamepad B steps back out of the page; leaving the page cancels the wait. B cannot be bound.
// - Its own open/close key (F1 / LB by default) would close the window: it is switched off
//   (SetHotkeyEnabled) while waiting and restored after, so it can be bound like any other key. The page then
//   warns that the key also opens the framework (Clash: "the SKSE Menu Framework key").
// The wait also ends when the page stops being drawn (another page, window closed) for half a second.

namespace dova::ui::panel::key_capture
{
	enum class Target : int
	{
		None = 0,
		Keyboard = 1,  // keyboard scan codes 1-255 (not Esc) and mouse buttons 3-8 (SKSE codes 258-263)
		Gamepad = 2    // gamepad buttons, SKSE codes 266-281 (not B)
	};

	// Main thread, once, with the Menu Framework loaded: registers the input and window callbacks.
	void Register();

	// Render thread. Starts waiting for a key for `a_target` (ends any other wait).
	void Begin(Target a_target);
	// Any thread. Stops waiting without binding anything.
	void Cancel();
	// What is being waited for (None when idle).
	[[nodiscard]] Target Waiting();
	// Render thread: the control that started the wait was drawn this frame (keeps the wait alive).
	void KeepAlive();
	// Render thread: the SKSE key code caught for `a_target`, once.
	[[nodiscard]] std::optional<std::int32_t> TakeResult(Target a_target);
	// Any thread: what else the key bound to `a_target` does (this session's capture, or RefreshClashes): the game's
	// gameplay control ("Activate") and/or the SKSE Menu Framework's own toggle key (read from its INI); "" when
	// nothing. The key is bound anyway; the page says so.
	[[nodiscard]] std::string Clash(Target a_target, std::int32_t a_code);
	// Main thread, AutoTest: a press of SKSE key code `a_code` while waiting, through the same path as a real key
	// down (Esc / gamepad B cancel; the other device's keys are ignored). True when it ended the wait.
	bool FeedForTest(std::int32_t a_code);
	// Whether the SKSE Menu Framework's own open/close key is on (off only while a wait runs).
	[[nodiscard]] bool FrameworkKeyEnabled();
	// Main thread (ControlMap): works out the clashes of the hotkeys as set now ([Input] iHotkey, iGamepadHotkey),
	// so the warning also shows after a restart. Called with every global snapshot.
	void RefreshClashes(std::int32_t a_hotkey, std::int32_t a_gamepad);
}
