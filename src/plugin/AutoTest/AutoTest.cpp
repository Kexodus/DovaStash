#include "AutoTest/AutoTest.h"

#include "AutoTest/LogCapture.h"
#include "AutoTest/Steps.h"
#include "Game/FormUtil.h"
#include "Home/Store.h"

#include <DovaStash/Version.h>

#include <Windows.h>

#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>

namespace dova::autotest
{
	namespace
	{
		constexpr auto kPlanDir = "Data/SKSE/Plugins/DovaStash/AutoTest"sv;

		struct Test
		{
			std::string       id;
			std::string       title;
			std::vector<Json> steps;
			std::string       manual;  // not automated: why (listed in the results as "manual")
		};

		struct Plan
		{
			std::string                  file;
			std::string                  name;
			std::optional<core::FormRef> home;
			std::string                  coc;
			std::vector<Test>            tests;
			std::string                  refused;  // why the plan does not run
		};

		struct Options
		{
			double startDelay{ 5.0 };       // seconds at the main menu before the first coc
			double timeoutMinutes{ 20.0 };  // the whole run
			bool   quit{ true };            // qqq when done
			// Message boxes the game or another mod shows by itself (not Dova Stash's): answered when their text
			// contains `first`, with button `second`.
			std::vector<std::pair<std::string, std::int32_t>> answers;
		};

		enum class Phase : std::uint8_t
		{
			Off,
			Boot,
			Running,
			Done
		};

		struct Run
		{
			Phase                              phase{ Phase::Off };
			Options                            options;
			std::vector<std::filesystem::path> files;
			std::vector<Plan>                  plans;
			std::filesystem::path              outDir;
			std::string                        startedAt;

			std::size_t plan{ 0 };
			std::size_t test{ 0 };
			std::size_t step{ 0 };
			bool        inTest{ false };
			bool        planStarted{ false };

			std::optional<Clock::time_point> mainMenuSince;
			Clock::time_point                started;
			Clock::time_point                deadline;
			Clock::time_point                testStarted;
			std::optional<Wait>              wait;
			Context                          ctx;
			std::vector<TestResult>          results;
			std::atomic<bool>                tickQueued{ false };
			std::atomic<bool>                stop{ false };
			std::string                      lastBox;
		};

		Run& R()
		{
			static Run run;
			return run;
		}

		std::string Now()
		{
			const auto  t = std::time(nullptr);
			std::tm     local{};
			char        buffer[32]{};
			if (localtime_s(&local, &t) == 0) {
				std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
			}
			return buffer;
		}

		std::string FileName(const std::filesystem::path& a_path)
		{
			const auto text = a_path.filename().u8string();
			return std::string(text.begin(), text.end());
		}

		// --- plans -----------------------------------------------------------------------------------------

		std::optional<Plan> LoadPlan(const std::filesystem::path& a_path)
		{
			Plan plan;
			plan.file = FileName(a_path);
			try {
				std::ifstream in(a_path, std::ios::binary);
				const auto    doc = Json::parse(in, nullptr, true, true);  // comments allowed
				plan.name = doc.value("name", plan.file);
				if (doc.contains("home")) {
					plan.home = core::FormRef::Parse(doc["home"].get<std::string>());
					if (!plan.home) {
						plan.refused = "\"home\" is not a form ref";
					}
				}
				plan.coc = doc.value("coc", ""s);
				for (const auto& t : doc.value("tests", Json::array())) {
					Test test;
					test.id = t.value("id", ""s);
					test.title = t.value("title", ""s);
					test.manual = t.value("manual", ""s);
					for (const auto& s : t.value("steps", Json::array())) {
						test.steps.push_back(s);
					}
					plan.tests.push_back(std::move(test));
				}
			} catch (const std::exception& e) {
				logger::error("AutoTest: {} could not be read: {}", plan.file, e.what());
				plan.refused = std::format("could not be read: {}", e.what());
			}
			return plan;
		}

		// --- results ---------------------------------------------------------------------------------------

