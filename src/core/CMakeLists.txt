# dova_core: engine-independent logic (no CommonLib). Owned by the core workstream.
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB_RECURSE DOVA_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(dova_core STATIC ${DOVA_CORE_SOURCES})
add_library(dova::core ALIAS dova_core)
target_include_directories(dova_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(dova_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(dova_core PUBLIC cxx_std_23)
if(MSVC)
	target_compile_options(dova_core PRIVATE /W4 /permissive- /utf-8)
endif()
