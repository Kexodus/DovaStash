# Unit tests for dova_core. Owned by the core workstream.
find_package(Catch2 3 CONFIG REQUIRED)

file(GLOB_RECURSE DOVA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(dova_tests ${DOVA_TEST_SOURCES})
target_link_libraries(dova_tests PRIVATE dova::core Catch2::Catch2WithMain)
target_compile_definitions(dova_tests PRIVATE
	DOVA_DIST_DIR="${PROJECT_SOURCE_DIR}/dist/Data/SKSE/Plugins/DovaStash"
	DOVA_OPTIONAL_DIR="${PROJECT_SOURCE_DIR}/dist/Optional"
	DOVA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(MSVC)
	target_compile_options(dova_tests PRIVATE /W4 /permissive- /utf-8)
endif()

include(Catch)
catch_discover_tests(dova_tests)
