# Release package, run as: cmake -DDLL=... -DPDB=... -DDIST_DIR=... -DINTERFACE_DIR=... -DDEFAULT_INI=... -DEDITOR_DIR=...
#                                -DGUIDE_PDF=... -DOUT_DIR=... -DVERSION=... [-DPACKAGE_ALLOW_PARTIAL=ON] -P Package.cmake
# (the DovaStash_package target passes them). One installable zip per release:
#
#   <OUT_DIR>/Dova Stash/                 the mod folder as a mod manager installs it (maps onto Data/)
#     SKSE/Plugins/DovaStash.dll, .pdb
#     SKSE/Plugins/DovaStash.ini.default  the shipped defaults; the plugin copies it to DovaStash.ini when that file
#                                         is missing (Settings::Load), so an update never overwrites the player's INI
#     SKSE/Plugins/DovaStash/             dist/Data/SKSE/Plugins/DovaStash (categories, templates)
#     Interface/                          dist/Data/Interface (the list menu logo)
#     Tools/DovaStashEditor.exe, .com     the editor build (preset editor-release)
#     Tools/DovaStashEditor-UserGuide.pdf presskit/guide's PDF
#   A missing editor or guide stops the package (one complete zip per release), unless PACKAGE_ALLOW_PARTIAL is ON.
#   <OUT_DIR>/DovaStash-<VERSION>.zip     that folder's contents, zipped: SKSE/, Interface/ and Tools/ at the top, with
#                                         no wrapping folder, so the zip maps onto Data/ (extracted by hand too)
#
# The stage is made fresh each time: nothing a test session wrote (layouts, backups, root/ files) gets in.

foreach(_var DLL OUT_DIR VERSION DIST_DIR DEFAULT_INI)
	if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
		message(FATAL_ERROR "Package.cmake: ${_var} not set")
	endif()
endforeach()

set(_stage "${OUT_DIR}/Dova Stash")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_stage}/SKSE/Plugins/DovaStash")

file(COPY_FILE "${DLL}" "${_stage}/SKSE/Plugins/DovaStash.dll")
if(DEFINED PDB AND EXISTS "${PDB}")
	file(COPY_FILE "${PDB}" "${_stage}/SKSE/Plugins/DovaStash.pdb")
endif()
file(COPY_FILE "${DEFAULT_INI}" "${_stage}/SKSE/Plugins/DovaStash.ini.default")

file(GLOB _dist "${DIST_DIR}/*")
foreach(_entry IN LISTS _dist)
	get_filename_component(_name "${_entry}" NAME)
	if(_name MATCHES "^(Layouts|Backups|AutoTest)$")
		continue()  # written by the plugin or the test harness, never shipped
	endif()
	file(COPY "${_entry}" DESTINATION "${_stage}/SKSE/Plugins/DovaStash")
endforeach()
file(REMOVE_RECURSE "${_stage}/SKSE/Plugins/DovaStash/Templates/User")

if(DEFINED INTERFACE_DIR AND IS_DIRECTORY "${INTERFACE_DIR}")
	file(COPY "${INTERFACE_DIR}/" DESTINATION "${_stage}/Interface")
endif()

file(MAKE_DIRECTORY "${_stage}/Tools")
set(_missing "")
foreach(_tool DovaStashEditor.exe DovaStashEditor.com)
	if(DEFINED EDITOR_DIR AND EXISTS "${EDITOR_DIR}/${_tool}")
		file(COPY_FILE "${EDITOR_DIR}/${_tool}" "${_stage}/Tools/${_tool}")
	else()
		list(APPEND _missing "Tools/${_tool} (build the editor: cmake --build --preset editor-release)")
	endif()
endforeach()
if(DEFINED GUIDE_PDF AND EXISTS "${GUIDE_PDF}")
	file(COPY_FILE "${GUIDE_PDF}" "${_stage}/Tools/DovaStashEditor-UserGuide.pdf")
else()
	list(APPEND _missing "Tools/DovaStashEditor-UserGuide.pdf (presskit/guide)")
endif()
if(_missing)
	string(REPLACE ";" "; " _missingText "${_missing}")
	if(PACKAGE_ALLOW_PARTIAL)
		message(WARNING "Package: left out ${_missingText} (PACKAGE_ALLOW_PARTIAL)")
	else()
		message(FATAL_ERROR "Package: missing ${_missingText}. Build them first, or configure with -DPACKAGE_ALLOW_PARTIAL=ON for a test zip.")
	endif()
endif()

set(_zip "${OUT_DIR}/DovaStash-${VERSION}.zip")
file(REMOVE "${_zip}")
file(GLOB _top RELATIVE "${_stage}" "${_stage}/*")
list(SORT _top)
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${_zip}" --format=zip ${_top}
	WORKING_DIRECTORY "${_stage}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "Package: zipping failed (${_rc})")
endif()
message(STATUS "Packaged ${_zip}")
