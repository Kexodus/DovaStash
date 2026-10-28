#include "UI/MessageBox.h"

namespace dova::ui
{
	namespace
	{
		class Callback final : public RE::IMessageBoxCallback
		{
		public:
			Callback(MessageBoxCallback a_fn, std::size_t a_buttonCount) :
				_fn(std::move(a_fn)),
				_buttonCount(a_buttonCount)
			{}

			void Run(std::uint8_t a_button) override
			{
				// buttonPressOffset is 0, so a_button is the index into buttonText.
				std::size_t index = a_button;
				if (index >= _buttonCount) {
					index = _buttonCount - 1;  // treat anything unexpected as cancel (last button)
				}
				auto fn = _fn;
				SKSE::GetTaskInterface()->AddTask([fn = std::move(fn), index]() {
					if (fn) {
						fn(index);
					}
				});
			}

		private:
			MessageBoxCallback _fn;
			std::size_t        _buttonCount;
		};

		std::mutex               g_lastLock;
		std::vector<std::string> g_lastButtons;  // the last box we queued (AutoTest reads its entries)
	}

	std::vector<std::string> LastMessageBoxButtons()
	{
		std::scoped_lock lock(g_lastLock);
		return g_lastButtons;
	}

	bool ShowMessageBox(std::string_view a_body, const std::vector<std::string>& a_buttons, MessageBoxCallback a_callback)
	{
		if (a_buttons.empty()) {
			return false;
		}
		auto* data = RE::UIMessageDataFactory::Create<RE::MessageBoxData>();
		if (!data) {
			logger::error("MessageBox: cannot create MessageBoxData");
			return false;
		}

		const std::string body{ a_body };
		data->bodyText = body.c_str();
		for (const auto& label : a_buttons) {
			data->buttonText.push_back(RE::BSString(label.c_str()));
		}
		data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(new Callback(std::move(a_callback), a_buttons.size()));
		{
			std::scoped_lock lock(g_lastLock);
			g_lastButtons = a_buttons;
		}
		data->warningType = 10;  // same value community helpers use for plugin message boxes
		data->buttonPressOffset = 0;
		data->cancelButtonIndex = static_cast<std::int32_t>(a_buttons.size() - 1);
		data->isCancellable = true;
		data->useHtml = false;
		RE::MessageBoxMenu::QueueMessage(data);
		return true;
	}

	namespace
	{
		void ShowPaged(std::string a_body, std::shared_ptr<const std::vector<MenuOption>> a_options, std::function<void()> a_onBack, std::size_t a_page)
		{
			// 5 options a page; the last page takes up to 6, as 6 + Back + Cancel is still 8 buttons (no page for a
			// lone 6th option).
			const auto total = a_options->size();
			std::vector<std::size_t> starts{ 0 };
			while (total - starts.back() > kOptionsPerPage + 1) {
				starts.push_back(starts.back() + kOptionsPerPage);
			}
			const auto pages = starts.size();
			a_page = std::min(a_page, pages - 1);
			const auto first = starts[a_page];
			const auto last = a_page + 1 < pages ? starts[a_page + 1] : total;

			std::vector<std::string>           labels;
			std::vector<std::function<void()>> actions;
			for (auto i = first; i < last; ++i) {
				labels.push_back((*a_options)[i].label);
				actions.push_back((*a_options)[i].action);
			}
			if (last < total) {
				labels.emplace_back("More...");
				actions.emplace_back([=]() { ShowPaged(a_body, a_options, a_onBack, a_page + 1); });
			}
			if (a_page > 0) {
				labels.emplace_back("Back");
				actions.emplace_back([=]() { ShowPaged(a_body, a_options, a_onBack, a_page - 1); });
			} else if (a_onBack) {
				labels.emplace_back("Back");
				actions.emplace_back(a_onBack);
			}
			labels.emplace_back("Cancel");
			actions.emplace_back(nullptr);

			auto body = a_body;
			if (pages > 1) {
				body += std::format("\n(page {}/{})", a_page + 1, pages);
			}
			ShowMessageBox(body, labels, [actions = std::move(actions)](std::size_t a_index) {
				if (a_index < actions.size() && actions[a_index]) {
					actions[a_index]();
				}
			});
		}
	}

	void ShowOptions(std::string a_body, std::vector<MenuOption> a_options, std::function<void()> a_onBack, std::size_t a_page)
	{
		ShowPaged(std::move(a_body), std::make_shared<const std::vector<MenuOption>>(std::move(a_options)), std::move(a_onBack), a_page);
	}

	void ShowNotification(std::string_view a_message)
	{
		if (a_message.empty()) {
			return;
		}
		const std::string text{ a_message };
		RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true);
	}
}
