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

# Home screen tile icon, installed by source/tile.c (breeze-tile.elf).
set(BREEZE_ICON_PNG ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png)
set(BREEZE_ICON_C ${CMAKE_CURRENT_BINARY_DIR}/icon0.c)
add_custom_command(
    OUTPUT ${BREEZE_ICON_C}
    COMMAND ${CMAKE_COMMAND} -DIN=${BREEZE_ICON_PNG} -DOUT=${BREEZE_ICON_C}
            -DVAR=kIcon0Png -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    DEPENDS ${BREEZE_ICON_PNG} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    COMMENT "Embedding assets/icon0.png"
    VERBATIM)

# breeze.elf carries breeze-tile.elf and hands it to the local ELF loader
# when the home screen tile is missing or out of date (source/launcher.c).
set(BREEZE_TILE_C ${CMAKE_CURRENT_BINARY_DIR}/tile_elf.c)
add_custom_command(
    OUTPUT ${BREEZE_TILE_C}
    COMMAND ${CMAKE_COMMAND} -DIN=$<TARGET_FILE:breeze-tile> -DOUT=${BREEZE_TILE_C}
            -DVAR=kTileElf -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    DEPENDS breeze-tile ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    COMMENT "Embedding breeze-tile.elf"
    VERBATIM)

# Same for breeze-guard.elf, which restores the stock fan target if
# breeze.elf is killed hard or crashes (source/guard.c).
set(BREEZE_GUARD_C ${CMAKE_CURRENT_BINARY_DIR}/guard_elf.c)
add_custom_command(
    OUTPUT ${BREEZE_GUARD_C}
    COMMAND ${CMAKE_COMMAND} -DIN=$<TARGET_FILE:breeze-guard> -DOUT=${BREEZE_GUARD_C}
            -DVAR=kGuardElf -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    DEPENDS breeze-guard ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
    COMMENT "Embedding breeze-guard.elf"
    VERBATIM)

add_executable(breeze
    source/main.c
    source/launcher.c
    ${BREEZE_TILE_C}
    ${BREEZE_ICON_C}
    source/guardctl.c
    ${BREEZE_GUARD_C}
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

# One-shot home screen tile installer (see source/tile.c). Linked against the
# app installer the same way ftpsrv's installer is: SceIpmi + SceAppInstUtil.
add_executable(breeze-tile source/tile.c ${BREEZE_ICON_C})
target_compile_options(breeze-tile PRIVATE -Wall -Os)
target_link_libraries(breeze-tile PRIVATE kernel_sys SceIpmi SceAppInstUtil)
set_target_properties(breeze-tile PROPERTIES OUTPUT_NAME breeze-tile.elf)

# Fan guard (see source/guard.c). It waits next to breeze.elf for as long as
# it runs, so: kernel_sys only, no threads.
add_executable(breeze-guard source/guard.c source/fan.c)
target_include_directories(breeze-guard PRIVATE source)
target_compile_options(breeze-guard PRIVATE -Wall -Os -ffunction-sections -fdata-sections)
target_link_options(breeze-guard PRIVATE -Wl,--gc-sections)
target_link_libraries(breeze-guard PRIVATE kernel_sys)
set_target_properties(breeze-guard PROPERTIES OUTPUT_NAME breeze-guard.elf)
