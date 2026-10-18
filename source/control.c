#include "control.h"
#include "guardctl.h"
#include "history.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define C256(c) ((c) * 256) /* whole degrees -> 1/256 C */

volatile sig_atomic_t g_running = 1;

/* Median of the last n duty samples. Single-second duty transients (observed:
 * 33 % -> 70 % -> 25 % at a steady temperature) must not drive target steps. */
static int median_duty(const int *v, int n) {
    int a[7];
    for (int i = 0; i < n; i++)
        a[i] = v[i];
    for (int i = 1; i < n; i++) {
        int k = a[i], j = i - 1;
        while (j >= 0 && a[j] > k) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = k;
    }
    return a[n / 2];
}

/* The four servo channels, every other tick. The console's minimum fan
 * speed (13% in the menus, 23% while a game runs) is what a channel asks
 * for while it sits well below its own target, and at least one channel
 * (the intake one, target 100 C) always idles there. */
static void read_channels(app_state *st) {
    fan_channel ch[FAN_CHANNELS];
    if (fan_channels(ch) == 0)
        return;
    int floor_q10 = -1;
    for (int i = 0; i < FAN_CHANNELS; i++)
        if (ch[i].duty_q10 >= 0 && ch[i].temp_c256 >= 0 && ch[i].target_c256 >= 0 &&
            ch[i].temp_c256 < ch[i].target_c256 - C256(3) &&
            (floor_q10 < 0 || ch[i].duty_q10 < floor_q10))
            floor_q10 = ch[i].duty_q10;
    app_lock(st);
    memcpy(st->ch, ch, sizeof ch);
    if (floor_q10 >= 0)
        st->console_min = (floor_q10 * 100 + 512) / 1024;
    app_unlock(st);
}

/* Stock learning. The firmware loads its own channel-0 target (91 C on a
 * CFI-2116, 86 C in one pattern bank) when a game starts or closes; Breeze
 * sees that as the target changing away from what it wrote (a reset). It
 * only does that on a pattern switch, so a value that comes back within
 * seconds is another tool writing the fan (drakmor's fan_target re-asserts
 * 85 C every 2 s), and so is one the firmware never uses (fan_stock_ok). A
 * reset that is neither is a sighting: the firmware's value for the current
 * pattern, used as this session's stock; two in a row with the same value
 * go to stock.conf (which a firmware update can change). The value read at
 * start is not one: it can be a leftover from another tool or a killed
 * Breeze. Ticks are about a second (sleep(1) plus the reads). */
#define RESET_GAP_TICKS 30
#define OTHER_LOG_TICKS 60

static int s_reset_tick = -1; /* tick of the previous reset */
static int s_other_tick = -1; /* last "something else" line */
static int s_seen = -1, s_seen_n; /* last sighting, how many in a row */

/* 1 if this reset is a sighting; 2 if it is also the one to save. */
static int reset_seen(int c256, int tick) {
    int gap = s_reset_tick < 0 || tick - s_reset_tick >= RESET_GAP_TICKS;
    s_reset_tick = tick;
    if (!gap || !fan_stock_ok(c256)) {
        if (s_other_tick < 0 || tick - s_other_tick >= OTHER_LOG_TICKS) {
            fc_log("ctl: something else keeps setting the fan target (%d.%02dC)",
                   c256 / 256, (c256 % 256) * 100 / 256);
            s_other_tick = tick;
        }
        return 0;
    }
    fc_log("ctl: the console reset the fan target to %d.%02dC", c256 / 256,
           (c256 % 256) * 100 / 256);
    s_seen_n = c256 == s_seen ? s_seen_n + 1 : 1;
    s_seen = c256;
    return s_seen_n >= 2 && c256 != fan_stock_saved() ? 2 : 1;
}

int fc_fan_restore_locked(app_state *st) {
    int ours = g_last_c256, prev = g_prev_c256; /* what breeze.lock says */
    int r = 0;
    if (ours >= 0 || prev >= 0) {
        r = fan_restore_stock_if_ours(ours, prev);
        if (r < 0) /* couldn't read or write: write it */
            r = fan_set_target_c256(fan_stock_c256()) == 0 ? 1 : -1;
    }
    st->current_setpoint = -1;
    /* A failed restore keeps the record: the next tick, the exit path, the
     * guard or the crash handler try again. */
    if (r >= 0) {
        st->last_c256 = -1;
        fc_lock_note_target(-1);
    }
    return r;
}

