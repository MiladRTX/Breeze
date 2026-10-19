#include "httpd.h"
#include "config.h"
#include "history.h"
#include "web_page.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define HTTP_REQ_MAX 16384
#define HTTP_BODY_MAX 4096
#define HTTPD_POLL_MS 500
/* Whole-request read budget. The server handles one connection at a time,
 * so an idle or trickling client must not hold the panel for long. */
#define HTTPD_READ_MS 1500

static pthread_t g_thread;
static volatile int g_running;
static int g_started;
static int g_listen_fd = -1;
static app_state *g_state;


static void send_all(int fd, const char *buf, size_t len) {
    while (len) {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if (n <= 0)
            return;
        buf += (size_t)n;
        len -= (size_t)n;
    }
}

static void send_resp(int fd, const char *status, const char *ctype,
                      const char *body, size_t blen) {
    char head[256];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\nCache-Control: no-store\r\n\r\n",
                     status, ctype, blen);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof head)
        n = (int)sizeof head - 1;
    send_all(fd, head, (size_t)n);
    if (body && blen)
        send_all(fd, body, blen);
}

/* The page carries the write token so the panel can POST; cross-origin
 * pages cannot read the response, so only the panel itself learns it. */
static void send_page(int fd) {
    char token[FC_TOKEN_LEN + 1];
    const char *mark = strstr(kWebPage, FC_WEB_TOKEN_MARK);

    app_lock(g_state);
    memcpy(token, g_state->cfg.token, sizeof token);
    app_unlock(g_state);

    if (!mark) {
        send_resp(fd, "200 OK", "text/html; charset=utf-8", kWebPage,
                  kWebPageLen);
        return;
    }
    size_t pre = (size_t)(mark - kWebPage);
    size_t mlen = sizeof FC_WEB_TOKEN_MARK - 1;
    size_t tlen = strlen(token);
    size_t blen = kWebPageLen - mlen + tlen;
    char head[256];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html; "
                     "charset=utf-8\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\nCache-Control: no-store\r\n\r\n",
                     blen);
    if (n < 0 || (size_t)n >= sizeof head)
        return;
    send_all(fd, head, (size_t)n);
    send_all(fd, kWebPage, pre);
    send_all(fd, token, tlen);
    send_all(fd, mark + mlen, kWebPageLen - pre - mlen);
}

static void catf(char *buf, size_t cap, size_t *off, const char *fmt, ...) {
    if (*off >= cap - 1)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    *off += (size_t)n;
    if (*off > cap - 1)
        *off = cap - 1;
}

/* 1/256 C as a JSON number with two decimals, or -1. */
static const char *c256_str(int v, char *out, size_t n) {
    if (v < 0)
        snprintf(out, n, "-1");
    else
        snprintf(out, n, "%d.%02d", v / 256, (v % 256) * 100 / 256);
    return out;
}

