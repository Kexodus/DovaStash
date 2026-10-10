#pragma once

// Precompiled header for DovaStash.dll. CommonLibSSE-NG headers are PCH-dependent: SKSE/Impl/PCH.h
// must come first (it pulls in the std headers CommonLib relies on).
#include <SKSE/Impl/PCH.h>

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Engine-independent core (pure C++ + nlohmann-json; no CommonLib).
#include <dova/core/Core.h>

using namespace std::literals;  // CommonLibSSE-NG consumer convention ("..."sv)

namespace logger = SKSE::log;
