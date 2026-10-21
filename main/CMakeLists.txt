if(NOT CMAKE_BUILD_EARLY_EXPANSION)
get_filename_component(passport_project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Optional per-computer defaults stay in ignored .local/; no secrets in CMake arguments.
set(passport_private_input "${passport_project_root}/.local/passport-defaults.json")
set(passport_private_header "${CMAKE_CURRENT_BINARY_DIR}/passport_private_defaults.h")
execute_process(
    COMMAND "${PYTHON}" "${passport_project_root}/tools/generate_private_defaults.py"
            --input "${passport_private_input}" --output "${passport_private_header}"
    RESULT_VARIABLE passport_private_result)
if(NOT passport_private_result EQUAL 0)
    message(FATAL_ERROR "Unable to prepare private Passport defaults")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${passport_private_input}" "${passport_project_root}/tools/generate_private_defaults.py")

endif()

set(passport_muse_sources "passport_muse.c" "passport_muse_voice.c")
# Dependencies must be visible during IDF early expansion, before CONFIG values
# exist. Disabled Bluetooth compiles no runtime; source inclusion stays optional.
set(passport_muse_requires bt efuse esp_hw_support esp_app_format passport_opus)
if(CONFIG_PASSPORT_MUSE_BLE)
    list(APPEND passport_muse_sources
        "muse/setup_controller.c" "muse/ble_server.c" "muse/link_pairing.c"
        "muse/identity.c" "muse/sdk_token_store.c" "muse/config_store.c"
        "muse/pairing_signer_policy.c" "muse/pairing_transcript.c"
        "muse/stack_monitor.c" "muse/phone_bridge.c" "muse/muse_adpcm.c" "muse/voice_turn.c" "muse/muse_speech.c" "muse/muse_speech_buffer.c")
endif()

idf_component_register(
    SRCS ${passport_muse_sources} "main.c" "passport_model.c" "passport_usage.c" "passport_i18n.c" "passport_ui.c" "passport_network.c" "passport_direct.c" "passport_public_data.c" "passport_train.c" "passport_train_fetch.c" "passport_voice.c" "passport_reply.c" "passport_photo.c"
         "passport_photo_pack.c" "passport_traffic_images.c" "../assets/fonts/passport_font_16.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash esp_http_server esp_http_client json esp_partition mbedtls esp_driver_usb_serial_jtag ${passport_muse_requires}
    EMBED_TXTFILES "passport_settings.html"
    EMBED_FILES "../assets/music/traffic-crossing.pcm"
)
