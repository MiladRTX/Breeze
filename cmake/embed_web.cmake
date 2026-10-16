# Turns web/panel.html into a C byte array (kWebPage, NUL-terminated) so the
# page can be edited as plain HTML. Run as a script:
#   cmake -DIN=<panel.html> -DOUT=<web_page.c> -P embed_web.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR len "${hexlen} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# CMake regexes have no {n} quantifier, so spell out 20 bytes per line.
string(REPEAT "0x[0-9a-f][0-9a-f]," 20 row)
string(REGEX REPLACE "(${row})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUT}"
"/* Generated from web/panel.html by cmake/embed_web.cmake. Do not edit. */\n"
"#include <stddef.h>\n\n"
"const char kWebPage[] = {\n${bytes}0x00};\n"
"const size_t kWebPageLen = ${len};\n")
