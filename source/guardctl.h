#pragma once

/* Fan safety for an exit Breeze doesn't get to run (Payload Manager's
 * SIGKILL, a crash). Either way the fan keeps Breeze's last target until a
 * game closes or starts, so the targets Breeze may have left in the fan are
 * kept where the crash handler and breeze-guard.elf can find them.
 *
 * breeze.lock holds "<pid> <c256> <stock> <prev>\n": the pid of the process
 * holding the lock (take_over reads it with atoi, so it stays first); the
 * channel-0 target Breeze is writing or wrote last, in 1/256 C; the stock
 * target it would restore (fan_stock_c256, which may be a value not saved
 * to stock.conf yet); and the target before c256. The new target is noted
 * before the SET, so a kill between the SET and the note can't leave one
 * nobody knows, and a write that may or may not have reached the fan keeps
 * both. Restore when the fan holds c256 or prev; -1 means none. Lines from
 * older builds have no prev ("<pid> <c256> <stock>"): read it as -1.
 * breeze-guard.elf puts its own pid in front of the same values while it
 * restores and writes "<pid> -1" once it is done, so a copy that starts
 * after an unguarded death, or has to stop a stalled guard, finds what is
 * left in the fan and keeps it in the line until it is dealt with (main.c). */

extern int g_lock_fd;            /* breeze.lock, set by main once it holds the lock */
extern volatile int g_last_c256; /* c256 above, -1 for none */
extern volatile int g_prev_c256; /* prev above, -1 for none */

/* Call before every target write with the target about to be written, with
 * -1 once nothing of Breeze's is in the fan (a restore, the firmware
 * replacing it), and under app_lock in the same critical section as the
 * write it records: notes taken outside it can land in the other order than
 * the writes reached the fan. A new target moves the previous one to prev;
 * -1 clears both. Rewrites breeze.lock only when the line changes (the stock
 * value included). */
void fc_lock_note_target(int c256);

/* After the SET of the target just noted failed: c256 goes back to
 * `confirmed` (the target before it, -1 if none) and prev keeps the failed
 * one, since an ICC error doesn't prove the fan didn't take it. */
void fc_lock_note_failed(int confirmed);

/* At start, with the targets the previous holder left in breeze.lock
 * (main.c): sets c256 and prev as they are (-1 for none), so the line keeps
 * them under this copy's pid until they are dealt with. Again with the same
 * pair, it only brings the stock value in the line up to date. */
void fc_lock_note_left(int c256, int prev);

/* SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT and SIGSYS put the stock target
 * back (if the fan still holds g_last_c256 or g_prev_c256) before the
 * process dies. */
void fc_crash_handler_install(void);