static void handle_state(int fd) {
    curve_config cfg;
    fan_sample s;
    int setp;
    int desired = -1;
    int floor_state = 0;
    int cmin;
    fan_channel ch[FAN_CHANNELS];
    char buf[1536], a[16], b[16], latest[16];
    size_t off = 0;

    app_lock(g_state);
    cfg = g_state->cfg;
    snprintf(latest, sizeof latest, "%s", cfg.update_check ? g_state->latest : "");
    s = g_state->sample;
    setp = g_state->current_setpoint; /* 1/256 C */
    cmin = g_state->console_min;
    memcpy(ch, g_state->ch, sizeof ch);
    app_unlock(g_state);

    if (cfg.enabled) {
        int t = s.cpu_c >= 0 ? s.cpu_c : s.soc_c;
        if (t > 0) {
            desired = fc_evaluate(&cfg, t);
            if (setp >= 0 && setp >= (t + 5) * 256)
                floor_state = 1;
        }
    }

    catf(buf, sizeof buf, &off,
         "{\"ver\":\"%s\",\"enabled\":%d,\"web_enabled\":%d,\"web_port\":%d,"
         "\"cpu\":%d,\"soc\":%d,\"duty\":%d,\"desired\":%d,\"target\":%s,"
         "\"stock\":%s,\"floor\":%d,\"min\":%d,\"preset\":\"%s\","
         "\"update_check\":%d,\"latest\":\"%s\",\"ch\":[",
         FC_PLUGIN_VERSION, cfg.enabled ? 1 : 0, cfg.web_enabled ? 1 : 0,
         cfg.web_port, s.cpu_c, s.soc_c, s.duty_pct, desired,
         c256_str(setp, a, sizeof a), c256_str(fan_stock_c256(), b, sizeof b),
         floor_state, cmin, cfg.preset, cfg.update_check, latest);
    /* per servo channel: [target C, temperature C, duty %] */
    for (int i = 0; i < FAN_CHANNELS; i++) {
        char t[16], u[16];
        catf(buf, sizeof buf, &off, "%s[%s,%s,%d]", i ? "," : "",
             c256_str(ch[i].target_c256, t, sizeof t),
             c256_str(ch[i].temp_c256, u, sizeof u),
             ch[i].duty_q10 < 0 ? -1 : (ch[i].duty_q10 * 100 + 512) / 1024);
    }
    catf(buf, sizeof buf, &off, "],\"points\":[");
    for (int i = 0; i < cfg.points_count; i++)
        catf(buf, sizeof buf, &off, "%s[%d,%d]", i ? "," : "",
             cfg.points[i].temp_c, cfg.points[i].fan_pct);
    catf(buf, sizeof buf, &off, "]}\n");

    send_resp(fd, "200 OK", "application/json", buf, off);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s) {
    char *d = s;
    for (; *s; s++) {
        if (*s == '+') {
            *d++ = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            int hi = hexval(s[1]);
            int lo = hexval(s[2]);
            if (hi >= 0 && lo >= 0) {
                *d++ = (char)((hi << 4) | lo);
                s += 2;
            } else {
                *d++ = *s;
            }
        } else {
            *d++ = *s;
        }
    }
    *d = 0;
}

/* pN=temp,fan form fields. Only indices actually sent count, compacted in
 * index order, so a gap never becomes a fake 0/0 point. */
typedef struct {
    curve_point pts[FC_CURVE_MAX_POINTS];
    unsigned char seen[FC_CURVE_MAX_POINTS];
} form_points;

static void form_point(form_points *fp, const char *key, const char *val) {
    if (key[0] != 'p' || !isdigit((unsigned char)key[1]))
        return;
    int i = atoi(key + 1);
    int t = 0, s = 0;
    if (i < 0 || i >= FC_CURVE_MAX_POINTS || sscanf(val, "%d,%d", &t, &s) != 2)
        return;
    fp->pts[i].temp_c = t;
    fp->pts[i].fan_pct = s;
    fp->seen[i] = 1;
}

static int form_points_take(const form_points *fp, curve_point *out) {
    int n = 0;
    for (int i = 0; i < FC_CURVE_MAX_POINTS; i++)
        if (fp->seen[i])
            out[n++] = fp->pts[i];
    return n;
}

/* Returns -1 when the change is live but could not be written to /data.
 * Points sent with preset=<name> are an edit of that preset and are saved
 * into it (or into a new one, see fc_presets_store_edit), which becomes the
 * active preset; points without it (an older panel or app) leave no preset
 * active. */