/* Closed loop: every second, read temp + real fan duty, look up the desired
 * duty, and steer the ICC target up/down until they match. The ICC target is
 * a temperature the firmware controller cools toward; lower target = more
 * fan. Values above the current temperature mean "no action" (stock).
 * The demand uses an EMA-smoothed temperature (bursty loads must not jerk
 * it), and stepping is trend-gated with cooldowns because the ICC's duty
 * response lags several seconds and arrives steep. */
void *fc_control_loop(void *arg) {
    app_state *st = arg;
    int rewrite_tick = 0;
    int desired = -1;
    int pending = -1;
    int pending_n = 0;
    int target = -1; /* 1/256 C, -1 = not set */
    int ema_x16 = -1; /* smoothed temp, 1/16 C */
    int prev_duty = -1;
    int cooldown = 0;
    int duty_hist[7] = {0};
    int duty_n = 0;
    int dip_hold = 0;
    int tick = 0;
    int retry_logged = 0;

    /* What the ICC holds before Breeze writes anything: the firmware's
     * stock target, unless another tool (or a killed Breeze) left one. */
    int boot = fan_target_c256();
    if (boot >= 0)
        fc_log("ctl: fan target at start %d.%02dC, stock %s %d.%02dC", boot / 256,
               (boot % 256) * 100 / 256, fan_stock_known() ? "learned" : "assumed",
               fan_stock_c256() / 256, (fan_stock_c256() % 256) * 100 / 256);

    while (g_running) {
        sleep(1);
        if (!g_running)
            break;

        fan_sample s;
        fan_read(&s);

        int enabled;
        app_lock(st);
        st->sample = s;
        enabled = st->cfg.enabled;
        if (!enabled)
            st->current_setpoint = -1;
        int temp_raw = s.cpu_c >= 0 ? s.cpu_c : s.soc_c;
        /* Same "wants" the panel shows live (curve at the raw temp). */
        int hist_want = enabled && temp_raw > 0 ? fc_evaluate(&st->cfg, temp_raw)
                                                : -1;
        app_unlock(st);
        fc_hist_record(temp_raw, s.duty_pct, hist_want);
        if (++tick % 2 == 0)
            read_channels(st);

        /* Read the target back: the firmware replaces it when a game starts
         * or closes, and then the stock value is in it. The other target in
         * breeze.lock is Breeze's own too (a write that reported failure
         * may still have taken): the fan holding it is no reset, it is the
         * target Breeze has in the fan. */
        int readback = fan_target_c256();
        int ours, other;
        app_lock(st);
        ours = st->last_c256;
        other = g_prev_c256;
        if (readback >= 0 && readback == other && readback != ours) {
            st->last_c256 = other;
            fc_lock_note_target(other);
        }
        app_unlock(st);
        int fw_reset = readback >= 0 && ours >= 0 && readback != ours && readback != other;
        if (fw_reset) {
            int seen = reset_seen(readback, tick);
            app_lock(st);
            if (seen)
                fan_stock_use(readback);
            if (st->last_c256 == ours) {
                st->last_c256 = -1;
                st->current_setpoint = -1;
            }
            fc_lock_note_target(st->last_c256); /* also records the stock in use */
            app_unlock(st);
            if (seen == 2 && fan_stock_save(readback) == 0)
                fc_log("ctl: learned the stock fan target, %d.%02dC", readback / 256,
                       (readback % 256) * 100 / 256);
        }

        if (!enabled) {
            desired = -1;
            pending = -1;
            pending_n = 0;
            rewrite_tick = 0;
            target = -1;
            ema_x16 = -1;
            prev_duty = -1;
            cooldown = 0;
            duty_n = 0;
            dip_hold = 0;
            /* Curve off but a target of Breeze's still noted: the restore
             * failed. Retried every tick until it is done. */
            app_lock(st);
            int r = 2; /* nothing to retry */
            if (!st->cfg.enabled && !st->stopping && (g_last_c256 >= 0 || g_prev_c256 >= 0))
                r = fc_fan_restore_locked(st);
            app_unlock(st);
            if (r < 0 && !retry_logged)
                fc_log("ctl: could not restore the stock target, retrying");
            else if (r == 1)
                fc_log("ctl: stock target restored on a retry");
            if (r != 2)
                retry_logged = r < 0;
            continue;
        }
        if (temp_raw < 0)
            continue;

        /* ~4 s time constant. Only used to ignore dips: the demand takes the
         * larger of raw and smoothed temperature, so a rise is seen at once
         * (a lagging demand left the fan at 100% while the curve said 25%)
         * while a falling temperature still decays slowly. */
        /* Kept in 1/16 C and rounded: the old whole-degree average
         * truncated, so after a climb it stalled up to 3 C under a steady
         * temp, and the rise gate (raw >= ema + 3) read "still rising" for
         * as long as that lasted, leaving the fan at the console's minimum. */
        ema_x16 = (ema_x16 < 0) ? temp_raw * 16
                                : ema_x16 + (temp_raw * 16 - ema_x16) / 4;
        int temp_ema = (ema_x16 + 8) / 16;
        int temp = temp_raw > temp_ema ? temp_raw : temp_ema;

        /* Post-dip hold. A loading-screen dip (raw well under the EMA) drops
         * the ICC to its floor, and once the temp is back the ICC takes ~8-10
         * s to bring the duty back up at an unchanged target. Stepping the
         * target down during that recovery stacked on top of it: observed
         * 23% -> 50% with the curve at 34%. The hold re-arms every dip tick,
         * so it counts from the end of the dip. */
        if (temp_raw <= temp_ema - FC_DIP_C) {
            if (dip_hold == 0)
                fc_log("ctl: dip %dC (ema %dC), holding", temp_raw, temp_ema);
            dip_hold = FC_DIP_HOLD_TICKS;
        } else if (dip_hold > 0)
            dip_hold--;

        int want;
        app_lock(st);
        want = fc_evaluate(&st->cfg, temp);
        app_unlock(st);

        /* Desired duty: move up immediately, back off only when persistent.
         * Any run of 3 lower readings counts, not 3 identical ones: an
         * interpolated curve on a noisy sensor wobbles a few percent per
         * tick, and requiring exact repeats pinned desired at its peak. The
         * backoff lands on the highest reading of the run (conservative). */
        if (desired < 0) {
            desired = want;
        } else if (want > desired) {
            desired = want;
            pending = -1;
            pending_n = 0;
        } else if (want < desired) {
            if (want > pending)
                pending = want;
            pending_n++;
            if (pending_n >= 3) {
                desired = pending;
                pending = -1;
                pending_n = 0;
            }
        } else {
            pending = -1;
            pending_n = 0;
        }

        /* Control runs on the median of the last 7 duty samples, not the raw
         * reading (see median_duty). 7 covers excursions up to 3 s. */
        int duty_f = s.duty_pct;
        if (s.duty_pct >= 0) {
            duty_hist[duty_n % 7] = s.duty_pct;
            duty_n++;
            duty_f = median_duty(duty_hist, duty_n < 7 ? duty_n : 7);
        }
        int trend = (prev_duty >= 0) ? duty_f - prev_duty : 0;
        prev_duty = duty_f;

        /* Targets are in 1/256 C (the ICC takes 8.8 fixed point: byte 4 is
         * the fraction), so steps can be a fraction of a degree. */
        int new_target;
        if (temp_raw >= FC_SAFETY_TEMP_C) {
            new_target = C256(FC_SAFETY_TARGET_C);
        } else if (s.duty_pct < 0) {
            /* no duty feedback: park at stock instead of guessing */
            new_target = target < 0 ? fan_stock_c256() : target;
        } else {
            if (target < 0)
                target = fan_stock_c256();
            new_target = target;
            int err = desired - duty_f;
            /* Small corrections are cheap now (a fraction of a degree), so the
             * band is narrow on both sides. With whole degrees one step near
             * the console's floor jumped the duty ~20% and the band had to be
             * +4/-8% to stop a 15-35% breathing cycle. */
            int adje = err > FC_CTL_DEADBAND_UP_PCT
                           ? err
                           : (err < -FC_CTL_DEADBAND_PCT ? err : 0);
            /* Dead-zone skip: a target above the current temp is "no action",
             * so walking it down toward the temp changes nothing. When more
             * fan is needed, jump straight to just above the temp (still
             * no-op, so no kick and no cooldown) and let the next real step
             * start acting. Never during a dip (raw below the EMA): skipping
             * there would drag the target down after the falling temp, and
             * when the temp snaps back the target sits far below it. */
            /* Downward moves (more fan) wait out the post-dip hold; upward
             * steps and the escape below stay live so a surge is still cut.
             * Rise gate, same idea: while the temp is climbing fast and the
             * target is already below it, the ICC is about to spin up on its
             * own and the duty just hasn't caught up yet. */
            int rising = temp_raw >= temp_ema + FC_RISE_C;
            int hold_down = adje > 0 && (dip_hold > 0 ||
                                         (rising && new_target < C256(temp_raw)));
            /* Park: any shortfall pulls a wound-up target back to just above
             * the temp, unless the temp is dipping or climbing fast. */
            int short_by = err >= FC_CTL_PARK_PCT ||
                           (err >= 1 && new_target > C256(temp + FC_CTL_WOUND_C));
            int park = adje == 0 && short_by && dip_hold == 0 && !rising;
            int skip = ((adje > 0 && !hold_down) || park) &&
                       temp_raw >= temp_ema && new_target > C256(temp + 1);
            if (skip)
                new_target = C256(temp + 1);
            /* Fast escape: a target left at or below a spiky CPU temp makes
             * the ICC cool hard (observed 23% -> 80% with the curve at 28%).
             * When the fan is well over the curve, jump straight to just
             * above the temp, ignoring the cooldown, then let it settle.
             * Raw duty, not the median, so it reacts on the first sample. */
            int escape = desired - s.duty_pct < -FC_CTL_ESCAPE_PCT &&
                         new_target <= C256(temp);
            if (escape) {
                new_target = C256(temp + 1);
                cooldown = FC_CTL_COOLDOWN_TICKS;
            }
            if (escape) {
            } else if (cooldown > 0) {
                cooldown--;
            } else if (adje != 0 && !skip && !hold_down) {
                /* Proportional step, about half of what the error needs: the
                 * duty moves ~8% per degree near the operating point. */
                int step = abs(adje) * 256 / FC_CTL_FINE_DIV;
                if (step < FC_CTL_FINE_MIN)
                    step = FC_CTL_FINE_MIN;
                if (step > FC_CTL_FINE_MAX)
                    step = FC_CTL_FINE_MAX;
                /* hold while the plant is already moving toward the target */
                int moving_our_way = adje > 0 ? trend >= 2 : trend <= -2;
                if (!moving_our_way || abs(adje) > 12) {
                    new_target += adje > 0 ? -step : step;
                    cooldown = FC_CTL_FINE_COOLDOWN_TICKS;
                }
            }
            /* No temp-relative clamping: a target above the current temp is
             * already "no action" (quietest). Clamping it down to follow a
             * dipping temp would FORCE cooling during dips - exactly wrong. */
            /* up to the stock target: the quietest Breeze is stock */
            int tmax = fan_stock_c256() > C256(FC_TARGET_MAX_C) ? fan_stock_c256()
                                                                 : C256(FC_TARGET_MAX_C);
            if (new_target > tmax)
                new_target = tmax;
            if (new_target < C256(FC_TARGET_MIN_C))
                new_target = C256(FC_TARGET_MIN_C);
        }

        int changed = new_target != target;
        target = new_target;
        /* Rewrite when the target moves or the read-back differs (the
         * firmware reset it); without a read-back, every 2 s as before. */
        int stale = readback >= 0 ? readback != target : ++rewrite_tick >= 2;
        if (changed || stale) {
            rewrite_tick = 0;
            /* Written under the lock, and only if still enabled: the panel
             * turning the curve off restores the stock target under the same
             * lock, and a custom write landing after it would stick. Noted
             * before the SET, so a kill between the two still finds it; a
             * failed SET keeps the previous target as Breeze's (the fan
             * still holds it) and the failed one as possible. */
            int still_on, rc = 0;
            app_lock(st);
            still_on = st->cfg.enabled && !st->stopping;
            if (still_on) {
                fc_lock_note_target(target);
                rc = fan_set_target_c256(target);
                st->current_setpoint = rc == 0 ? target : -1;
                if (rc == 0)
                    st->last_c256 = target;
                else
                    fc_lock_note_failed(st->last_c256);
            }
            app_unlock(st);
            if (!still_on) {
                target = -1;
            } else if (rc == 0) {
                if (changed)
                    fc_log("ctl: target %d.%02dC (fan %d%% -> want %d%%, temp %dC)",
                           target / 256, (target % 256) * 100 / 256, duty_f,
                           desired, temp_raw);
            } else {
                target = -1;
                fc_log("ctl: write failed");
            }
        }
    }
    return NULL;
}
