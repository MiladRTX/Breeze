#include "guardctl.h"
#include "fan.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int g_lock_fd = -1;
volatile int g_last_c256 = -1;
volatile int g_prev_c256 = -1;

static pthread_mutex_t s_note_lock = PTHREAD_MUTEX_INITIALIZER;
/* in the file; -2 forces the first write */
static int s_noted = -2, s_noted_prev = -1, s_noted_stock = -1;

/* Caller holds s_note_lock. */
static void write_note(void) {
    int c256 = g_last_c256, prev = g_prev_c256, stock = fan_stock_c256();
    if (g_lock_fd < 0 ||
        (c256 == s_noted && prev == s_noted_prev && stock == s_noted_stock))
        return;
    char line[64];
    int n = snprintf(line, sizeof line, "%d %d %d %d\n", (int)getpid(), c256, stock,
                     prev);
    /* Overwrite, then trim: truncating first left a moment where the
     * file was empty and a new launch could read no pid. */
    if (pwrite(g_lock_fd, line, (size_t)n, 0) == n) {
        (void)ftruncate(g_lock_fd, n);
        s_noted = c256;
        s_noted_prev = prev;
        s_noted_stock = stock;
    }
}

void fc_lock_note_target(int c256) {
    if (c256 < 0)
        c256 = -1;
    pthread_mutex_lock(&s_note_lock);
    /* prev first: the crash handler may read the pair at any point */
    if (c256 < 0)
        g_prev_c256 = -1;
    else if (c256 != g_last_c256)
        g_prev_c256 = g_last_c256;
    g_last_c256 = c256;
    write_note();
    pthread_mutex_unlock(&s_note_lock);
}

void fc_lock_note_failed(int confirmed) {
    if (confirmed < 0)
        confirmed = -1;
    pthread_mutex_lock(&s_note_lock);
    if (confirmed != g_last_c256) {
        g_prev_c256 = g_last_c256;
        g_last_c256 = confirmed;
    }
    write_note();
    pthread_mutex_unlock(&s_note_lock);
}

void fc_lock_note_left(int c256, int prev) {
    pthread_mutex_lock(&s_note_lock);
    g_prev_c256 = prev < 0 ? -1 : prev;
    g_last_c256 = c256 < 0 ? -1 : c256;
    write_note();
    pthread_mutex_unlock(&s_note_lock);
}

/* Main thread only: threads start without an alternate stack and run the
 * handler on their own (a stack overflow there is left to the guard). */
static char s_altstack[64 * 1024];

/* Only async-signal-safe calls: no app_lock, no fc_log, no malloc. */
static void on_fatal(int sig) {
    int c256 = g_last_c256, prev = g_prev_c256;
    if (c256 >= 0 || prev >= 0)
        fan_restore_stock_if_ours(c256, prev);
    raise(sig); /* SA_RESETHAND: the default action ends the process */
}

void fc_crash_handler_install(void) {
    static const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS};
    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_sp = s_altstack;
    ss.ss_size = sizeof s_altstack;
    sigaltstack(&ss, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fatal;
    sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        sigaction(sigs[i], &sa, NULL);
}