		void WriteResults(bool a_finished, std::string_view a_reason)
		{
			auto&                 run = R();
			nlohmann::ordered_json doc;
			doc["schema"] = "dovastash-autotest-results-1";
			doc["plugin"] = std::format("{} v{}", version::kName, version::kString);
			doc["runtime"] = REL::Module::get().version().string();
			doc["started"] = run.startedAt;
			doc["written"] = Now();
			doc["finished"] = a_finished;
			doc["reason"] = a_reason;
			std::map<std::string, int> summary{ { "pass", 0 }, { "fail", 0 }, { "skip", 0 }, { "manual", 0 } };
			auto                       tests = nlohmann::ordered_json::array();
			for (const auto& t : run.results) {
				++summary[t.status];
				nlohmann::ordered_json entry;
				entry["id"] = t.id;
				entry["title"] = t.title;
				entry["plan"] = t.plan;
				entry["status"] = t.status;
				if (!t.note.empty()) {
					entry["note"] = t.note;
				}
				entry["seconds"] = std::round(t.seconds * 10.0) / 10.0;
				auto checks = nlohmann::ordered_json::array();
				for (const auto& c : t.checks) {
					checks.push_back({ { "ok", c.ok }, { "what", c.what }, { "detail", c.detail } });
				}
				entry["checks"] = std::move(checks);
				tests.push_back(std::move(entry));
			}
			doc["summary"] = summary;
			doc["tests"] = std::move(tests);

			std::error_code ec;
			std::filesystem::create_directories(run.outDir, ec);
			const auto path = run.outDir / "results.json";
			auto       temp = path;
			temp += ".tmp";
			{
				std::ofstream out(temp, std::ios::binary | std::ios::trunc);
				out << doc.dump(2);
			}
			// The runner reads the file while the game writes it: a refused rename is tried again.
			for (int attempt = 0; attempt < 20; ++attempt) {
				std::filesystem::rename(temp, path, ec);
				if (!ec) {
					break;
				}
				std::this_thread::sleep_for(25ms);
			}
			if (ec) {
				// Still refused (a scanner holding the file): written in place instead.
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				out << doc.dump(2);
				if (!out) {
					logger::error("AutoTest: could not write {}: {}", home::Store::PathText(path), ec.message());
				}
				std::filesystem::remove(temp, ec);
			}
		}

		// --- the run ---------------------------------------------------------------------------------------

		void Quit()
		{
			logger::info("AutoTest: quitting the game");
			spdlog::default_logger()->flush();
			Console("qqq");
			// If the console command did not end the game, the process ends (nothing to save).
			std::thread([]() {
				std::this_thread::sleep_for(20s);
				spdlog::default_logger()->flush();
				TerminateProcess(GetCurrentProcess(), 0);
			}).detach();
		}

		void Finish(std::string_view a_reason)
		{
			auto& run = R();
			if (run.phase == Phase::Done) {
				return;
			}
			run.phase = Phase::Done;
			int pass = 0, fail = 0, other = 0;
			for (const auto& t : run.results) {
				(t.status == "pass" ? pass : t.status == "fail" ? fail : other)++;
			}
			logger::info("AutoTest: run {} after {:.0f} s: {} passed, {} failed, {} skipped or manual", a_reason,
				std::chrono::duration<double>(Clock::now() - run.started).count(), pass, fail, other);
			WriteResults(true, a_reason);
			run.stop = true;
			if (run.options.quit) {
				std::thread([]() {
					std::this_thread::sleep_for(2s);
					SKSE::GetTaskInterface()->AddTask([]() { Quit(); });
				}).detach();
			}
		}

		void EndTest()
		{
			auto& run = R();
			if (!run.inTest) {
				return;
			}
			auto& result = run.results.back();
			result.seconds = std::chrono::duration<double>(Clock::now() - run.testStarted).count();
			// A message box left open blocks every container and crafting menu of the tests that follow (and of the
			// next plans): name the culprit here instead of letting the run cascade.
			if (const auto box = MessageBoxText(); !box.empty()) {
				run.ctx.Record(false, "left a message box open (it blocks every later menu)", box);
			}
			if (result.status.empty() && !run.ctx.skipReason.empty() && std::ranges::all_of(result.checks, [](const CheckRecord& c) { return c.ok; })) {
				result.status = "skip";
				result.note = run.ctx.skipReason;
			}
			if (result.status.empty()) {
				const bool failed = std::ranges::any_of(result.checks, [](const CheckRecord& c) { return !c.ok; });
				result.status = failed ? "fail" : "pass";
			}
			logger::info("AutoTest: [{}] {} {}: {}", result.status, result.id, result.title,
				result.status == "fail" ? std::format("{} of {} checks failed", std::ranges::count_if(result.checks, [](const CheckRecord& c) { return !c.ok; }),
											  result.checks.size()) :
											std::format("{} checks", result.checks.size()));
			run.inTest = false;
			run.ctx.test = nullptr;
			run.ctx.abortTest = false;
			++run.test;
			run.step = 0;
			WriteResults(false, "running");
		}

