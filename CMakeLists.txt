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

project(xedge)
