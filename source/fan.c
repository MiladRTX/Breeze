#include "fan.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* libkernel exports. Weak so a firmware/SDK combination without them degrades
 * (sensors read as unavailable) instead of failing to load the plugin. */
extern int sceKernelGetCpuTemperature(int *cputemp) __attribute__((weak));
extern int sceKernelGetSocSensorTemperature(int sensor_id, int *out)
    __attribute__((weak));
extern int sceKernelGetCurrentFanDuty(uint16_t *out_duty,
                                      uint64_t *out_chassis_info)
    __attribute__((weak));

#define ICC_FAN_DEV "/dev/icc_fan"

/* _IOWR requests: the kernel copies the encoded size in and back out, so the
 * buffer must be at least that large (0x1C = 28 for 8F07/8F08) or stack
 * garbage goes to the ICC and the copy-out smashes the stack. */
#define ICC_FAN_SET 0xC01C8F07 /* autoservo object, 28 B */
#define ICC_FAN_GET 0xC01C8F08 /* same object, read */
#define ICC_FAN_EXT 0xC0148F09 /* channel state, 20 B */
#define ICC_FAN_BUF_SIZE 32
_Static_assert(ICC_FAN_BUF_SIZE >= ((ICC_FAN_SET >> 16) & 0x1fff) &&
                   ICC_FAN_BUF_SIZE >= ((ICC_FAN_EXT >> 16) & 0x1fff),
               "ICC buffer smaller than ioctl payload");

#define STOCK_PATH "/data/breeze/stock.conf"

static volatile int s_stock_c256 = FAN_STOCK_DEFAULT_C256;
static int s_stock_saved = -1;

void fan_read(fan_sample *out) {
    out->cpu_c = -1;
    out->soc_c = -1;
    out->duty_pct = -1;

    if (sceKernelGetCpuTemperature) {
        int t = 0;
        if (sceKernelGetCpuTemperature(&t) == 0 && t > 0 && t < 150)
            out->cpu_c = t;
    }
    if (sceKernelGetSocSensorTemperature) {
        int t = 0;
        if (sceKernelGetSocSensorTemperature(0, &t) == 0 && t > 0 && t < 150)
            out->soc_c = t;
    }
    if (sceKernelGetCurrentFanDuty) {
        uint16_t duty = 0;
        uint64_t chassis = 0;
        if (sceKernelGetCurrentFanDuty(&duty, &chassis) == 0)
            out->duty_pct = (int)(((uint32_t)duty * 100u + 512u) / 1024u);
    }
}