static int apply_form(const char *body) {
    curve_config cfg;
    preset_store ps;
    form_points fp;
    char target[FC_PRESET_NAME_MAX + 1] = "";
    int has_target = 0;

    memset(&fp, 0, sizeof fp);
    app_lock(g_state);
    cfg = g_state->cfg;
    ps = g_state->presets;
    app_unlock(g_state);

    char tmp[HTTP_BODY_MAX + 1];
    snprintf(tmp, sizeof tmp, "%s", body);

    for (char *tok = strtok(tmp, "&"); tok; tok = strtok(NULL, "&")) {
        char *eq = strchr(tok, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = tok;
        char *val = eq + 1;
        url_decode(key);
        url_decode(val);
        if (!strcmp(key, "enabled")) {
            cfg.enabled = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "web_enabled")) {
            cfg.web_enabled = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "update_check")) {
            cfg.update_check = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "web_port")) {
            cfg.web_port = atoi(val);
        } else if (!strcmp(key, "preset")) {
            fc_preset_name_clean(target, val, sizeof target);
            has_target = 1;
        } else {
            form_point(&fp, key, val);
        }
    }

    /* Sent points replace the whole curve; a form without any (a toggle)
     * leaves it alone, whatever row count n= says. */
    int n = form_points_take(&fp, cfg.points);
    if (n > 0)
        cfg.points_count = n;
    fc_sanitize(&cfg);

    int presets_changed = 0;
    if (n > 0 && has_target)
        presets_changed = fc_presets_store_edit(&ps, target, cfg.points, cfg.points_count,
                                                cfg.preset, sizeof cfg.preset);
    else if (n > 0)
        cfg.preset[0] = 0;

    /* Swap and hand the fan back under the lock: the control thread writes
     * its target under the same lock after re-checking enabled, so it can't
     * slip a custom target in after the stock one. */
    app_lock(g_state);
    int was_enabled = g_state->cfg.enabled;
    g_state->cfg = cfg;
    if (presets_changed)
        g_state->presets = ps;
    g_state->ui_dirty = 1;
    if (!cfg.enabled && was_enabled)
        fc_fan_restore_locked(g_state);
    app_unlock(g_state);

    int rc = presets_changed ? fc_presets_save(&ps) : 0;
    return fc_config_save(&cfg) != 0 ? -1 : rc;
}

/* The active preset (cfg.preset), after a preset was used, renamed or
 * deleted. Returns fc_config_save's result. */
static int set_active_preset(const char *name) {
    curve_config cfg;
    app_lock(g_state);
    snprintf(g_state->cfg.preset, sizeof g_state->cfg.preset, "%s", name);
    cfg = g_state->cfg;
    g_state->ui_dirty = 1;
    app_unlock(g_state);
    return fc_config_save(&cfg);
}

static int do_restore(void) {
    curve_config cfg;

    app_lock(g_state);
    cfg = g_state->cfg;
    cfg.enabled = 0;
    g_state->cfg = cfg;
    g_state->ui_dirty = 1;
    fc_fan_restore_locked(g_state);
    app_unlock(g_state);

    return fc_config_save(&cfg);
}

static void send_save_failed(int fd) {
    const char msg[] = "applied, but saving to /data failed\n";
    send_resp(fd, "500 Internal Server Error", "text/plain", msg,
              sizeof msg - 1);
}

/* GET /api/history?s=<seconds>: averaged into at most 300 points. */
static void send_history(int fd, const char *query) {
    char buf[8192];
    int seconds = 300;
    const char *s = strstr(query, "s=");
    if (s && (s == query || s[-1] == '&'))
        seconds = atoi(s + 2);
    if (seconds < 60)
        seconds = 60;
    if (seconds > FC_HIST_SPAN_S)
        seconds = FC_HIST_SPAN_S;
    size_t n = fc_hist_json(buf, sizeof buf, seconds, 300);
    send_resp(fd, "200 OK", "application/json", buf, n);
}

