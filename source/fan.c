#include "fan.h"

#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
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

/* One-shot setpoint write, recovered from OnionHEN daemon_fan.cpp:
 * ioctl on /dev/icc_fan with a zeroed buffer, target temperature in byte 5,
 * value clamped 0..100. The firmware reasserts its own curve shortly after,
 * so the caller must rewrite periodically to hold a value.
 * The request is _IOWR with a 0x1C (28) byte payload: the kernel copies that
 * many bytes in and back out, so the buffer must be at least that large or
 * stack garbage goes to the ICC and the copy-out smashes the stack. */
#define ICC_FAN_IOCTL 0xC01C8F07
#define ICC_FAN_BUF_SIZE 32

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

int fan_write_setpoint(int celsius_c) {
    if (celsius_c < 0)
        celsius_c = 0;
    if (celsius_c > 100)
        celsius_c = 100;

    int fd = open(ICC_FAN_DEV, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    _Static_assert(ICC_FAN_BUF_SIZE >= ((ICC_FAN_IOCTL >> 16) & 0x1fff),
                   "ICC buffer smaller than ioctl payload");
    char data[ICC_FAN_BUF_SIZE] = {0};
    data[5] = (char)celsius_c;
    int rc = ioctl(fd, ICC_FAN_IOCTL, data);
    int saved = errno;
    close(fd);
    errno = saved;
    return rc < 0 ? -1 : 0;
}

int fan_write_setpoint_fine(int c256) {
    if (c256 < 0)
        c256 = 0;
    if (c256 > 100 * 256)
        c256 = 100 * 256;

    int fd = open(ICC_FAN_DEV, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    char data[ICC_FAN_BUF_SIZE] = {0};
    data[4] = (char)(c256 & 0xff);
    data[5] = (char)(c256 >> 8);
    int rc = ioctl(fd, ICC_FAN_IOCTL, data);
    int saved = errno;
    close(fd);
    errno = saved;
    return rc < 0 ? -1 : 0;
}