		void StartTest(const Plan& a_plan, const Test& a_test)
		{
			auto& run = R();
			run.results.push_back(TestResult{ .id = a_test.id, .title = a_test.title, .plan = a_plan.file });
			run.inTest = true;
			run.testStarted = Clock::now();
			run.ctx.test = &run.results.back();
			run.ctx.testMark = logcap::Mark();
			run.ctx.abortTest = false;
			run.ctx.skipReason.clear();
			logger::info("AutoTest: --- {} {} ({})", a_test.id, a_test.title, a_plan.name);
		}

		// Runs steps until one has to wait. Main thread.
		void Advance()
		{
			auto& run = R();
			for (int budget = 0; budget < 40 && run.phase == Phase::Running; ++budget) {
				if (run.plan >= run.plans.size()) {
					Finish("done");
					return;
				}
				auto& plan = run.plans[run.plan];
				if (!run.planStarted) {
					run.planStarted = true;
					run.ctx.home = plan.home.value_or(core::FormRef{});
					run.ctx.homeCoc = plan.coc;
					run.ctx.planMark = logcap::Mark();
					run.ctx.abortPlan = false;
					run.ctx.snapshots.clear();
					run.ctx.quote.reset();
					logger::info("AutoTest: === plan {} ({})", plan.name, plan.file);
				}
				if (run.test >= plan.tests.size()) {
					++run.plan;
					run.test = 0;
					run.step = 0;
					run.planStarted = false;
					continue;
				}
				const auto& test = plan.tests[run.test];
				if (!run.inTest) {
					StartTest(plan, test);
					auto& result = run.results.back();
					if (!test.manual.empty()) {
						result.status = "manual";
						result.note = test.manual;
					} else if (!plan.refused.empty()) {
						result.status = "fail";
						result.note = "plan not run: " + plan.refused;
					} else if (run.ctx.abortPlan) {
						result.status = "skip";
						result.note = "the plan's home could not be entered";
					}
					if (!result.status.empty()) {
						EndTest();
						continue;
					}
				}
				if (run.step >= test.steps.size() || run.ctx.abortTest) {
					if (run.ctx.abortTest && run.step < test.steps.size() && run.ctx.skipReason.empty()) {
						run.ctx.Record(false, std::format("{} step(s) not run after a failed action", test.steps.size() - run.step));
					}
					EndTest();
					continue;
				}
				run.ctx.stepMark = logcap::Mark();
				auto wait = RunStep(test.steps[run.step], run.ctx);
				++run.step;
				if (wait) {
					run.wait = std::move(wait);
					return;
				}
			}
		}

		void Tick()
		{
			auto& run = R();
			if (run.phase == Phase::Done || run.phase == Phase::Off) {
				return;
			}
			const auto now = Clock::now();
			if (const auto box = MessageBoxText(); box != run.lastBox) {
				run.lastBox = box;
				if (!box.empty()) {
					logger::info("AutoTest: a message box is up: \"{}\"", box);
					for (const auto& [contains, button] : run.options.answers) {
						if (!contains.empty() && box.find(contains) != std::string::npos) {
							logger::info("AutoTest: answering it with button {} (_run.json)", button);
							PressMessageBoxButton(button);
							break;
						}
					}
				}
			}
			if (run.phase == Phase::Running && now > run.deadline) {
				if (run.inTest) {
					run.ctx.Record(false, "the run's timeout passed", run.wait ? "waiting for: " + run.wait->what : "");
					EndTest();
				}
				Finish("timeout");
				return;
			}
			if (run.phase == Phase::Boot) {
				auto* ui = RE::UI::GetSingleton();
				if (!ui || !ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
					run.mainMenuSince.reset();
					return;
				}
				if (!run.mainMenuSince) {
					run.mainMenuSince = now;
					logger::info("AutoTest: main menu is up; starting in {} s", run.options.startDelay);
				}
				if (now - *run.mainMenuSince < std::chrono::milliseconds(static_cast<std::int64_t>(run.options.startDelay * 1000.0))) {
					return;
				}
				run.phase = Phase::Running;
				run.started = now;
				run.deadline = now + std::chrono::milliseconds(static_cast<std::int64_t>(run.options.timeoutMinutes * 60000.0));
			}
			if (run.wait) {
				auto& wait = *run.wait;
				const bool done = wait.done();
				if (!done && now < wait.deadline) {
					return;
				}
				auto finish = std::move(wait.finish);
				run.wait.reset();
				if (finish) {
					finish(!done);
				}
				if (run.ctx.next) {  // the step goes on with another wait (a fallback)
					run.wait = std::move(run.ctx.next);
					run.ctx.next.reset();
					return;
				}
			}
			Advance();
		}