static void send_presets(int fd) {
    preset_store ps;
    char buf[8192];
    size_t off = 0;

    app_lock(g_state);
    ps = g_state->presets;
    app_unlock(g_state);

    catf(buf, sizeof buf, &off, "[");
    for (int i = 0; i < ps.count; i++) {
        catf(buf, sizeof buf, &off,
             "%s{\"name\":\"%s\",\"points\":[", i ? "," : "", ps.list[i].name);
        for (int j = 0; j < ps.list[i].points_count; j++)
            catf(buf, sizeof buf, &off, "%s[%d,%d]", j ? "," : "",
                 ps.list[i].points[j].temp_c, ps.list[i].points[j].fan_pct);
        catf(buf, sizeof buf, &off, "]}");
    }
    catf(buf, sizeof buf, &off, "]\n");

    send_resp(fd, "200 OK", "application/json", buf, off);
}

static void handle_presets_post(const char *body, int fd) {
    char action[24] = {0};
    char name[FC_PRESET_NAME_MAX + 1] = {0};
    char to[FC_PRESET_NAME_MAX + 1] = {0}; /* new name for action=rename */
    curve_point pts[FC_CURVE_MAX_POINTS];
    form_points fp;
    int overwrite = 0;
    int use = 0; /* action=save: also run the saved curve, as the active preset */

    memset(&fp, 0, sizeof fp);

    char tmp[HTTP_BODY_MAX + 1];
    snprintf(tmp, sizeof tmp, "%s", body);
    for (char *tok = strtok(tmp, "&"); tok; tok = strtok(NULL, "&")) {
        char *eq = strchr(tok, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = tok;
        char *val = eq + 1;
        url_decode(key);
        url_decode(val);
        if (!strcmp(key, "action"))
            snprintf(action, sizeof action, "%s", val);
        else if (!strcmp(key, "name"))
            fc_preset_name_clean(name, val, sizeof name);
        else if (!strcmp(key, "to"))
            fc_preset_name_clean(to, val, sizeof to);
        else if (!strcmp(key, "overwrite"))
            overwrite = atoi(val) != 0;
        else if (!strcmp(key, "use"))
            use = atoi(val) != 0;
        else
            form_point(&fp, key, val);
    }
    memset(pts, 0, sizeof pts);
    int pts_count = form_points_take(&fp, pts);

    if (!strcmp(action, "apply")) {
        preset_store ps;
        int idx;
        app_lock(g_state);
        ps = g_state->presets;
        idx = fc_presets_find(&ps, name);
        app_unlock(g_state);
        if (idx < 0) {
            send_resp(fd, "404 Not Found", "text/plain", "no such preset\n",
                      15);
            return;
        }
        curve_config cfg;
        app_lock(g_state);
        cfg = g_state->cfg;
        cfg.points_count = ps.list[idx].points_count;
        memcpy(cfg.points, ps.list[idx].points, sizeof cfg.points);
        snprintf(cfg.preset, sizeof cfg.preset, "%s", ps.list[idx].name);
        fc_sanitize(&cfg);
        g_state->cfg = cfg;
        g_state->ui_dirty = 1;
        app_unlock(g_state);
        if (fc_config_save(&cfg) != 0)
            send_save_failed(fd);
        else
            send_presets(fd);
    } else if (!strcmp(action, "save")) {
        if (!name[0]) {
            send_resp(fd, "400 Bad Request", "text/plain", "name required\n",
                      14);
            return;
        }
        preset_store ps;
        curve_config cfg;
        app_lock(g_state);
        ps = g_state->presets;
        cfg = g_state->cfg;
        app_unlock(g_state);
        int idx = fc_presets_find(&ps, name);
        /* Replacing an existing preset has to be asked for explicitly. */
        if (idx >= 0 && !overwrite) {
            send_resp(fd, "409 Conflict", "text/plain", "name taken\n", 11);
            return;
        }
        if (idx >= 0 && fc_preset_is_builtin(name)) {
            const char msg[] = "the built-in preset can't be changed\n";
            send_resp(fd, "409 Conflict", "text/plain", msg, sizeof msg - 1);
            return;
        }
        if (idx < 0) {
            if (ps.count >= FC_PRESET_MAX) {
                send_resp(fd, "409 Conflict", "text/plain",
                          "preset limit reached\n", 21);
                return;
            }
            idx = ps.count++;
        }
        snprintf(ps.list[idx].name, sizeof ps.list[idx].name, "%s", name);
        /* The sent points, else the running curve. */
        if (pts_count > 0) {
            ps.list[idx].points_count = pts_count;
            memcpy(ps.list[idx].points, pts, sizeof ps.list[idx].points);
        } else {
            ps.list[idx].points_count = cfg.points_count;
            memcpy(ps.list[idx].points, cfg.points,
                   sizeof ps.list[idx].points);
        }
        fc_sanitize_points(&ps.list[idx].points_count, ps.list[idx].points);
        app_lock(g_state);
        g_state->presets = ps;
        if (use) {
            cfg = g_state->cfg;
            cfg.points_count = ps.list[idx].points_count;
            memcpy(cfg.points, ps.list[idx].points, sizeof cfg.points);
            snprintf(cfg.preset, sizeof cfg.preset, "%s", ps.list[idx].name);
            fc_sanitize(&cfg);
            g_state->cfg = cfg;
            g_state->ui_dirty = 1;
        }
        app_unlock(g_state);
        int failed = fc_presets_save(&ps) != 0;
        if (use && fc_config_save(&cfg) != 0)
            failed = 1;
        if (failed)
            send_save_failed(fd);
        else
            send_presets(fd);
    } else if (!strcmp(action, "delete")) {
        if (fc_preset_is_builtin(name)) {
            const char msg[] = "the built-in preset can't be deleted\n";
            send_resp(fd, "409 Conflict", "text/plain", msg, sizeof msg - 1);
            return;
        }
        preset_store ps;
        app_lock(g_state);
        ps = g_state->presets;
        app_unlock(g_state);
        int idx = fc_presets_find(&ps, name);
        if (idx < 0) {
            send_resp(fd, "404 Not Found", "text/plain", "no such preset\n",
                      15);
            return;
        }
        for (int i = idx; i + 1 < ps.count; i++)
            ps.list[i] = ps.list[i + 1];
        ps.count--;
        app_lock(g_state);
        g_state->presets = ps;
        int was_active = !strcmp(g_state->cfg.preset, name);
        app_unlock(g_state);
        /* Deleting the active preset leaves the curve running, in no preset. */
        int failed = fc_presets_save(&ps) != 0;
        if (was_active && set_active_preset("") != 0)
            failed = 1;
        if (failed)
            send_save_failed(fd);
        else
            send_presets(fd);
    } else if (!strcmp(action, "rename")) {
        if (!to[0]) {
            send_resp(fd, "400 Bad Request", "text/plain", "name required\n",
                      14);
            return;
        }
        if (fc_preset_is_builtin(name)) {
            const char msg[] = "the built-in preset can't be renamed\n";
            send_resp(fd, "409 Conflict", "text/plain", msg, sizeof msg - 1);
            return;
        }
        preset_store ps;
        app_lock(g_state);
        ps = g_state->presets;
        app_unlock(g_state);
        int idx = fc_presets_find(&ps, name);
        if (idx < 0) {
            send_resp(fd, "404 Not Found", "text/plain", "no such preset\n",
                      15);
            return;
        }
        int clash = fc_presets_find(&ps, to);
        if (clash >= 0 && clash != idx) {
            send_resp(fd, "409 Conflict", "text/plain", "name taken\n", 11);
            return;
        }
        snprintf(ps.list[idx].name, sizeof ps.list[idx].name, "%s", to);
        app_lock(g_state);
        g_state->presets = ps;
        int was_active = !strcmp(g_state->cfg.preset, name);
        app_unlock(g_state);
        int failed = fc_presets_save(&ps) != 0;
        if (was_active && set_active_preset(to) != 0)
            failed = 1;
        if (failed)
            send_save_failed(fd);
        else
            send_presets(fd);
    } else {
        send_resp(fd, "400 Bad Request", "text/plain", "bad action\n", 11);
    }
}

static char *find_ci(char *hay, const char *needle) {
    size_t nl = strlen(needle);
    for (char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) ==
                   tolower((unsigned char)needle[i]))
            i++;
        if (i == nl)
            return p;
    }
    return NULL;
}

