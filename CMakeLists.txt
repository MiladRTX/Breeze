cmake_minimum_required(VERSION 3.20)
project(Breeze C)

# Configure with the ps5-payload-sdk toolchain:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake

# The web panel is edited as web/panel.html and embedded as a byte array at
# build time (see cmake/embed_web.cmake).
set(BREEZE_WEB_HTML ${CMAKE_CURRENT_SOURCE_DIR}/web/panel.html)
set(BREEZE_WEB_C ${CMAKE_CURRENT_BINARY_DIR}/web_page.c)
add_custom_command(
    OUTPUT ${BREEZE_WEB_C}
    COMMAND ${CMAKE_COMMAND} -DIN=${BREEZE_WEB_HTML} -DOUT=${BREEZE_WEB_C}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    DEPENDS ${BREEZE_WEB_HTML} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    COMMENT "Embedding web/panel.html"
    VERBATIM)

add_executable(breeze
    source/main.c
    source/control.c
    source/app.c
    source/fan.c
    source/curve.c
    source/config.c
    source/preset.c
    source/history.c
    source/httpd.c
    ${BREEZE_WEB_C})
target_include_directories(breeze PRIVATE source)
target_compile_options(breeze PRIVATE -Wall -Os -ffunction-sections -fdata-sections)
target_link_options(breeze PRIVATE -Wl,--gc-sections)
target_link_libraries(breeze PRIVATE pthread kernel_sys)
set_target_properties(breeze PROPERTIES OUTPUT_NAME breeze.elf)
