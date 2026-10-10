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
		data->warningType = 10;  // same value community helpers use for plugin message boxes
		data->buttonPressOffset = 0;
		data->cancelButtonIndex = static_cast<std::int32_t>(a_buttons.size() - 1);
		data->isCancellable = true;
		data->useHtml = false;
		RE::MessageBoxMenu::QueueMessage(data);
		return true;
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
