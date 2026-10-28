# DovaStash.dll — the SKSE plugin (SE 1.5.97 + AE 1.6.x + VR in one DLL via CommonLibSSE-NG).
# Only this layer may touch RE::/SKSE::. See docs/BUILDING.md for build/deploy details.

set(DOVASTASH_COMMONLIB_DIR "${PROJECT_SOURCE_DIR}/extern/CommonLibSSE-NG")
if(NOT EXISTS "${DOVASTASH_COMMONLIB_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"CommonLibSSE-NG submodule missing. Run: git submodule update --init --recursive")
endif()

# ---- CommonLibSSE-NG ---------------------------------------------------------------------------
# All three runtimes must stay ON: the multi-runtime struct layouts are what make one DLL work on
# SE/AE/VR, and the prebuilt bundle (COMMONLIB_PREBUILT, see CMakePresets.json) only serves that set.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Build CommonLibSSE-NG's own unit tests" FORCE)

# SYSTEM: CommonLib headers go through /external:I so our /W4 does not flag them.
add_subdirectory("${DOVASTASH_COMMONLIB_DIR}" "${CMAKE_BINARY_DIR}/extern/CommonLibSSE-NG" SYSTEM EXCLUDE_FROM_ALL)

# Source build under the Visual Studio generator: CommonLib does not enable /MP itself, so its
# ~1000 TUs would compile one at a time inside the single CommonLibSSE project.
get_target_property(_dova_commonlib_imported CommonLibSSE IMPORTED)
if(MSVC AND NOT _dova_commonlib_imported)
	target_compile_options(CommonLibSSE PRIVATE /MP)
endif()

# CommonLib only include()s its plugin helper in prebuilt mode; pull it in for source builds too.
if(NOT COMMAND add_commonlibsse_plugin)
	include("${DOVASTASH_COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)  # function detours for craft from storage (Hooks/CraftingHooks)
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

# ---- Plugin target -----------------------------------------------------------------------------
set(DOVASTASH_PLUGIN_SOURCES
	PCH.h
	main.cpp
	Log.h
	Log.cpp
	Settings.h
	Settings.cpp
	Controller.h
	Controller.cpp
	Game/FormUtil.h
	Game/FormUtil.cpp
	Game/ItemAdapter.h
	Game/ItemAdapter.cpp
	Game/ItemMover.h
	Game/ItemMover.cpp
	Game/Eligibility.h
	Game/Eligibility.cpp
	Game/Displays.h
	Game/Displays.cpp
	Game/ArmorIndex.h
	Game/ArmorIndex.cpp
	Home/Store.h
	Home/Store.cpp
	Home/HomeRegistry.h
	Home/HomeRegistry.cpp
	Home/Areas.h
	Home/Areas.cpp
	Home/TemplateDisplays.h
	Home/TemplateDisplays.cpp
	Icons/IconService.h
	Icons/IconService.cpp
	Sort/SortEngine.h
	Sort/SortEngine.cpp
	Sort/DisplayFiller.h
	Sort/DisplayFiller.cpp
	Sort/CraftingSupply.h
	Sort/CraftingSupply.cpp
	Sort/SellChest.h
	Sort/SellChest.cpp
	Sort/CraftingDetail.h
	Sort/CraftingStorage.h
	Sort/CraftingStorage.cpp
	Game/RecipeIndex.h
	Game/RecipeIndex.cpp
	Events/EventSinks.h
	Events/EventSinks.cpp
	Input/InputHandler.h
	Input/InputHandler.cpp
	UI/IStashUI.h
	UI/MessageBox.h
	UI/MessageBox.cpp
	UI/MessageBoxUI.h
	UI/MessageBoxUI.cpp
	UI/MenuFramework.h
	UI/MenuFramework.cpp
	UI/MenuFrameworkUI.h
	UI/MenuFrameworkUI.cpp
	UI/Panel/Snapshot.h
	UI/Panel/Strings.h
	UI/Panel/Widgets.h
	UI/Panel/PanelModel.h
	UI/Panel/PanelModel.cpp
	UI/Panel/PanelRender.h
	UI/Panel/PanelRender.cpp
	UI/Panel/ArmorSetsModel.h
	UI/Panel/ArmorSetsModel.cpp
	UI/Panel/ArmorSetPicker.h
	UI/Panel/ArmorSetPicker.cpp
	UI/Panel/DisplaysModel.h
	UI/Panel/DisplaysModel.cpp
	UI/Panel/DisplaysPage.h
	UI/Panel/DisplaysPage.cpp
	UI/Panel/EditorInfo.h
	UI/Panel/EditorInfo.cpp
	UI/Panel/KeyCapture.h
	UI/Panel/KeyCapture.cpp
	UI/DisplayMenu.h
	UI/DisplayMenu.cpp
	UI/ArmorSetMenu.h
	UI/ArmorSetMenu.cpp
	UI/Labels.h
	UI/Labels.cpp
	UI/SaveTemplate.h
	UI/SaveTemplate.cpp
	UI/AreaPrompt.h
	UI/AreaPrompt.cpp
	UI/Factory.cpp
	UI/UIExtensions.h
	UI/UIExtensions.cpp
	UI/ListUI.h
	UI/ListUI.cpp
	UI/Overlay.h
	UI/Overlay.cpp
	Hooks/ActivatePrompt.h
	Hooks/ActivatePrompt.cpp
	Hooks/CraftingHooks.h
	Hooks/CraftingHooks.cpp
	Integrations/QuickLoot.h
	Integrations/QuickLoot.cpp
	Diagnostics/Environment.h
	Diagnostics/Environment.cpp
	AutoTest/AutoTest.h
	AutoTest/AutoTest.cpp
	AutoTest/Steps.h
	AutoTest/Steps.cpp
	AutoTest/LogCapture.h
	AutoTest/LogCapture.cpp
)
list(TRANSFORM DOVASTASH_PLUGIN_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# add_commonlibsse_plugin() creates the SHARED target, links CommonLibSSE and generates
# __DovaStashPlugin.cpp containing SKSEPluginInfo(...), which exports BOTH SKSEPlugin_Version
# (SKSE64 1.6.x / AE) and SKSEPlugin_Query (SE 1.5.97 + SKSEVR). Address Library compatibility.
add_commonlibsse_plugin(DovaStash
	NAME "DovaStash"
	AUTHOR "Dova Stash contributors"
	VERSION "${PROJECT_VERSION}"
	USE_ADDRESS_LIBRARY
	SOURCES ${DOVASTASH_PLUGIN_SOURCES}
)

target_compile_features(DovaStash PRIVATE cxx_std_23)
target_include_directories(DovaStash
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"${CMAKE_CURRENT_BINARY_DIR}/generated"
)
# Third-party headers: /external:I so /W4 does not apply to them.
target_include_directories(DovaStash SYSTEM
	PRIVATE
		"${PROJECT_SOURCE_DIR}/extern/SKSEMenuFramework"
		"${PROJECT_SOURCE_DIR}/extern/QuickLootIE"
		"${SIMPLEINI_INCLUDE_DIRS}"
)
# dova::core: engine-independent logic (categories, layouts, templates, keep rules, sort planner).
target_link_libraries(DovaStash PRIVATE spdlog::spdlog minhook::minhook dova::core Version)  # Version: file version resources (Diagnostics)

target_precompile_headers(DovaStash PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PCH.h")

# Version info for code (single source of truth: project() in the root CMakeLists.txt).
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/generated/DovaStash/Version.h"
	@ONLY
)

if(MSVC)
	target_compile_options(DovaStash PRIVATE
		/W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /external:W0 /MP
		/Zi                                  # PDB in every config (crash logs)
		"$<$<NOT:$<CONFIG:Debug>>:/Zc:inline;/Gy;/Gw>"
	)
	target_link_options(DovaStash PRIVATE
		/DEBUG:FULL
		"$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>"
	)
	target_compile_definitions(DovaStash PRIVATE
		WIN32_LEAN_AND_MEAN NOMINMAX
	)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${DOVASTASH_PLUGIN_SOURCES})

# ---- Deploy into the MO2 mod folder ------------------------------------------------------------
option(DOVASTASH_DEPLOY "Copy DovaStash.dll/.pdb and dist data into DOVASTASH_DEPLOY_DIR after build" ON)
# The environment variable DOVASTASH_DEPLOY_DIR names the mod folder on another machine; the default is the
# developer's MO2 instance.
if(DEFINED ENV{DOVASTASH_DEPLOY_DIR})
	set(_dova_deploy_default "$ENV{DOVASTASH_DEPLOY_DIR}")
else()
	set(_dova_deploy_default "E:/modding/Kexodus Skyrim/mods/Dova Stash")
endif()
set(DOVASTASH_DEPLOY_DIR "${_dova_deploy_default}"
	CACHE PATH "Mod root to deploy into (the folder that maps onto Skyrim's Data/)")

# Deploy after the build only where the mods folder exists (or the environment names the folder): a machine
# without the developer's drive still builds.
set(_dova_deploy_after_build OFF)
if(DOVASTASH_DEPLOY)
	get_filename_component(_dova_mods_dir "${DOVASTASH_DEPLOY_DIR}" DIRECTORY)
	if(IS_DIRECTORY "${_dova_mods_dir}" OR DEFINED ENV{DOVASTASH_DEPLOY_DIR})
		set(_dova_deploy_after_build ON)
	else()
		message(STATUS "Deploy off: ${DOVASTASH_DEPLOY_DIR} not found (set DOVASTASH_DEPLOY_DIR)")
	endif()
endif()

set(_dova_deploy_cmd
	"${CMAKE_COMMAND}"
	"-DDLL=$<TARGET_FILE:DovaStash>"
	"-DPDB=$<TARGET_PDB_FILE:DovaStash>"
	"-DDIST_DIR=${PROJECT_SOURCE_DIR}/dist/Data/SKSE/Plugins/DovaStash"
	"-DINTERFACE_DIR=${PROJECT_SOURCE_DIR}/dist/Data/Interface"
	"-DDEFAULT_INI=${CMAKE_CURRENT_SOURCE_DIR}/DovaStash.ini.default"
	"-DDEPLOY_DIR=${DOVASTASH_DEPLOY_DIR}"
	-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Deploy.cmake"
)

if(_dova_deploy_after_build)
	# Runs whenever the DLL relinks.
	add_custom_command(TARGET DovaStash POST_BUILD
		COMMAND ${_dova_deploy_cmd}
		COMMENT "Deploying DovaStash to ${DOVASTASH_DEPLOY_DIR}"
		VERBATIM
	)
endif()

# Explicit re-deploy (e.g. after editing only dist/ JSON): cmake --build build/plugin --config Release --target DovaStash_deploy
add_custom_target(DovaStash_deploy
	COMMAND ${_dova_deploy_cmd}
	DEPENDS DovaStash
	COMMENT "Deploying DovaStash to ${DOVASTASH_DEPLOY_DIR}"
	VERBATIM
)

# The release zip (one installable archive): cmake --build --preset plugin-release --target DovaStash_package
# Stages build/release/Dova Stash/ (DLL, PDB, the default INI as DovaStash.ini.default, dist data, Interface, the
# editor of build/editor and the user guide PDF) and zips it to build/release/DovaStash-<version>.zip
# (cmake/Package.cmake). A missing editor or guide stops it unless PACKAGE_ALLOW_PARTIAL is ON.
option(PACKAGE_ALLOW_PARTIAL "Let DovaStash_package zip without the editor or the user guide (test zips only)" OFF)
add_custom_target(DovaStash_package
	COMMAND "${CMAKE_COMMAND}"
		"-DDLL=$<TARGET_FILE:DovaStash>"
		"-DPDB=$<TARGET_PDB_FILE:DovaStash>"
		"-DDIST_DIR=${PROJECT_SOURCE_DIR}/dist/Data/SKSE/Plugins/DovaStash"
		"-DINTERFACE_DIR=${PROJECT_SOURCE_DIR}/dist/Data/Interface"
		"-DDEFAULT_INI=${CMAKE_CURRENT_SOURCE_DIR}/DovaStash.ini.default"
		"-DEDITOR_DIR=${PROJECT_SOURCE_DIR}/build/editor/editor/Release"
		"-DGUIDE_PDF=${PROJECT_SOURCE_DIR}/presskit/guide/DovaStashEditor-UserGuide.pdf"
		"-DOUT_DIR=${PROJECT_SOURCE_DIR}/build/release"
		"-DVERSION=${PROJECT_VERSION}"
		"-DPACKAGE_ALLOW_PARTIAL=${PACKAGE_ALLOW_PARTIAL}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Package.cmake"
	DEPENDS DovaStash
	COMMENT "Packaging Dova Stash ${PROJECT_VERSION} into build/release"
	VERBATIM
)
