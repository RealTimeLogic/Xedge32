# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# --- START CAMERA LOGIC ---
# We evaluate what target the idf.py is requesting.
if("$ENV{IDF_TARGET}" STREQUAL "esp32" OR "$ENV{IDF_TARGET}" STREQUAL "esp32s3")
    # If it's an S3 or Legacy, we tell it to read the generic one, the one from the CPU, and add the one from the camera.
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.camera;sdkconfig.defaults.$ENV{IDF_TARGET}")
endif()
# --- END CAMERA LOGIC ---

# --- START P4 COMPONENTS BYPASS ---
# WORKAROUND: The Component Manager currently throws compatibility errors if 
# P4-exclusive components (like 'espressif/esp_ipa') are scanned during an S3 build.
# This conditional hides them from the build system unless targeting esp32p4.
# TODO: Re-evaluate and potentially remove this bypass once Espressif releases 
# esp_ipa version > 2.0.0 with proper multi-target support in its manifest.
if("$ENV{IDF_TARGET}" STREQUAL "esp32p4")
    list(APPEND EXTRA_COMPONENT_DIRS "components_p4")
endif()
# --- END P4 COMPONENTS BYPASS ---

project(xedge)