/* Value of a request header, searched only inside the header block. Copies
 * at most cap-1 chars, stopping at CR/LF. Returns 0 when absent. */
static int header_value(char *buf, const char *hdr_end, const char *name,
                        char *out, size_t cap) {
    char key[64];
    snprintf(key, sizeof key, "\r\n%s:", name);
    char *h = find_ci(buf, key);
    if (!h || h >= hdr_end)
        return 0;
    h += strlen(key);
    while (*h == ' ' || *h == '\t')
        h++;
    size_t i = 0;
    while (i + 1 < cap && h[i] && h[i] != '\r' && h[i] != '\n')
        i++;
    memcpy(out, h, i);
    out[i] = 0;
    return 1;
}

/* POSTs must carry X-FC-Token. A custom header can't be sent cross-origin
 * without a CORS preflight, which this server never answers, so a random
 * website in a LAN browser can no longer rewrite the curve. */
static int token_ok(char *buf, const char *hdr_end) {
    char got[FC_TOKEN_LEN + 8];
    char want[FC_TOKEN_LEN + 1];

    app_lock(g_state);
    memcpy(want, g_state->cfg.token, sizeof want);
    app_unlock(g_state);

    if (!want[0] || !header_value(buf, hdr_end, "x-fc-token", got, sizeof got))
        return 0;
    size_t n = strlen(want);
    if (strlen(got) != n)
        return 0;
    unsigned diff = 0;
    for (size_t i = 0; i < n; i++)
        diff |= (unsigned)(got[i] ^ want[i]);
    return diff == 0;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* recv bounded by an absolute deadline shared by the whole request. */
static ssize_t recv_by(int fd, char *b, size_t cap, long long deadline) {
    long long left = deadline - now_ms();
    if (left <= 0)
        return -1;
    struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, (int)left) <= 0)
        return -1;
    return recv(fd, b, cap, 0);
}

