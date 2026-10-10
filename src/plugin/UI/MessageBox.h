#pragma once

// Native message box (MessageBoxMenu) with a C++ callback. The callback is invoked on the main thread
// (via the SKSE task queue) with the 0-based index of the pressed button; Esc/cancel maps to the last
// button. Button count per box should stay small (the vanilla layout wraps badly above ~8).

namespace dova::ui
{
	using MessageBoxCallback = std::function<void(std::size_t a_button)>;

	bool ShowMessageBox(std::string_view a_body, const std::vector<std::string>& a_buttons, MessageBoxCallback a_callback);

	// HUD notification (top-left), one line.
	void ShowNotification(std::string_view a_message);
}
