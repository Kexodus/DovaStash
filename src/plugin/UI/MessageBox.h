#pragma once

// Native message box (MessageBoxMenu) with a C++ callback. The callback is invoked on the main thread
// (via the SKSE task queue) with the 0-based index of the pressed button; Esc/cancel maps to the last
// button. Button count per box should stay small (the vanilla layout wraps badly above ~8).

namespace dova::ui
{
	using MessageBoxCallback = std::function<void(std::size_t a_button)>;

	bool ShowMessageBox(std::string_view a_body, const std::vector<std::string>& a_buttons, MessageBoxCallback a_callback);
	// The buttons of the last box ShowMessageBox queued (AutoTest's menuEntries check, while that box is open).
	[[nodiscard]] std::vector<std::string> LastMessageBoxButtons();

	// One choice of a message-box menu.
	struct MenuOption
	{
		std::string           label;
		std::function<void()> action;  // null: closes the box
	};
	inline constexpr std::size_t kOptionsPerPage = 5;  // + More... / Back / Cancel = at most 8 buttons
	// Every message-box menu pages the same way: options[page * 5 ...], then More..., Back (the previous page; on
	// the first page `a_onBack`, if any) and Cancel. "(page 2/4)" is added to the body when there is more than one.
	void ShowOptions(std::string a_body, std::vector<MenuOption> a_options, std::function<void()> a_onBack = {}, std::size_t a_page = 0);

	// HUD notification (top-left), one line.
	void ShowNotification(std::string_view a_message);
}