static void handle_conn(int fd) {
    char buf[HTTP_REQ_MAX];
    size_t len = 0;
    char *hdr_end = NULL;
    long long deadline = now_ms() + HTTPD_READ_MS;

    for (;;) {
        if (len >= sizeof buf - 1)
            break;
        ssize_t n = recv_by(fd, buf + len, sizeof buf - 1 - len, deadline);
        if (n <= 0)
            break;
        len += (size_t)n;
        buf[len] = 0;
        hdr_end = strstr(buf, "\r\n\r\n");
        if (hdr_end)
            break;
    }
    if (!hdr_end) {
        const char bad[] = "bad request\n";
        send_resp(fd, "400 Bad Request", "text/plain", bad, sizeof bad - 1);
        return;
    }

    char method[8] = {0};
    char path[256] = {0};
    if (sscanf(buf, "%7s %255s", method, path) != 2) {
        const char bad[] = "bad request\n";
        send_resp(fd, "400 Bad Request", "text/plain", bad, sizeof bad - 1);
        return;
    }
    const char *query = "";
    char *qmark = strchr(path, '?');
    if (qmark) {
        *qmark = 0;
        query = qmark + 1;
    }

    char *body = hdr_end + 4;
    size_t body_len = len - (size_t)(body - buf);

    if (!strcmp(method, "POST")) {
        /* A cut-off or oversized body is refused, never applied in part:
         * a truncated form would silently drop curve points. */
        char clv[24] = {0};
        char *endp = clv;
        unsigned long content_length = 0;
        if (header_value(buf, hdr_end, "content-length", clv, sizeof clv))
            content_length = strtoul(clv, &endp, 10);
        if (endp == clv && clv[0]) {
            const char bad[] = "bad content-length\n";
            send_resp(fd, "400 Bad Request", "text/plain", bad, sizeof bad - 1);
            return;
        }
        if (content_length > HTTP_BODY_MAX) {
            const char big[] = "body too large\n";
            send_resp(fd, "413 Payload Too Large", "text/plain", big,
                      sizeof big - 1);
            return;
        }
        while (body_len < content_length && len < sizeof buf - 1) {
            ssize_t n = recv_by(fd, buf + len, sizeof buf - 1 - len, deadline);
            if (n <= 0)
                break;
            len += (size_t)n;
            buf[len] = 0;
            body_len = len - (size_t)(body - buf);
        }
        if (body_len < content_length) {
            const char bad[] = "incomplete body\n";
            send_resp(fd, "400 Bad Request", "text/plain", bad, sizeof bad - 1);
            return;
        }
        body[content_length] = 0;
    }

    if (!strcmp(method, "GET")) {
        if (!strcmp(path, "/"))
            send_page(fd);
        else if (!strcmp(path, "/api/state"))
            handle_state(fd);
        else if (!strcmp(path, "/api/presets"))
            send_presets(fd);
        else if (!strcmp(path, "/api/history"))
            send_history(fd, query);
        else
            send_resp(fd, "404 Not Found", "text/plain", "not found\n", 10);
    } else if (!strcmp(method, "POST")) {
        if (!token_ok(buf, hdr_end)) {
            const char bad[] = "bad token - reload the panel\n";
            send_resp(fd, "403 Forbidden", "text/plain", bad, sizeof bad - 1);
        } else if (!strcmp(path, "/api/config")) {
            if (apply_form(body) != 0)
                send_save_failed(fd);
            else
                handle_state(fd);
        } else if (!strcmp(path, "/api/restore")) {
            if (do_restore() != 0)
                send_save_failed(fd);
            else
                handle_state(fd);
        } else if (!strcmp(path, "/api/presets")) {
            handle_presets_post(body, fd);
        } else {
            send_resp(fd, "404 Not Found", "text/plain", "not found\n", 10);
        }
    } else {
        send_resp(fd, "405 Method Not Allowed", "text/plain", "no\n", 3);
    }
}

