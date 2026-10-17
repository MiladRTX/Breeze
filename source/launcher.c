#include "launcher.h"
#include "app.h"
#include "tile_param.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define ELFLDR_PORT 9021

extern const char kIcon0Png[];
extern const size_t kIcon0PngLen;
extern const char kTileElf[];
extern const size_t kTileElfLen;

/* 1 when the file at `path` is missing or differs from `data`. */
static int differs(const char *path, const void *data, size_t len) {
    struct stat st;
    if (stat(path, &st) != 0 || (size_t)st.st_size != len)
        return 1;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 1;
    char *buf = malloc(len ? len : 1);
    int diff = !buf || fread(buf, 1, len, f) != len || memcmp(buf, data, len);
    free(buf);
    fclose(f);
    return diff;
}

/* Sends an ELF to the loader on this console, as Payload Manager does. */
static int send_to_loader(const char *elf, size_t len) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(ELFLDR_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, elf + off, len - off, MSG_NOSIGNAL);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    close(fd);
    return off == len ? 0 : -1;
}

int fc_launcher_ensure(int port) {
    char param[512];
    int plen = tile_param(param, sizeof param, port);
    if (!differs(TILE_SYS_DIR "/param.json", param, (size_t)plen) &&
        !differs(TILE_SYS_DIR "/icon0.png", kIcon0Png, kIcon0PngLen))
        return 0;

    if (send_to_loader(kTileElf, kTileElfLen) != 0) {
        fc_log("launcher: could not reach the ELF loader on port %d", ELFLDR_PORT);
        return -1;
    }
    fc_log("launcher: tile missing or outdated, installer started");
    return 1;
}
