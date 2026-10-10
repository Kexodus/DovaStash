# Post-build deploy, run as: cmake -DDLL=... -DPDB=... -DDIST_DIR=... -DDEFAULT_INI=... -DDEPLOY_DIR=... -P Deploy.cmake
#
# Layout produced under DEPLOY_DIR (an MO2 mod folder, i.e. maps onto Skyrim's Data/):
#   SKSE/Plugins/DovaStash.dll, DovaStash.pdb
#   SKSE/Plugins/DovaStash.ini          only if it does not exist yet (never clobber user settings)
#   SKSE/Plugins/DovaStash/...          mirror of dist/Data/SKSE/Plugins/DovaStash (if present)

foreach(_var DLL DEPLOY_DIR)
	if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
		message(FATAL_ERROR "Deploy.cmake: ${_var} not set")
	endif()
endforeach()

set(_plugins "${DEPLOY_DIR}/SKSE/Plugins")
file(MAKE_DIRECTORY "${_plugins}")

file(COPY "${DLL}" DESTINATION "${_plugins}")
message(STATUS "Deployed ${DLL}")
if(DEFINED PDB AND EXISTS "${PDB}")
	file(COPY "${PDB}" DESTINATION "${_plugins}")
	message(STATUS "Deployed ${PDB}")
endif()

if(DEFINED DEFAULT_INI AND EXISTS "${DEFAULT_INI}")
	if(NOT EXISTS "${_plugins}/DovaStash.ini")
		file(COPY_FILE "${DEFAULT_INI}" "${_plugins}/DovaStash.ini")
		message(STATUS "Deployed default DovaStash.ini")
	else()
		message(STATUS "Kept existing ${_plugins}/DovaStash.ini")
	endif()
endif()

if(DEFINED DIST_DIR AND IS_DIRECTORY "${DIST_DIR}")
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
