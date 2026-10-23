# Copies the editor into the MO2 mod's Tools folder after a build (editor/CMakeLists.txt). A running editor
# locks its file, so a failed copy is a warning, not a failed build; the DovaStashEditor_deploy target retries it
# (a later build that relinks nothing does not).
if(NOT EXE OR NOT DEPLOY_DIR)
	message(FATAL_ERROR "Deploy.cmake wants -DEXE=<DovaStashEditor.exe> -DDEPLOY_DIR=<mod root>")
endif()
file(MAKE_DIRECTORY "${DEPLOY_DIR}/Tools")
get_filename_component(NAME "${EXE}" NAME)  # DovaStashEditor.exe, or the console companion DovaStashEditor.com
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${EXE}" "${DEPLOY_DIR}/Tools/" RESULT_VARIABLE result ERROR_QUIET OUTPUT_QUIET)
if(result EQUAL 0)
	message(STATUS "Deployed ${NAME} to ${DEPLOY_DIR}/Tools")
else()
	message(WARNING "${NAME} not deployed to ${DEPLOY_DIR}/Tools (is the editor running?); close it and run `cmake --build --preset editor-release --target DovaStashEditor_deploy`")
endif()
