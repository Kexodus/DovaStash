# Post-build deploy, run as: cmake -DDLL=... -DPDB=... -DDIST_DIR=... -DINTERFACE_DIR=... -DDEFAULT_INI=... -DDEPLOY_DIR=... -P Deploy.cmake
#
# Layout produced under DEPLOY_DIR (an MO2 mod folder, i.e. maps onto Skyrim's Data/):
#   SKSE/Plugins/DovaStash.dll, DovaStash.pdb
#   SKSE/Plugins/DovaStash.ini          created if missing; otherwise kept, and only [Sections] that the
#                                       default INI has but it lacks are appended (new settings show up
#                                       without touching the user's values)
#   SKSE/Plugins/DovaStash.ini.default  the shipped defaults, as the release zip has them (Settings::Load makes
#                                       DovaStash.ini from it when that file is missing)
#   SKSE/Plugins/DovaStash/...          exact mirror of dist/Data/SKSE/Plugins/DovaStash (if present): files dist no
#                                       longer has are removed (a renamed template would stay live otherwise),
#                                       except what the plugin or the player writes (Layouts/, Backups/,
#                                       Templates/User/, AutoTest/: under MO2 those live in overwrite anyway)
#   Interface/...                       dist/Data/Interface (if present: the list menu logo); Interface/DovaStash
#                                       is an exact mirror
#
# The Dova Stash Editor (Tools/DovaStashEditor.exe and .com) is not copied here: the editor build deploys itself
# (preset editor-release), so a plugin build never replaces a newer editor with an older build/editor one.

foreach(_var DLL DEPLOY_DIR)
	if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
		message(FATAL_ERROR "Deploy.cmake: ${_var} not set")
	endif()
endforeach()

set(_plugins "${DEPLOY_DIR}/SKSE/Plugins")
file(MAKE_DIRECTORY "${_plugins}")

# Top-level entries no build writes (a test tool's output, a hand-copied file): Root Builder copies root/ into the game
# folder, so say so once per deploy. Nothing is removed: they may be the player's.
file(GLOB _top RELATIVE "${DEPLOY_DIR}" "${DEPLOY_DIR}/*")
set(_stray "")
foreach(_entry IN LISTS _top)
	if(NOT _entry MATCHES "^(SKSE|Interface|Tools|meta\\.ini)$")
		list(APPEND _stray "${_entry}")
	endif()
endforeach()
if(_stray)
	# root/ holding only tools/stress_loot.py's old output (ds_*.txt, ds_manifest.csv): name it and give the command.
	set(_legacy FALSE)
	if(_stray STREQUAL "root" AND IS_DIRECTORY "${DEPLOY_DIR}/root")
		file(GLOB_RECURSE _rootFiles RELATIVE "${DEPLOY_DIR}/root" "${DEPLOY_DIR}/root/*")
		set(_legacy TRUE)
		foreach(_f IN LISTS _rootFiles)
			if(NOT _f MATCHES "^ds_[^/]*\\.txt$" AND NOT _f STREQUAL "ds_manifest.csv")
				set(_legacy FALSE)
			endif()
		endforeach()
	endif()
	string(REPLACE ";" ", " _stray "${_stray}")
	if(_legacy)
		message(WARNING "Deploy: ${DEPLOY_DIR}/root holds only the old output of tools/stress_loot.py (ds_*.txt, ds_manifest.csv), "
			"which Root Builder copies into the game folder. Remove it: cmake -E rm -rf \"${DEPLOY_DIR}/root\"")
	else()
		message(WARNING "Deploy: ${DEPLOY_DIR} has entries no build writes: ${_stray}. Remove them unless they belong there.")
	endif()
endif()

# The game locks the DLL (and PDB) while it runs: report that instead of failing the build, and still
# deploy the data and INI below.
function(_dova_deploy_binary _src)
	get_filename_component(_name "${_src}" NAME)
	file(COPY_FILE "${_src}" "${_plugins}/${_name}" RESULT _rc)
	if(_rc EQUAL 0)
		message(STATUS "Deployed ${_src}")
	else()
		message(WARNING "Could not replace ${_plugins}/${_name} (${_rc}). Is Skyrim running? Quit it and run the DovaStash_deploy target.")
	endif()
endfunction()

_dova_deploy_binary("${DLL}")
if(DEFINED PDB AND EXISTS "${PDB}")
	_dova_deploy_binary("${PDB}")
endif()