static void *httpd_thread(void *arg) {
    (void)arg;
    while (g_running) {
        struct pollfd pfd;
        pfd.fd = g_listen_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, HTTPD_POLL_MS);
        if (!g_running)
            break;
        if (pr <= 0)
            continue;
        int fd = accept(g_listen_fd, NULL, NULL);
        if (fd < 0)
            continue;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        /* Reads are bounded by HTTPD_READ_MS; this bounds a client that
         * stops reading the response. */
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        handle_conn(fd);
        close(fd);
    }
    return NULL;
}

int httpd_start(app_state *state) {
    if (g_started)
        return 0;

    int port;
    app_lock(state);
    port = state->cfg.web_port;
    app_unlock(state);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fc_log("web: socket failed (%d)", errno);
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fc_log("web: bind :%d failed (%d)", port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        fc_log("web: listen failed (%d)", errno);
        close(fd);
        return -1;
    }

    g_listen_fd = fd;
    g_state = state;
    g_running = 1;
    if (pthread_create(&g_thread, NULL, httpd_thread, NULL) != 0) {
        g_running = 0;
        close(fd);
        g_listen_fd = -1;
        return -1;
    }
    g_started = 1;
    fc_log("web: panel on :%d", port);
    return 0;
}

void httpd_stop(void) {
    if (!g_started)
        return;
    g_running = 0;
    /* The thread polls the listen fd with a timeout and rechecks g_running;
     * closing first would let it poll a closed (or reused) descriptor. */
    pthread_join(g_thread, NULL);
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    g_started = 0;
    fc_log("web: stopped");
}
