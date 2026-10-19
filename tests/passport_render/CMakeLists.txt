cmake_minimum_required(VERSION 3.16)
project(passport_render C)
get_filename_component(PASSPORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "")
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "")
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "")
add_subdirectory(${PASSPORT_ROOT}/managed_components/lvgl__lvgl lvgl)
add_executable(render ../test_passport_render.c ${PASSPORT_ROOT}/main/passport_ui.c
    ${PASSPORT_ROOT}/main/passport_model.c ${PASSPORT_ROOT}/main/passport_i18n.c
    ${PASSPORT_ROOT}/main/passport_photo_pack.c ${PASSPORT_ROOT}/assets/fonts/passport_font_16.c)
target_include_directories(render PRIVATE ${PASSPORT_ROOT}/main ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(render PRIVATE lvgl m)
