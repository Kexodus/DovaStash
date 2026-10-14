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

# ---- Deploy next to the plugin's copy ----------------------------------------------------------
# The plugin build copies the editor into the MO2 mod's Tools folder too, but only when the DLL relinks;
# the editor preset keeps that copy current on its own (the same option and folder as src/plugin).
option(DOVASTASH_DEPLOY "Copy DovaStashEditor.exe into DOVASTASH_DEPLOY_DIR/Tools after build" ON)
set(DOVASTASH_DEPLOY_DIR "E:/modding/Kexodus Skyrim/mods/Dova Stash"
	CACHE PATH "Mod root to deploy into (the folder that maps onto Skyrim's Data/)")
if(DOVASTASH_DEPLOY)
	add_custom_command(TARGET DovaStashEditor POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${DOVASTASH_DEPLOY_DIR}/Tools"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:DovaStashEditor>" "${DOVASTASH_DEPLOY_DIR}/Tools/"
		COMMENT "Deploying DovaStashEditor.exe to ${DOVASTASH_DEPLOY_DIR}/Tools"
		VERBATIM
	)
endif()
