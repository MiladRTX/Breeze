#pragma once

/* The home screen tile, shared by breeze.elf (to check whether the tile is
 * current) and breeze-tile.elf (to write it), so both agree byte for byte. */
#include <stdio.h>

#define TILE_TITLE_ID "BREZ00001"
#define TILE_APP_DIR "/user/app/" TILE_TITLE_ID
#define TILE_SYS_DIR TILE_APP_DIR "/sce_sys"

/* param.json for a web launcher (category 65536) opening the panel. */
static inline int tile_param(char *buf, size_t n, int port) {
    return snprintf(buf, n,
        "{\n"
        "    \"titleId\": \"" TILE_TITLE_ID "\",\n"
        "    \"applicationCategoryType\": 65536,\n"
        "    \"deeplinkUri\": \"http://127.0.0.1:%d/\",\n"
        "    \"localizedParameters\": {\n"
        "        \"defaultLanguage\": \"en-US\",\n"
        "        \"en-US\": { \"titleName\": \"Breeze\" }\n"
        "    }\n"
        "}\n", port);
}
