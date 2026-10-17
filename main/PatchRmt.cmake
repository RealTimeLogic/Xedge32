# IDF 6.1 can assert on invalid RMT dividers and carrier tick counts. Check
# these where the actual group clock is known, including S3/P4 prescaling.
# Compile patched build copies; never modify the user's installed ESP-IDF.
idf_component_get_property(rmt_dir esp_driver_rmt COMPONENT_DIR)
idf_component_get_property(rmt_lib esp_driver_rmt COMPONENT_LIB)

# Replace one exact source fragment and select that build copy in esp_driver_rmt.
# Inputs: source basename and old/new C fragments. No return value. A changed
# SDK fragment is a configure error so an upgrade cannot silently omit a guard.
function(xedge_rmt_patch source old new)
    set(original "${rmt_dir}/src/${source}.c")
    file(READ "${original}" code)
    string(FIND "${code}" "${old}" at)
    if(at LESS 0)
        message(FATAL_ERROR "RMT changed; review PatchRmt.cmake: ${source}")
    endif()
    string(REPLACE "${old}" "${new}" code "${code}")
    # Keep the object basename: IDF's linker.lf selects rmt_tx ISR functions
    # by that name when placing them in IRAM.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rmt_checked")
    set(patched "${CMAKE_CURRENT_BINARY_DIR}/rmt_checked/${source}.c")
    file(WRITE "${patched}" "${code}")
    get_target_property(sources ${rmt_lib} SOURCES)
    list(REMOVE_ITEM sources "src/${source}.c" "${original}")
    set_property(TARGET ${rmt_lib} PROPERTY SOURCES "${sources}")
    target_sources(${rmt_lib} PRIVATE "${patched}")
endfunction()

# rmt_select_periph_clock(): its independent-clock branch (classic ESP32)
# rounds source Hz / requested Hz. Reject dividers outside 1..256 before the
# LL assertion, for both TX and RX. ESP_ERR_INVALID_ARG follows the existing
# clock-release error path; no Lua stack is involved. S3/P4 already check this.
set(divider [=[    real_div = (group->resolution_hz + expect_channel_resolution / 2) / expect_channel_resolution;]=])
xedge_rmt_patch(rmt_common "${divider}" "${divider}
    ESP_GOTO_ON_FALSE(real_div >= 1 && real_div <= 256, ESP_ERR_INVALID_ARG, err, TAG, \"channel prescale out of range\");")

# rmt_tx_modulate_carrier(): config supplies duty cycle and carrier Hz; the
# existing driver treats NULL/zero Hz as disabled. For nonzero Hz, reject NaN,
# infinity and duty outside (0,1) before float-to-integer conversion. Then check
# both pulse lengths against the 16-bit counters (zero encodes 65536 in LL).
# Return ESP_ERR_INVALID_ARG before any carrier register changes; the Lua
# constructor's existing error path tears down its newly allocated channel.
set(carrier [=[        uint32_t total_ticks = group->resolution_hz / config->frequency_hz; // Note this division operation will lose precision
        uint32_t high_ticks = total_ticks * config->duty_cycle;
        uint32_t low_ticks = total_ticks - high_ticks;]=])
xedge_rmt_patch(rmt_tx "${carrier}" [=[        ESP_RETURN_ON_FALSE(config->duty_cycle > 0 && config->duty_cycle < 1,
                            ESP_ERR_INVALID_ARG, TAG, "invalid carrier duty cycle");
        uint32_t total_ticks = group->resolution_hz / config->frequency_hz;
        uint32_t high_ticks = total_ticks * config->duty_cycle;
        uint32_t low_ticks = total_ticks - high_ticks;
        ESP_RETURN_ON_FALSE(high_ticks >= 1 && high_ticks <= 65536 && low_ticks >= 1 && low_ticks <= 65536,
                            ESP_ERR_INVALID_ARG, TAG, "carrier high/low ticks out of range");]=])
