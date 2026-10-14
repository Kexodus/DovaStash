# Dova Stash Editor: standalone template editor (docs/EDITOR.md). Built only with -DDOVASTASH_BUILD_EDITOR=ON
# (preset `editor`); links dova_core for the template schema and validation. No CommonLib.
find_package(imgui CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)

file(GLOB_RECURSE DOVA_EDITOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(DovaStashEditor WIN32 ${DOVA_EDITOR_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/res/DovaStashEditor.rc")
# The resource script embeds the icon and the UI fonts (ui/Theme.cpp): rebuild it when they change.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/res/DovaStashEditor.rc" PROPERTIES OBJECT_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/res/DovaStashEditor.ico;${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Cinzel-VF.ttf;${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Jost-VF.ttf")
target_include_directories(DovaStashEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DovaStashEditor PRIVATE dova::core imgui::imgui ZLIB::ZLIB lz4::lz4 d3d11 dxgi d3dcompiler windowscodecs)
target_compile_definitions(DovaStashEditor PRIVATE UNICODE _UNICODE)
target_compile_features(DovaStashEditor PRIVATE cxx_std_23)
if(MSVC)
	target_compile_options(DovaStashEditor PRIVATE /W4 /permissive- /utf-8 /MP)
	# Windows subsystem (no console flash under MO2) with a wmain entry point (Unicode arguments).
	target_link_options(DovaStashEditor PRIVATE /ENTRY:wmainCRTStartup)
endif()
