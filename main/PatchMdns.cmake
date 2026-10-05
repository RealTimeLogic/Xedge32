# Allow the default hostname to be disabled while ba.createmdns names resolve.
# Pinned espressif/mdns 1.12.0 rejects empty primary names and drops all name
# queries without a primary. Patch build copies only, preserving managed hashes.
if(CONFIG_mDNS_ENABLED)
    idf_component_get_property(mdns_dir espressif__mdns COMPONENT_DIR)
    idf_component_get_property(mdns_lib espressif__mdns COMPONENT_LIB)
    function(xedge_mdns_patch source old new)
        set(original "${mdns_dir}/${source}.c")
        file(READ "${original}" code)
        string(FIND "${code}" "${old}" at)
        if(at LESS 0)
            message(FATAL_ERROR "mdns changed; review PatchMdns.cmake: ${source}")
        endif()
        string(REPLACE "${old}" "${new}" code "${code}")
        if(source STREQUAL "mdns_receive")
            # hostname_is_ours already checks the primary AND delegated names.
            set(primary_guard "                && !mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname())\n")
            string(FIND "${code}" "${primary_guard}" at)
            if(at LESS 0)
                message(FATAL_ERROR "mdns changed; review the hostname match in PatchMdns.cmake")
            endif()
            string(REPLACE "${primary_guard}" "" code "${code}")
        endif()
        set(patched "${CMAKE_CURRENT_BINARY_DIR}/${source}_xedge.c")
        file(WRITE "${patched}" "${code}")
        get_target_property(sources ${mdns_lib} SOURCES)
        list(REMOVE_ITEM sources "${source}.c" "${original}")
        set_property(TARGET ${mdns_lib} PROPERTY SOURCES "${sources}")
        target_sources(${mdns_lib} PRIVATE "${patched}")
    endfunction()
    # Empty string disables the primary; NULL remains invalid. Retaining an
    # allocated empty string also preserves internal non-NULL assumptions.
    set(hostname_check [=[esp_err_t mdns_hostname_set(const char *hostname)
{
    if (!s_server) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mdns_utils_str_null_or_empty(hostname) || strlen(hostname) > (MDNS_NAME_BUF_LEN - 1))]=])
    string(REPLACE "mdns_utils_str_null_or_empty(hostname)" "hostname == NULL"
        hostname_check_fixed "${hostname_check}")
    xedge_mdns_patch(mdns_responder "${hostname_check}" "${hostname_check_fixed}")
    # Delegated browser hostnames need no primary hostname or service records.
    xedge_mdns_patch(mdns_receive
        "if (header.questions && !header.answers && mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname()))"
        "if (header.questions && !header.answers && mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname()) && !mdns_priv_get_hosts())")
endif()
