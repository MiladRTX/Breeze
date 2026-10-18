#pragma once

typedef struct fan_sample {
    int cpu_c;    /* -1 when unavailable */
    int soc_c;    /* SoC sensor, the one OnionHEN labels "GPU" */
    int duty_pct; /* 0..100, -1 when unavailable */
} fan_sample;

void fan_read(fan_sample *out);

/* The ICC fan controller runs four servo channels; each has a target and
 * asks for a duty, and the fan runs at the highest ask. Channel 0 is the
 * CPU/APU one Breeze steers. Its object (28 bytes) is read with 8F08 and
 * written with 8F07: byte 2 selects the channel, bytes 4..7 hold the target
 * as a little-endian u32 in 1/256 C, the other words are firmware
 * parameters. Measured on a CFI-2116: stock target 91.00 C, reloaded by the
 * firmware when a game closes or starts, and kept otherwise. */
#define FAN_ICC_OBJ 28
#define FAN_CHANNELS 4
#define FAN_STOCK_DEFAULT_C256 (91 * 256)

int fan_icc_get(int sel, unsigned char out[FAN_ICC_OBJ]); /* 0 ok, -1 fail */
int fan_target_c256(void);         /* channel-0 target, -1 on failure */
/* GET, patch bytes 4..7, SET; 0 ok, -1 if the ioctl or the ICC result
 * (bytes 0..1) failed. */
int fan_set_target_c256(int c256);

/* Old names, same write. */
int fan_write_setpoint(int celsius_c);
int fan_write_setpoint_fine(int c256);

typedef struct fan_channel {
    int target_c256; /* -1 unknown */
    int temp_c256;   /* filtered temperature the servo works from, -1 unknown */
    int duty_q10;    /* duty this channel asks for (1024 = 100%), -1 unknown */
} fan_channel;
/* Reads all channels (8F08 + 8F09). Returns how many were read. */
int fan_channels(fan_channel ch[FAN_CHANNELS]);

/* Stock (firmware) channel-0 target: learned from the firmware's own
 * object and kept in /data/breeze/stock.conf; FAN_STOCK_DEFAULT_C256 until
 * then. fan_stock_c256 is a plain read, safe in a signal handler. */
int fan_stock_c256(void);
/* 1 if `c256` can be a stock target: whole degrees, 60 C up to
 * FAN_STOCK_DEFAULT_C256, not 77 C. Every stock value Breeze takes passes
 * this, so no restore writes a target hotter than 91 C. */
int fan_stock_ok(int c256);
void fan_stock_load(void);
int fan_stock_save(int c256); /* 0 ok; values fan_stock_ok refuses: -1 */
void fan_stock_use(int c256); /* same check, not saved: this session only */
int fan_stock_saved(void);    /* the value in stock.conf, -1 if none */
int fan_stock_known(void);    /* 1 once loaded from the file or saved */

/* If channel 0 still holds `ours_c256` or `prev_c256` (Breeze's last two
 * writes, -1 for none), write the stock target and return 1; 0 if the
 * firmware or someone else changed it since; -1 on error. Only
 * open/ioctl/close: async-signal-safe. */
int fan_restore_stock_if_ours(int ours_c256, int prev_c256);
/* Same with the stock target given (fan_stock_ok, or 91 C): breeze-guard.elf
 * restores the value breeze.elf was using, which may not be saved. */
int fan_restore_if_ours(int ours_c256, int prev_c256, int stock_c256);
