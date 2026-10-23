# Dova Stash Editor: standalone template editor (docs/EDITOR.md). Built only with -DDOVASTASH_BUILD_EDITOR=ON
# (preset `editor`); links dova_core for the template schema and validation. No CommonLib.
find_package(imgui CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)

file(GLOB_RECURSE DOVA_EDITOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
list(REMOVE_ITEM DOVA_EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Everything but the entry point, compiled once: the editor and its tests (tests/EditorTests.cpp) link it.
add_library(DovaStashEditorObjects OBJECT ${DOVA_EDITOR_SOURCES})
target_include_directories(DovaStashEditorObjects PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DovaStashEditorObjects PUBLIC dova::core imgui::imgui ZLIB::ZLIB lz4::lz4 d3d11 dxgi d3dcompiler windowscodecs)
target_compile_definitions(DovaStashEditorObjects PUBLIC UNICODE _UNICODE)
target_compile_features(DovaStashEditorObjects PUBLIC cxx_std_23)
if(MSVC)
	target_compile_options(DovaStashEditorObjects PUBLIC /W4 /permissive- /utf-8 /MP)
endif()

add_executable(DovaStashEditor WIN32 "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/res/DovaStashEditor.rc")
# The resource script embeds the icon and the UI fonts (ui/Theme.cpp): rebuild it when they change.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/res/DovaStashEditor.rc" PROPERTIES OBJECT_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/res/DovaStashEditor.ico;${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Cinzel-VF.ttf;${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Jost-VF.ttf")
target_link_libraries(DovaStashEditor PRIVATE DovaStashEditorObjects)
if(MSVC)
	# Windows subsystem (no console flash under MO2) with a wmain entry point (Unicode arguments).
	target_link_options(DovaStashEditor PRIVATE /ENTRY:wmainCRTStartup)
endif()

# ---- DovaStashEditorTests: the readers (BSA, Assets, DDS, NIF) and state.json on fixtures it writes --------------------
# `ctest --preset editor` (or run the exe); docs/EDITOR.md §5.2.
option(DOVASTASH_EDITOR_TESTS "Build the editor's tests with the editor" ON)
if(DOVASTASH_EDITOR_TESTS)
	add_executable(DovaStashEditorTests "${CMAKE_CURRENT_SOURCE_DIR}/tests/EditorTests.cpp")
	target_link_libraries(DovaStashEditorTests PRIVATE DovaStashEditorObjects)
	enable_testing()
	add_test(NAME editor-tests COMMAND DovaStashEditorTests)
	add_dependencies(DovaStashEditor DovaStashEditorTests)  # built with the editor preset's build
endif()

# ---- DovaStashEditor.com: the console companion -------------------------------------------------
# Typed as `DovaStashEditor` in cmd or PowerShell the .com wins (PATHEXT): it runs the .exe with the same
# arguments, waits, and passes on its output and exit code (console/Main.cpp, docs/EDITOR.md §9).
add_executable(DovaStashEditorConsole "${CMAKE_CURRENT_SOURCE_DIR}/console/Main.cpp")
set_target_properties(DovaStashEditorConsole PROPERTIES OUTPUT_NAME DovaStashEditor SUFFIX ".com" PDB_NAME DovaStashEditorConsole)
target_compile_definitions(DovaStashEditorConsole PRIVATE UNICODE _UNICODE)
target_compile_features(DovaStashEditorConsole PRIVATE cxx_std_23)
if(MSVC)
	target_compile_options(DovaStashEditorConsole PRIVATE /W4 /permissive- /utf-8)
	target_link_options(DovaStashEditorConsole PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:wmainCRTStartup)
endif()
add_dependencies(DovaStashEditor DovaStashEditorConsole)  # built (and deployed) with the editor

# ---- Deploy into the MO2 mod's Tools folder ---------------------------------------------------------
# Only the editor build copies the editor (a plugin build never does); the same option, folder and rules as
# src/plugin. The copy runs when the exe relinks; DovaStashEditor_deploy retries it on its own.
option(DOVASTASH_DEPLOY "Copy DovaStashEditor.exe into DOVASTASH_DEPLOY_DIR/Tools after build" ON)
# The environment variable DOVASTASH_DEPLOY_DIR names the mod folder on another machine; the default is the
# developer's MO2 instance.
if(DEFINED ENV{DOVASTASH_DEPLOY_DIR})
	set(_dova_editor_deploy_default "$ENV{DOVASTASH_DEPLOY_DIR}")
else()
	set(_dova_editor_deploy_default "E:/modding/Kexodus Skyrim/mods/Dova Stash")
endif()
set(DOVASTASH_DEPLOY_DIR "${_dova_editor_deploy_default}"
	CACHE PATH "Mod root to deploy into (the folder that maps onto Skyrim's Data/)")
# Deploy only where the mods folder exists (or the environment names the folder): a machine without the
# developer's drive still builds, and no stray folder tree is made.
set(_dova_editor_deploy OFF)
if(DOVASTASH_DEPLOY)
	get_filename_component(_dova_editor_mods_dir "${DOVASTASH_DEPLOY_DIR}" DIRECTORY)
	if(IS_DIRECTORY "${_dova_editor_mods_dir}" OR DEFINED ENV{DOVASTASH_DEPLOY_DIR})
		set(_dova_editor_deploy ON)
	else()
		message(STATUS "Editor deploy off: ${DOVASTASH_DEPLOY_DIR} not found (set DOVASTASH_DEPLOY_DIR)")
	endif()
endif()
if(_dova_editor_deploy)
	# A running editor locks its file: the script warns instead of failing the build (editor/cmake/Deploy.cmake).
	add_custom_command(TARGET DovaStashEditor POST_BUILD
		COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:DovaStashEditor>" "-DDEPLOY_DIR=${DOVASTASH_DEPLOY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake"
		COMMENT "Deploying DovaStashEditor.exe to ${DOVASTASH_DEPLOY_DIR}/Tools"
		VERBATIM
	)
	add_custom_command(TARGET DovaStashEditorConsole POST_BUILD
		COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:DovaStashEditorConsole>" "-DDEPLOY_DIR=${DOVASTASH_DEPLOY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake"
		COMMENT "Deploying DovaStashEditor.com to ${DOVASTASH_DEPLOY_DIR}/Tools"
		VERBATIM
	)
	# Explicit re-deploy (the exe was up to date, or the editor was running during the build):
	# cmake --build --preset editor-release --target DovaStashEditor_deploy
	add_custom_target(DovaStashEditor_deploy
		COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:DovaStashEditor>" "-DDEPLOY_DIR=${DOVASTASH_DEPLOY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake"
		COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:DovaStashEditorConsole>" "-DDEPLOY_DIR=${DOVASTASH_DEPLOY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake"
		DEPENDS DovaStashEditor DovaStashEditorConsole
		COMMENT "Deploying DovaStashEditor.exe and .com to ${DOVASTASH_DEPLOY_DIR}/Tools"
		VERBATIM
	)
endif()
