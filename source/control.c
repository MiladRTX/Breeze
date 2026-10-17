#include "control.h"
#include "history.h"

#include <stdlib.h>
#include <unistd.h>

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
    int target = -1;
    int temp_ema = -1;
    int prev_duty = -1;
    int cooldown = 0;
    int duty_hist[7] = {0};
    int duty_n = 0;
    int dip_hold = 0;

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

        if (!enabled) {
            desired = -1;
            pending = -1;
            pending_n = 0;
            rewrite_tick = 0;
            target = -1;
            temp_ema = -1;
            prev_duty = -1;
            cooldown = 0;
            duty_n = 0;
            dip_hold = 0;
            continue;
        }
        if (temp_raw < 0)
            continue;

        /* ~4 s time constant. Only used to ignore dips: the demand takes the
         * larger of raw and smoothed temperature, so a rise is seen at once
         * (a lagging demand left the fan at 100% while the curve said 25%)
         * while a falling temperature still decays slowly. */
        temp_ema = (temp_ema < 0) ? temp_raw : (temp_ema * 3 + temp_raw) / 4;
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

        int new_target;
        if (temp_raw >= FC_SAFETY_TEMP_C) {
            new_target = FC_SAFETY_TARGET_C;
        } else if (s.duty_pct < 0) {
            /* no duty feedback: park at stock instead of guessing */
            new_target = target < 0 ? FC_STOCK_TARGET_C : target;
        } else {
            if (target < 0)
                target = FC_STOCK_TARGET_C;
            new_target = target;
            int err = desired - duty_f;
            /* Wider band on the "more fan" side: one 1 C step near the
             * console's floor jumps the duty ~20% (idle: 15% -> 35%), so a
             * small shortfall was chased into a 15-35% breathing cycle. */
            int adje = err > FC_CTL_DEADBAND_UP_PCT
                           ? err
                           : (err < -FC_CTL_DEADBAND_PCT ? err : 0);
            /* Dead-zone skip: a target above the current temp is "no action",
             * so walking 77 -> 76 -> ... toward the temp changes nothing and
             * costs a full cooldown per degree. When more fan is needed, jump
             * straight to just above the temp (still no-op, so no kick and no
             * cooldown) and let the next real step start acting.
             * Never during a dip (raw below the EMA): skipping there would
             * drag the target down after the falling temp, and when the temp
             * snaps back the target sits far below it - the 1.18 surge. */
            /* Downward moves (more fan) wait out the post-dip hold; upward
             * steps and the escape below stay live so a surge is still cut.
             * Rise gate, same idea: while the temp is climbing fast and the
             * target is already below it, the ICC is about to spin up on its
             * own and the duty just hasn't caught up yet. Stepping down then
             * stacks on that response (game launch: 23% -> 69%). */
            int rising = temp_raw >= temp_ema + FC_RISE_C;
            int hold_down =
                adje > 0 && (dip_hold > 0 || (rising && new_target < temp_raw));
            /* Park: a shortfall inside the deadband still pulls a wound-up
             * target back to just above the temp (FC_CTL_PARK_PCT), unless
             * the temp is dipping or climbing fast. */
            int park = adje == 0 && err >= FC_CTL_PARK_PCT && dip_hold == 0 &&
                       !rising;
            int skip = ((adje > 0 && !hold_down) || park) &&
                       temp_raw >= temp_ema && new_target > temp + 1;
            if (skip)
                new_target = temp + 1;
            /* Fast escape, the mirror case: a target parked just above a quiet
             * temp ends up far below it when the (spiky) CPU temp jumps, and
             * the ICC cools hard - observed 23% -> 80% with the curve at 28%.
             * Walking back up 1-2 C per cooldown took ~40 s, so when the fan
             * is well over the curve and the target is at or below the temp,
             * jump straight to just above it, ignoring the cooldown. This only
             * ever raises the target (quieter), then holds a full cooldown so
             * the lagging duty can settle before the next step.
             * Triggered on the raw duty, not the median: the median lags ~3 s
             * on a surge, and since the escape only makes the fan quieter a
             * one-sample false trigger costs at most a brief dip in duty. */
            int escape = desired - s.duty_pct < -FC_CTL_ESCAPE_PCT &&
                         new_target <= temp;
            if (escape) {
                new_target = temp + 1;
                cooldown = FC_CTL_COOLDOWN_TICKS;
            }
            /* Cooldown counts wall ticks, not just ticks spent off-target, so
             * a step is always followed by a fixed settle window. The escape
             * tick already moved and armed the cooldown. */
            if (escape) {
            } else if (cooldown > 0) {
                cooldown--;
            } else if (adje != 0 && !skip && !hold_down) {
                /* 2 C steps only when far off. Near the goal a 2 C step
                 * overshoots: the duty response to target is steep (one
                 * observed run went 23% -> 100% after a 1 C/2 s staircase). */
                int mag = abs(adje) > 24 ? 2 : 1;
                /* hold while the plant is already moving toward the target */
                int moving_our_way = adje > 0 ? trend >= 2 : trend <= -2;
                if (!moving_our_way || abs(adje) > 12) {
                    new_target += adje > 0 ? -mag : mag;
                    cooldown = FC_CTL_COOLDOWN_TICKS;
                }
            }
            /* No temp-relative clamping: a target above the current temp is
             * already "no action" (quietest). Clamping it down to follow a
             * dipping temp would FORCE cooling during dips - exactly wrong. */
            if (new_target > FC_TARGET_MAX_C)
                new_target = FC_TARGET_MAX_C;
            if (new_target < FC_TARGET_MIN_C)
                new_target = FC_TARGET_MIN_C;
        }

        int changed = new_target != target;
        target = new_target;
        if (changed || ++rewrite_tick >= 2) {
            rewrite_tick = 0;
            /* Written under the lock, and only if still enabled: the panel
             * turning the curve off restores the stock target under the same
             * lock, and a custom write landing after it would stick. */
            int still_on, rc = 0;
            app_lock(st);
            still_on = st->cfg.enabled && !st->stopping;
            if (still_on) {
                rc = fan_write_setpoint(target);
                st->current_setpoint = rc == 0 ? target : -1;
            }
            app_unlock(st);
            if (!still_on) {
                target = -1;
            } else if (rc == 0) {
                if (changed)
                    fc_log("ctl: target %dC (fan %d%% -> want %d%%, temp %dC)",
                           target, duty_f, desired, temp_raw);
            } else {
                target = -1;
                fc_log("ctl: write failed");
            }
        }
    }
    return NULL;
}
