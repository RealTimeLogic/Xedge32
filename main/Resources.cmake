# Resolve the shared main directory, including the full-SDK S3/P4 links.
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()
get_filename_component(xedge_main "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
get_filename_component(xedge_source "${xedge_main}" DIRECTORY)
set(ZIP_FILE_NAME "$ENV{ZIP_FILE_NAME}")
if(NOT ZIP_FILE_NAME)
    set(ZIP_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/resources/XedgeZip.c")
    set(xedge_psa 0)
    set(xedge_opcua 0)
    set(xedge_ota 1)
    if(CONFIG_XEDGE_PSA)
        set(xedge_psa 1)
    endif()
    if(CONFIG_OPCUA_ENABLED)
        set(xedge_opcua 1)
    endif()
    if(IDF_TARGET STREQUAL "esp32")
        set(xedge_ota 0)
    endif()
    file(GLOB_RECURSE xedge_resource_inputs
        "${xedge_main}/BAS-Resources/src/*" "${xedge_source}/.lua/*")
    add_custom_command(OUTPUT "${ZIP_FILE_NAME}"
        COMMAND "${PYTHON}" "${xedge_source}/Build-Resources/build_resources.py"
            --source "${xedge_source}" --output "${ZIP_FILE_NAME}"
            --psa ${xedge_psa} --opcua ${xedge_opcua} --ota ${xedge_ota}
        DEPENDS ${xedge_resource_inputs} "${SDKCONFIG}"
            "${CMAKE_CURRENT_LIST_FILE}"
            "${xedge_source}/Build-Resources/build_resources.py"
            "${xedge_main}/BAS-Resources/build/Xedge.sh"
        VERBATIM)
endif()
add_custom_target(xedge-resources DEPENDS "${ZIP_FILE_NAME}")
