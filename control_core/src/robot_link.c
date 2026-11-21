#define _POSIX_C_SOURCE 200809L
#include "robot_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#ifdef __unix__
#include <sys/time.h>
#endif

#define UR_PORT 30003

struct robot_link {
    int fd;
};

robot_link_t *robot_link_connect(const char *ip) {
    if (!ip) { errno = EINVAL; return NULL; }

    robot_link_t *link_ = calloc(1, sizeof(*link_));
    if (!link_) return NULL;

    link_->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (link_->fd < 0) { free(link_); return NULL; }

    int one = 1;
    setsockopt(link_->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(UR_PORT);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        close(link_->fd);
        free(link_);
        errno = EINVAL;
        return NULL;
    }

    /* Bounded connect timeout: 5s, matching connection.py's UR30003. */
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(link_->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(link_->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(link_->fd);
        free(link_);
        return NULL;
    }

    return link_;
}

int robot_link_send(robot_link_t *link_, const char *urscript) {
    if (!link_ || !urscript) { errno = EINVAL; return -1; }

    size_t len = strlen(urscript);
    int need_nl = (len == 0 || urscript[len - 1] != '\n');
    char *buf = malloc(len + (need_nl ? 2 : 1));
    if (!buf) { errno = ENOMEM; return -1; }
    memcpy(buf, urscript, len);
    if (need_nl) buf[len++] = '\n';
    buf[len] = '\0';

    size_t sent = 0;
    int rc = 0;
    while (sent < len) {
        ssize_t w = send(link_->fd, buf + sent, len - sent, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            rc = -1;
            break;
        }
        sent += (size_t)w;
    }
    free(buf);
    return rc;
}

/* ── Type-64 flat telemetry parsing ──────────────────────────────────────
 * Mirrors src/robot/telemetry.py (_parse_type64) field-for-field. Offsets
 * are relative to `payload`, where payload[0] is the type byte (64) — i.e.
 * the same indexing the Python parser uses after stripping the 4-byte
 * big-endian length prefix. Keep these two implementations in sync; the
 * smoke test in tests/test_telemetry_parse.c pins the exact offsets with a
 * synthetic packet so a accidental drift fails `make test` instead of
 * silently corrupting q_actual on a real robot. */
#define FLAT_MIN_BYTES   817
#define OFF_Q_ACTUAL     249
#define OFF_ROBOT_MODE   753
#define OFF_SAFETY_MODE  809

static double read_f64_le(const uint8_t *p) {
    uint64_t bits;
    memcpy(&bits, p, 8);
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    /* Host is little-endian (true for every target we build on: x86_64,
     * ARM64 Linux, and QNX on the same architectures) — no byte-swap. */
#else
#error "robot_link telemetry parser assumes a little-endian host"
#endif
    double v;
    memcpy(&v, &bits, 8);
    return v;
}

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int vcr_parse_type64_telemetry(const uint8_t *payload, size_t len,
                                robot_telemetry_t *out) {
    if (len < FLAT_MIN_BYTES || payload[0] != 64) return 0;
    for (int i = 0; i < 6; i++) {
        out->q_actual[i] = read_f64_le(payload + OFF_Q_ACTUAL + i * 8);
    }
    out->robot_mode   = (int32_t)(read_f64_le(payload + OFF_ROBOT_MODE) + 0.5);
    out->safety_mode  = (int32_t)(read_f64_le(payload + OFF_SAFETY_MODE) + 0.5);
    return 1;
}

int robot_link_try_read_telemetry(robot_link_t *link_, int timeout_ms,
                                   robot_telemetry_t *out) {
    if (!link_ || !out) { errno = EINVAL; return -1; }

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(link_->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t len_buf[4];
    ssize_t r = recv(link_->fd, len_buf, 4, MSG_WAITALL);
    if (r == 0) return -1;
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; /* timeout */
        return -1;
    }
    if (r != 4) return 0;

    uint32_t pkt_len = read_u32_be(len_buf);
    if (pkt_len < 5 || pkt_len > 16384) return 0;

    uint8_t payload[16384];
    size_t payload_len = pkt_len - 4;
    r = recv(link_->fd, payload, payload_len, MSG_WAITALL);
    if (r <= 0) {
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return -1;
    }

    return vcr_parse_type64_telemetry(payload, (size_t)r, out);
}

void robot_link_close(robot_link_t *link_) {
    if (!link_) return;
    close(link_->fd);
    free(link_);
}
