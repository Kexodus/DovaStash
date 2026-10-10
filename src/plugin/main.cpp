// DovaStash.dll entry point.
//
// The SKSE plugin declaration (SKSEPlugin_Version for AE, SKSEPlugin_Query for SE/VR) is generated
// by add_commonlibsse_plugin() in src/plugin/CMakeLists.txt from the root project() version; only
// SKSEPlugin_Load lives here.

#include "Controller.h"
#include "Events/EventSinks.h"
#include "Game/FormUtil.h"
#include "Input/InputHandler.h"
#include "Log.h"
#include "Settings.h"
#include "UI/MenuFramework.h"

#include <DovaStash/Version.h>

namespace
{
	std::string_view RuntimeName()
	{
		switch (REL::Module::GetRuntime()) {
		case REL::Module::Runtime::AE:
			return "AE"sv;
		case REL::Module::Runtime::SE:
			return "SE"sv;
		case REL::Module::Runtime::VR:
			return "VR"sv;
		default:
			return "Unknown"sv;
		}
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			{
				// All SKSE plugins are loaded now, so the Menu Framework DLL (if any) is resolvable.
				const bool installed = dova::ui::menu_framework::IsInstalled();
				if (installed) {
					logger::info("SKSE Menu Framework: installed (v{:.2f})", dova::ui::menu_framework::GetVersion());
				} else {
					logger::info("SKSE Menu Framework: not installed, native MessageBox UI will be used");
				}
			}
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			logger::info("kDataLoaded: forms are available");
			// Order matters: form tables first (formList resolution needs them), then data + UI, then
			// the sinks that may start enqueueing work.
			dova::game::InitFormUtil();
			dova::Controller::Get().Init();
			dova::events::RegisterSinks();
			dova::input::RegisterInputSink();
			break;
		case SKSE::MessagingInterface::kNewGame:
			logger::info("kNewGame");
			dova::events::RegisterPlayerSinks();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			logger::info("kPostLoadGame (success: {})", a_msg->data != nullptr);
			dova::events::RegisterPlayerSinks();
			// The player may load straight into a home: refresh now (messaging runs on the main thread,
			// but go through the task queue like every other trigger).
			SKSE::GetTaskInterface()->AddTask([]() { dova::Controller::Get().OnCellMaybeChanged(); });
			break;
		case SKSE::MessagingInterface::kSaveGame:
			dova::Controller::Get().OnSaveGame();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// We set up our own logger (pattern, level from INI) instead of CommonLib's default one.
	SKSE::Init(a_skse, { .log = false });

	const bool logOk = dova::log::Init();

	auto& settings = dova::Settings::GetSingleton();
	settings.Load();
	dova::log::SetLevel(settings.GetGeneral().logLevel);

	logger::info("{} v{} loaded", dova::version::kName, dova::version::kString);
	logger::info("Runtime: Skyrim {} {} (SKSE {:#x}, plugin built against CommonLibSSE-NG SE+AE+VR)",
		RuntimeName(), REL::Module::get().version().string(), SKSE::GetSKSEVersion());
	if (!logOk) {
		// Nothing will reach a file in this case, but keep the call for debugger output.
		logger::warn("Could not resolve the SKSE log directory");
	}

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		logger::critical("Failed to register SKSE messaging listener");
		return false;
	}

	// Everything else (data, sinks, input) is set up at kDataLoaded, see OnSKSEMessage.

	logger::info("Dova Stash loaded");
	return true;
}
