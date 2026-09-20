# Fix the pinned esp_linenoise basic-terminal reader without modifying the
# component-manager cache. Remove this patch when the dependency contains it.
idf_component_get_property(linenoise_dir espressif__esp_linenoise COMPONENT_DIR)
idf_component_get_property(linenoise_lib espressif__esp_linenoise COMPONENT_LIB)
set(linenoise_source "${linenoise_dir}/src/esp_linenoise.c")
file(READ "${linenoise_source}" linenoise_code)
set(bad_read [=[if (nread < 0) {
            exit_loop = true;
            count = nread;
            continue;
        }]=])
set(good_read [=[if (nread <= 0) {
            buffer[count] = '\0';
            return -1;
        }]=])
set(bad_end [=[buffer[count + 1] = '\0';]=])
set(good_end [=[buffer[count] = '\0';]=])
string(FIND "${linenoise_code}" "${bad_read}" read_index)
string(FIND "${linenoise_code}" "${bad_end}" end_index)
if(read_index LESS 0 OR end_index LESS 0)
    message(FATAL_ERROR "esp_linenoise changed; review PatchLinenoise.cmake before building")
endif()
string(REPLACE "${bad_read}" "${good_read}" linenoise_code "${linenoise_code}")
string(REPLACE "${bad_end}" "${good_end}" linenoise_code "${linenoise_code}")
set(patched_source "${CMAKE_CURRENT_BINARY_DIR}/esp_linenoise_fixed.c")
file(WRITE "${patched_source}" "${linenoise_code}")
get_target_property(linenoise_sources ${linenoise_lib} SOURCES)
list(REMOVE_ITEM linenoise_sources "src/esp_linenoise.c" "${linenoise_source}")
set_property(TARGET ${linenoise_lib} PROPERTY SOURCES "${linenoise_sources}")
target_sources(${linenoise_lib} PRIVATE "${patched_source}")
