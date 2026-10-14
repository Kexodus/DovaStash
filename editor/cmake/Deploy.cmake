# Copies the editor into the MO2 mod's Tools folder after a build (editor/CMakeLists.txt). A running editor
# locks its file, so a failed copy is a warning, not a failed build: the next build (or a plugin build) copies it.
if(NOT EXE OR NOT DEPLOY_DIR)
	message(FATAL_ERROR "Deploy.cmake wants -DEXE=<DovaStashEditor.exe> -DDEPLOY_DIR=<mod root>")
endif()
file(MAKE_DIRECTORY "${DEPLOY_DIR}/Tools")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${EXE}" "${DEPLOY_DIR}/Tools/" RESULT_VARIABLE result ERROR_QUIET OUTPUT_QUIET)
if(result EQUAL 0)
	message(STATUS "Deployed DovaStashEditor.exe to ${DEPLOY_DIR}/Tools")
else()
	message(WARNING "DovaStashEditor.exe not deployed to ${DEPLOY_DIR}/Tools (is the editor running?); the next build copies it")
endif()