/* Plain syscalls only: also used from the crash handler and the guard. */
static int icc_ioctl(unsigned long req, unsigned char *buf) {
    int fd = open(ICC_FAN_DEV, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    int rc = ioctl(fd, req, buf);
    int saved = errno;
    close(fd);
    errno = saved;
    return rc < 0 ? -1 : 0;
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static void put_target(unsigned char *b, int c256) {
    b[4] = (unsigned char)(c256 & 0xff);
    b[5] = (unsigned char)(c256 >> 8 & 0xff);
    b[6] = 0;
    b[7] = 0;
}

/* A SET the ICC refuses can still return 0 from ioctl(): bytes 0..1 carry
 * its result, as after a GET. */
static int icc_set(unsigned char *b) {
    b[0] = b[1] = 0;
    if (icc_ioctl(ICC_FAN_SET, b) != 0 || b[0] || b[1])
        return -1;
    return 0;
}

int fan_icc_get(int sel, unsigned char out[FAN_ICC_OBJ]) {
    unsigned char b[ICC_FAN_BUF_SIZE] = {0};
    b[2] = (unsigned char)sel;
    if (icc_ioctl(ICC_FAN_GET, b) != 0 || b[0] || b[1])
        return -1; /* bytes 0..1: ICC result */
    for (int i = 0; i < FAN_ICC_OBJ; i++)
        out[i] = b[i];
    return 0;
}

int fan_target_c256(void) {
    unsigned char b[FAN_ICC_OBJ];
    if (fan_icc_get(0, b) != 0)
        return -1;
    uint32_t v = le32(b + 4);
    return v > 255u * 256u ? -1 : (int)v;
}

/* Keeps the firmware's other words: only the target changes. When the read
 * fails, the old zero-filled object goes out instead (what every Breeze up
 * to 1.6 sent; the hardware keeps its words either way). */
int fan_set_target_c256(int c256) {
    if (c256 < 0)
        c256 = 0;
    if (c256 > 100 * 256)
        c256 = 100 * 256;
    unsigned char b[ICC_FAN_BUF_SIZE] = {0};
    unsigned char cur[FAN_ICC_OBJ];
    if (fan_icc_get(0, cur) == 0)
        for (int i = 0; i < FAN_ICC_OBJ; i++)
            b[i] = cur[i];
    b[2] = 0;
    put_target(b, c256);
    return icc_set(b);
}

int fan_write_setpoint(int celsius_c) {
    if (celsius_c < 0)
        celsius_c = 0;
    if (celsius_c > 100)
        celsius_c = 100;
    return fan_set_target_c256(celsius_c * 256);
}

int fan_write_setpoint_fine(int c256) {
    return fan_set_target_c256(c256);
}

int fan_channels(fan_channel ch[FAN_CHANNELS]) {
    int n = 0;
    for (int sel = 0; sel < FAN_CHANNELS; sel++) {
        ch[sel].target_c256 = ch[sel].temp_c256 = ch[sel].duty_q10 = -1;
        unsigned char o[FAN_ICC_OBJ];
        if (fan_icc_get(sel, o) == 0) {
            uint32_t v = le32(o + 4);
            if (v <= 255u * 256u)
                ch[sel].target_c256 = (int)v;
        }
        unsigned char b[ICC_FAN_BUF_SIZE] = {0};
        b[2] = (unsigned char)sel;
        if (icc_ioctl(ICC_FAN_EXT, b) == 0 && !b[0] && !b[1]) {
            uint32_t d = le32(b + 12), t = le32(b + 16);
            if (d <= 1024)
                ch[sel].duty_q10 = (int)d;
            if (t <= 255u * 256u)
                ch[sel].temp_c256 = (int)t;
            n++;
        }
    }
    return n;
}

int fan_stock_c256(void) {
    return s_stock_c256;
}

/* The firmware's channel-0 targets are whole degrees (91 C, 86 C in one
 * pattern bank). 91 C is the ceiling: a higher value is never taken, and
 * restoring 91 C instead only means a little more fan. 77 C is what Breeze
 * 1.0-1.6 and other fan tools wrote, not a firmware value. */
int fan_stock_ok(int c256) {
    return c256 >= 60 * 256 && c256 <= FAN_STOCK_DEFAULT_C256 && c256 % 256 == 0 &&
           c256 != 77 * 256;
}

void fan_stock_load(void) {
    FILE *f = fopen(STOCK_PATH, "r");
    if (!f)
        return;
    char line[64];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "stock_c256=", 11)) {
            int v = atoi(line + 11);
            if (fan_stock_ok(v)) {
                s_stock_c256 = v;
                s_stock_saved = v;
            }
        }
    }
    fclose(f);
}

int fan_stock_save(int c256) {
    if (!fan_stock_ok(c256))
        return -1;
    s_stock_c256 = c256;
    const char *tmp = STOCK_PATH ".tmp";
    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    int bad = fprintf(f,
                      "# Firmware (stock) CPU fan target in 1/256 C, learned from "
                      "the console.\n# Delete to relearn.\nstock_c256=%d\n",
                      c256) < 0;
    if (fflush(f) != 0 || fsync(fileno(f)) != 0)
        bad = 1;
    if (fclose(f) != 0)
        bad = 1;
    if (bad || rename(tmp, STOCK_PATH) != 0) {
        unlink(tmp);
        return -1;
    }
    s_stock_saved = c256;
    return 0;
}

void fan_stock_use(int c256) {
    if (fan_stock_ok(c256))
        s_stock_c256 = c256;
}

int fan_stock_saved(void) {
    return s_stock_saved;
}

int fan_stock_known(void) {
    return s_stock_saved >= 0;
}

int fan_restore_stock_if_ours(int ours_c256, int prev_c256) {
    return fan_restore_if_ours(ours_c256, prev_c256, s_stock_c256);
}

int fan_restore_if_ours(int ours_c256, int prev_c256, int stock_c256) {
    if (!fan_stock_ok(stock_c256))
        stock_c256 = FAN_STOCK_DEFAULT_C256;
    unsigned char b[ICC_FAN_BUF_SIZE] = {0};
    b[2] = 0;
    if (icc_ioctl(ICC_FAN_GET, b) != 0 || b[0] || b[1])
        return -1;
    uint32_t cur = le32(b + 4);
    if ((ours_c256 < 0 || cur != (uint32_t)ours_c256) &&
        (prev_c256 < 0 || cur != (uint32_t)prev_c256))
        return 0;
    put_target(b, stock_c256);
    return icc_set(b) == 0 ? 1 : -1;
}
