/*
 * Breeze app - talking to breeze.elf, the background payload that runs the fan,
 * through the same HTTP API as the web panel (on 127.0.0.1).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "link.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace breeze::link
{
namespace
{
constexpr int default_port = 2878;
constexpr int loader_port = 9021;
constexpr const char *conf_path = "/data/breeze/breeze.conf";

char g_token[40];
char g_reply[131072]; // the panel page is the largest answer (about 80 KB)
size_t g_reply_len;

// -1 when nothing accepts the connection.
int connect_local(int port, int timeout_s) noexcept
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    timeval tv{timeout_s, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
    const int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    sockaddr_in a{};
#ifndef BREEZE_PREVIEW // BSD only; the PC preview builds on Linux
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<unsigned short>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<const sockaddr *>(&a), sizeof a) != 0)
    {
        (void)close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const void *data, size_t len) noexcept
{
    const auto *p = static_cast<const char *>(data);
    while (len > 0)
    {
        const ssize_t n = send(fd, p, len, 0);
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

// One request; the answer's body is left in g_reply. Returns the HTTP status,
// 0 when nothing listens, -1 for an answer that isn't HTTP.
int request(int port, const char *method, const char *path, const char *body) noexcept
{
    g_reply_len = 0;
    g_reply[0] = '\0';
    const int fd = connect_local(port, 3);
    if (fd < 0)
        return 0;
    char head[512];
    const size_t blen = body ? strlen(body) : 0;
    const int hlen = snprintf(head, sizeof head,
                              "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n"
                              "X-FC-Token: %s\r\n"
                              "Content-Type: application/x-www-form-urlencoded\r\n"
                              "Content-Length: %zu\r\n\r\n",
                              method, path, g_token, blen);
    size_t len = 0;
    if (hlen > 0 && static_cast<size_t>(hlen) < sizeof head && send_all(fd, head, hlen) &&
        (blen == 0 || send_all(fd, body, blen)))
    {
        while (len < sizeof g_reply - 1)
        {
            const ssize_t n = recv(fd, g_reply + len, sizeof g_reply - 1 - len, 0);
            if (n <= 0)
                break;
            len += static_cast<size_t>(n);
        }
    }
    (void)close(fd);
    g_reply[len] = '\0';

    int status = -1;
    if (strncmp(g_reply, "HTTP/1.", 7) == 0 && len > 12)
        status = (g_reply[9] - '0') * 100 + (g_reply[10] - '0') * 10 + (g_reply[11] - '0');
    const char *b = strstr(g_reply, "\r\n\r\n");
    if (!b)
        return -1;
    b += 4;
    g_reply_len = len - static_cast<size_t>(b - g_reply);
    memmove(g_reply, b, g_reply_len + 1);
    return status;
}

// Parses an integer at p (optional '-'); p moves past it.
bool read_int(const char *&p, int &out) noexcept
{
    const bool negative = *p == '-';
    const char *q = negative ? p + 1 : p;
    if (*q < '0' || *q > '9')
        return false;
    long v = 0;
    while (*q >= '0' && *q <= '9' && v < 100000000)
        v = v * 10 + (*q++ - '0');
    out = static_cast<int>(negative ? -v : v);
    p = q;
    return true;
}

// The value after "key": - times 100 (two decimals at most); false when there is none.
bool json_x100(const char *json, const char *key, int &out) noexcept
{
    char pattern[32];
    (void)snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return false;
    p += strlen(pattern);
    int whole = 0;
    const bool negative = *p == '-';
    if (!read_int(p, whole))
        return false;
    int frac = 0;
    if (*p == '.')
    {
        ++p;
        for (int digits = 0; digits < 2; ++digits)
            frac = frac * 10 + ((*p >= '0' && *p <= '9') ? *p++ - '0' : 0);
    }
    out = negative ? whole * 100 - frac : whole * 100 + frac;
    return true;
}

// -1 when missing (breeze.elf itself reports -1 for no value).
int json_int(const char *json, const char *key) noexcept
{
    int v = 0;
    return json_x100(json, key, v) ? v / 100 : -1;
}

// [[t,f],[t,f],...] at p into a curve; p moves past the closing bracket.
bool read_points(const char *&p, Curve &c) noexcept
{
    c.count = 0;
    if (*p != '[')
        return false;
    ++p;
    while (*p == '[' && c.count < max_points)
    {
        ++p;
        int t = 0, f = 0;
        if (!read_int(p, t) || *p++ != ',' || !read_int(p, f) || *p++ != ']')
            return false;
        c.temp[c.count] = t;
        c.fan[c.count] = f;
        ++c.count;
        if (*p == ',')
            ++p;
    }
    if (*p != ']')
        return false;
    ++p;
    return c.count > 0;
}

void read_conf(char *buf, size_t cap) noexcept
{
    buf[0] = '\0';
    const int fd = open(conf_path, O_RDONLY);
    if (fd < 0)
        return;
    const ssize_t n = read(fd, buf, cap - 1);
    (void)close(fd);
    buf[n > 0 ? n : 0] = '\0';
}

// The value of a "key=" line in breeze.conf, copied up to the end of the line.
bool conf_value(const char *conf, const char *key, char *out, size_t cap) noexcept
{
    const size_t klen = strlen(key);
    for (const char *line = conf; *line;)
    {
        if (strncmp(line, key, klen) == 0)
        {
            size_t i = 0;
            for (const char *p = line + klen; *p && *p != '\n' && *p != '\r' && i < cap - 1; ++p)
                out[i++] = *p;
            out[i] = '\0';
            return i > 0;
        }
        const char *nl = strchr(line, '\n');
        if (!nl)
            break;
        line = nl + 1;
    }
    return false;
}

void append(char *out, size_t cap, const char *text) noexcept
{
    const size_t n = strlen(out);
    (void)snprintf(out + n, cap - n, "%s", text);
}

void append_encoded(char *out, size_t cap, const char *text) noexcept
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(out);
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p && n + 4 < cap;
         ++p)
    {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.')
            out[n++] = static_cast<char>(*p);
        else
        {
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 15];
        }
    }
    out[n] = '\0';
}
} // namespace

int panel_port() noexcept
{
    static char conf[4096];
    read_conf(conf, sizeof conf);
    char value[16];
    if (!conf_value(conf, "web_port=", value, sizeof value))
        return default_port;
    int port = 0;
    for (const char *p = value; *p >= '0' && *p <= '9' && port < 100000; ++p)
        port = port * 10 + (*p - '0');
    return port > 0 && port < 65536 ? port : default_port;
}

bool load_token(int port) noexcept
{
    static char conf[4096];
    read_conf(conf, sizeof conf);
    if (conf_value(conf, "token=", g_token, sizeof g_token))
        return true;
    // No access to /data from the app: the panel page carries it.
    if (request(port, "GET", "/", nullptr) != 200)
        return false;
    const char *mark = strstr(g_reply, "var TK='");
    if (!mark)
        return false;
    mark += 8;
    size_t i = 0;
    while (mark[i] && mark[i] != '\'' && i < sizeof g_token - 1)
    {
        g_token[i] = mark[i];
        ++i;
    }
    g_token[i] = '\0';
    return i > 0;
}

namespace
{
// A string value ("key":"value") into out; "" when absent.
void json_str(const char *json, const char *key, char *out, size_t cap) noexcept
{
    char pat[32];
    (void)snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *v = strstr(json, pat);
    size_t i = 0;
    if (v)
        for (v += strlen(pat); v[i] && v[i] != '"' && i < cap - 1; ++i)
            out[i] = v[i];
    out[i] = '\0';
}

// The state document left in g_reply (GET /api/state, or a config POST's answer).
bool parse_state(State &out) noexcept
{
    if (!strstr(g_reply, "\"ver\":"))
        return false;
    State s{};
    json_str(g_reply, "ver", s.version, sizeof s.version);
    json_str(g_reply, "preset", s.preset, sizeof s.preset);
    s.enabled = json_int(g_reply, "enabled");
    s.cpu = json_int(g_reply, "cpu");
    s.duty = json_int(g_reply, "duty");
    s.desired = json_int(g_reply, "desired");
    if (!json_x100(g_reply, "target", s.target_c100) || s.target_c100 < 0)
        s.target_c100 = -1;
    if (!json_x100(g_reply, "stock", s.stock_c100))
        s.stock_c100 = -1;
    s.console_min = json_int(g_reply, "min");
    s.at_floor = json_int(g_reply, "floor");
    const char *pts = strstr(g_reply, "\"points\":");
    if (pts)
    {
        pts += 9;
        (void)read_points(pts, s.curve);
    }
    out = s;
    return true;
}
} // namespace

Reply fetch_state(int port, State &out) noexcept
{
    const int status = request(port, "GET", "/api/state", nullptr);
    if (status == 0)
        return Reply::no_server;
    return status == 200 && parse_state(out) ? Reply::ok : Reply::bad;
}

Reply fetch_history(int port, int seconds, History &out) noexcept
{
    char path[48];
    (void)snprintf(path, sizeof path, "/api/history?s=%d", seconds);
    const int status = request(port, "GET", path, nullptr);
    if (status == 0)
        return Reply::no_server;
    if (status != 200)
        return Reply::bad;
    out.step_s = json_int(g_reply, "step");
    out.count = 0;
    const char *p = strstr(g_reply, "\"d\":[");
    if (!p)
        return Reply::bad;
    p += 5;
    while (*p == '[' && out.count < max_history)
    {
        ++p;
        int t = 0, f = 0, w = 0;
        if (!read_int(p, t) || *p++ != ',' || !read_int(p, f) || *p++ != ',' ||
            !read_int(p, w) || *p++ != ']')
            return Reply::bad;
        out.temp[out.count] = static_cast<signed char>(t < 0 || t > 127 ? -1 : t);
        out.fan[out.count] = static_cast<signed char>(f < 0 || f > 100 ? -1 : f);
        out.want[out.count] = static_cast<signed char>(w < 0 || w > 100 ? -1 : w);
        ++out.count;
        if (*p == ',')
            ++p;
    }
    return Reply::ok;
}

Reply fetch_presets(int port, Presets &out) noexcept
{
    const int status = request(port, "GET", "/api/presets", nullptr);
    if (status == 0)
        return Reply::no_server;
    if (status != 200)
        return Reply::bad;
    out.count = 0;
    const char *p = g_reply;
    while (out.count < max_presets && (p = strstr(p, "{\"name\":\"")) != nullptr)
    {
        Preset &pr = out.list[out.count];
        p += 9;
        size_t i = 0;
        while (*p && *p != '"' && i < sizeof pr.name - 1)
            pr.name[i++] = *p++;
        pr.name[i] = '\0';
        const char *pts = strstr(p, "\"points\":");
        if (!pts)
            break;
        pts += 9;
        if (!read_points(pts, pr.curve))
            break;
        p = pts;
        ++out.count;
    }
    return Reply::ok;
}

int post_config(int port, int enabled, const Curve *curve, const char *preset, State *out) noexcept
{
    char body[1024];
    (void)snprintf(body, sizeof body, "enabled=%d", enabled ? 1 : 0);
    if (curve)
    {
        char item[32];
        for (int i = 0; i < curve->count; ++i)
        {
            (void)snprintf(item, sizeof item, "&p%d=%d%%2C%d", i, curve->temp[i], curve->fan[i]);
            append(body, sizeof body, item);
        }
        append(body, sizeof body, "&preset=");
        append_encoded(body, sizeof body, preset ? preset : "");
    }
    const int status = request(port, "POST", "/api/config", body);
    if (out && status == 200 && !parse_state(*out))
        out->version[0] = '\0';
    return status;
}

int post_preset(int port, const char *action, const char *name, const Curve *curve,
                bool use) noexcept
{
    char body[1024] = "action=";
    append(body, sizeof body, action);
    append(body, sizeof body, "&name=");
    append_encoded(body, sizeof body, name);
    if (use)
        append(body, sizeof body, "&use=1");
    if (curve)
    {
        char item[32];
        for (int i = 0; i < curve->count; ++i)
        {
            (void)snprintf(item, sizeof item, "&p%d=%d%%2C%d", i, curve->temp[i], curve->fan[i]);
            append(body, sizeof body, item);
        }
    }
    return request(port, "POST", "/api/presets", body);
}

bool local_ip(char *out, int cap) noexcept
{
    // A UDP "connection" sends nothing; it only picks the outgoing interface.
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;
    sockaddr_in a{};
#ifndef BREEZE_PREVIEW // BSD only; the PC preview builds on Linux
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    a.sin_addr.s_addr = htonl(0x01010101); // 1.1.1.1
    sockaddr_in me{};
    socklen_t len = sizeof me;
    const bool ok = connect(fd, reinterpret_cast<const sockaddr *>(&a), sizeof a) == 0 &&
                    getsockname(fd, reinterpret_cast<sockaddr *>(&me), &len) == 0 &&
                    me.sin_addr.s_addr != 0;
    (void)close(fd);
    if (!ok)
        return false;
    const unsigned ip = ntohl(me.sin_addr.s_addr);
    (void)snprintf(out, static_cast<size_t>(cap), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255,
                   (ip >> 8) & 255, ip & 255);
    return true;
}

int compare_versions(const char *a, const char *b) noexcept
{
    // A part that ends in anything but '.' ends that version: its rest counts as 0.
    while (*a || *b)
    {
        int x = 0, y = 0;
        while (*a >= '0' && *a <= '9')
            x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9')
            y = y * 10 + (*b++ - '0');
        if (x != y)
            return x < y ? -1 : 1;
        a = *a == '.' ? a + 1 : "";
        b = *b == '.' ? b + 1 : "";
    }
    return 0;
}

int start_breeze() noexcept
{
    static unsigned char elf[1 << 20];
    const int fd = open("/app0/assets/breeze.elf", O_RDONLY);
    if (fd < 0)
        return -1;
    size_t len = 0;
    for (;;)
    {
        const ssize_t n = read(fd, elf + len, sizeof elf - len);
        if (n <= 0)
            break;
        len += static_cast<size_t>(n);
        if (len == sizeof elf)
            break;
    }
    (void)close(fd);
    if (len < 64 || len == sizeof elf || memcmp(elf, "\x7f" "ELF", 4) != 0)
        return -1;

    const int s = connect_local(loader_port, 5);
    if (s < 0)
        return -2;
    const bool sent = send_all(s, elf, len);
    (void)close(s);
    return sent ? 0 : -2;
}
} // namespace breeze::link