		// Which thread opens the menus the steps drive (the crafting menu counts and consumes materials there; SKSE
		// tasks run on other threads): logged once per menu, for the crafting checks.
		class MenuThreadSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->opening &&
					(a_event->menuName == RE::CraftingMenu::MENU_NAME || a_event->menuName == RE::ContainerMenu::MENU_NAME)) {
					std::ostringstream id;
					id << std::this_thread::get_id();
					logger::info("AutoTest: {} opened (event on thread {})", a_event->menuName.c_str(), id.str());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void StartTicker()
		{
			std::thread([]() {
				auto& run = R();
				while (!run.stop) {
					std::this_thread::sleep_for(100ms);
					if (!run.tickQueued.exchange(true)) {
						SKSE::GetTaskInterface()->AddTask([]() {
							R().tickQueued = false;
							try {
								Tick();
							} catch (const std::exception& e) {
								logger::error("AutoTest: tick failed: {}", e.what());
							}
						});
					}
				}
			}).detach();
		}
	}

	bool Init()
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(kPlanDir, ec)) {
			return false;
		}
		auto& run = R();
		for (auto it = std::filesystem::directory_iterator(kPlanDir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			const auto name = FileName(it->path());
			if (it->path().extension() == ".json" && !name.starts_with("_")) {
				run.files.push_back(it->path());
			}
		}
		if (run.files.empty()) {
			return false;
		}
		std::ranges::sort(run.files, {}, [](const std::filesystem::path& p) { return FileName(p); });
		run.phase = Phase::Boot;
		logcap::Install();
		run.ctx.runMark = logcap::Mark();
		run.startedAt = Now();

		if (const auto options = std::filesystem::path{ kPlanDir } / "_run.json"; std::filesystem::exists(options, ec)) {
			try {
				std::ifstream in(options, std::ios::binary);
				const auto    doc = Json::parse(in, nullptr, true, true);
				run.options.startDelay = doc.value("startDelay", run.options.startDelay);
				run.options.timeoutMinutes = doc.value("timeoutMinutes", run.options.timeoutMinutes);
				run.options.quit = doc.value("quit", run.options.quit);
				for (const auto& a : doc.value("messageBoxes", Json::array())) {
					run.options.answers.emplace_back(a.value("contains", ""s), a.value("button", 0));
				}
			} catch (const std::exception& e) {
				logger::error("AutoTest: _run.json could not be read: {}", e.what());
			}
		}

		auto dir = SKSE::log::log_directory();
		run.outDir = dir ? *dir / "DovaStash" / "AutoTest" : std::filesystem::path{ "DovaStashAutoTest" };
		std::filesystem::create_directories(run.outDir / "Backups", ec);
		home::Store::RedirectBackups(run.outDir / "Backups");
		logger::info("AutoTest: {} plan(s) in {}: this is a test run (results: {})", run.files.size(), kPlanDir, home::Store::PathText(run.outDir / "results.json"));
		WriteResults(false, "starting");
		return true;
	}

	bool Enabled() noexcept
	{
		return R().phase != Phase::Off;
	}

	void OnDataLoaded()
	{
		auto& run = R();
		if (run.phase == Phase::Off) {
			return;
		}
		for (const auto& file : run.files) {
			auto plan = LoadPlan(file);
			if (!plan) {
				continue;
			}
			if (plan->refused.empty() && plan->home) {
				if (!game::LookupForm(*plan->home)) {
					plan->refused = std::format("its home {} is not in this load order", plan->home->ToString());
				} else if (home::Store::Get().HasPlayerLayoutFile(*plan->home)) {
					// The player's own setup (MO2 shows the overwrite folder to every profile): never touched.
					plan->refused = std::format("a layout for {} already exists ({}); the run only changes setups it makes itself",
						plan->home->ToString(), home::Store::PathText(home::Store::Get().LayoutPath(*plan->home)));
				}
			}
			if (!plan->refused.empty()) {
				logger::warn("AutoTest: plan {} will not run: {}", plan->file, plan->refused);
			}
			logger::info("AutoTest: plan {} ({}): {} test(s)", plan->file, plan->name, plan->tests.size());
			run.plans.push_back(std::move(*plan));
		}
		static MenuThreadSink menuThreads;
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuThreads);
		}
		StartTicker();
	}
}
