cmake_minimum_required(VERSION 3.25)

project(
	DovaStash
	VERSION 0.2.2
	DESCRIPTION "Auto-sorting loot storage for any Skyrim player home"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "In-source builds are not allowed; use a preset (see CMakePresets.json).")
endif()

option(DOVASTASH_BUILD_PLUGIN "Build the SKSE plugin DLL (requires CommonLibSSE-NG)" ON)
option(DOVASTASH_BUILD_TESTS "Build the engine-independent unit tests" ON)

# Engine-independent logic: no CommonLib, no game headers. Unit-testable.
add_subdirectory(src/core)

if(DOVASTASH_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

# SKSE plugin: CommonLib adapters, events, input, UI.
if(DOVASTASH_BUILD_PLUGIN)
	add_subdirectory(src/plugin)
endif()