if(DEFINED DEFAULT_INI AND EXISTS "${DEFAULT_INI}")
	file(COPY_FILE "${DEFAULT_INI}" "${_plugins}/DovaStash.ini.default" ONLY_IF_DIFFERENT)
	if(NOT EXISTS "${_plugins}/DovaStash.ini")
		file(COPY_FILE "${DEFAULT_INI}" "${_plugins}/DovaStash.ini")
		message(STATUS "Deployed default DovaStash.ini")
	else()
		# Append every [Section] of the default INI that the existing file does not have yet, with its
		# comments. Strings are handled with FIND/SUBSTRING only: INI comments are full of ';', which
		# CMake would treat as list separators.
		file(READ "${DEFAULT_INI}" _default)
		file(READ "${_plugins}/DovaStash.ini" _existing)
		set(_existing "\n${_existing}")
		set(_rest "\n${_default}")
		set(_added "")
		set(_appendix "")
		while(TRUE)
			string(FIND "${_rest}" "\n[" _start)
			if(_start EQUAL -1)
				break()
			endif()
			math(EXPR _start "${_start} + 1")
			string(SUBSTRING "${_rest}" ${_start} -1 _rest)
			string(FIND "${_rest}" "]" _close)
			math(EXPR _close "${_close} + 1")
			string(SUBSTRING "${_rest}" 0 ${_close} _header)
			string(FIND "${_rest}" "\n[" _next)
			if(_next EQUAL -1)
				set(_section "${_rest}")
			else()
				string(SUBSTRING "${_rest}" 0 ${_next} _section)
			endif()
			string(FIND "${_existing}" "\n${_header}" _has)
			if(_has EQUAL -1)
				string(APPEND _appendix "\n${_section}")
				string(APPEND _added " ${_header}")
			endif()
			if(_next EQUAL -1)
				break()
			endif()
		endwhile()
		if(_added STREQUAL "")
			message(STATUS "Kept existing ${_plugins}/DovaStash.ini")
		else()
			file(APPEND "${_plugins}/DovaStash.ini" "${_appendix}")
			message(STATUS "Kept existing ${_plugins}/DovaStash.ini, added new sections:${_added}")
		endif()
	endif()
endif()

# Removes the files under `_dst` that `_src` does not have (then the folders left empty), so `_dst` mirrors `_src`
# exactly. Paths the plugin or the player write are never touched.
function(_dova_prune _src _dst _what)
	if(NOT IS_DIRECTORY "${_dst}")
		return()
	endif()
	file(GLOB_RECURSE _deployed RELATIVE "${_dst}" "${_dst}/*")
	foreach(_rel IN LISTS _deployed)
		if(_rel MATCHES "^(Layouts|Backups|AutoTest|Templates/User)(/|$)")
			continue()
		endif()
		if(NOT EXISTS "${_src}/${_rel}")
			file(REMOVE "${_dst}/${_rel}")
			message(STATUS "Removed ${_what}/${_rel} (no longer in dist)")
		endif()
	endforeach()
	file(GLOB_RECURSE _dirs LIST_DIRECTORIES true RELATIVE "${_dst}" "${_dst}/*")
	list(SORT _dirs ORDER DESCENDING)  # deeper folders first
	foreach(_rel IN LISTS _dirs)
		if(IS_DIRECTORY "${_dst}/${_rel}" AND NOT EXISTS "${_src}/${_rel}" AND NOT _rel MATCHES "^(Layouts|Backups|AutoTest|Templates/User)(/|$)")
			file(GLOB _left "${_dst}/${_rel}/*")
			if(NOT _left)
				file(REMOVE_RECURSE "${_dst}/${_rel}")
				message(STATUS "Removed ${_what}/${_rel}/ (no longer in dist)")
			endif()
		endif()
	endforeach()
endfunction()

if(DEFINED DIST_DIR AND IS_DIRECTORY "${DIST_DIR}")
	_dova_prune("${DIST_DIR}" "${_plugins}/DovaStash" "SKSE/Plugins/DovaStash")
	file(GLOB _dist_entries "${DIST_DIR}/*")
	if(_dist_entries)
		file(COPY ${_dist_entries} DESTINATION "${_plugins}/DovaStash")
		message(STATUS "Deployed dist data from ${DIST_DIR}")
	else()
		message(STATUS "dist data folder is empty, nothing to copy")
	endif()
else()
	message(STATUS "No dist data folder (${DIST_DIR}), skipping")
endif()

# The hotkey list menu's logo (Interface/DovaStash/logo.dds): Scaleform loads it relative to Interface/.
if(DEFINED INTERFACE_DIR AND IS_DIRECTORY "${INTERFACE_DIR}")
	if(IS_DIRECTORY "${INTERFACE_DIR}/DovaStash")
		_dova_prune("${INTERFACE_DIR}/DovaStash" "${DEPLOY_DIR}/Interface/DovaStash" "Interface/DovaStash")
	endif()
	file(GLOB _interface_entries "${INTERFACE_DIR}/*")
	if(_interface_entries)
		file(COPY ${_interface_entries} DESTINATION "${DEPLOY_DIR}/Interface")
		message(STATUS "Deployed interface files from ${INTERFACE_DIR}")
	endif()
endif()
