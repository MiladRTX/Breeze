# Turns a file into a C byte array (NUL-terminated, length excludes the NUL)
# so assets can be edited as normal files. Run as a script:
#   cmake -DIN=<file> -DOUT=<file.c> [-DVAR=name] -P embed_web.cmake
# Defines `const char <VAR>[]` and `const size_t <VAR>Len` (VAR defaults to
# kWebPage, the panel).
if(NOT VAR)
  set(VAR kWebPage)
endif()
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR len "${hexlen} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# CMake regexes have no {n} quantifier, so spell out 20 bytes per line.
string(REPEAT "0x[0-9a-f][0-9a-f]," 20 row)
string(REGEX REPLACE "(${row})" "\\1\n" bytes "${bytes}")
get_filename_component(src "${IN}" NAME)
file(WRITE "${OUT}"
"/* Generated from ${src} by cmake/embed_web.cmake. Do not edit. */\n"
"#include <stddef.h>\n\n"
"const char ${VAR}[] = {\n${bytes}0x00};\n"
"const size_t ${VAR}Len = ${len};\n")
