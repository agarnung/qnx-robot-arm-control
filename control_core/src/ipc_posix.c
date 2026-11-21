/*
 * ipc_posix.c — today's implementation of ipc_transport.h: a UNIX-domain
 * SOCK_STREAM socket, chosen to approximate QNX message-passing semantics
 * as closely as plain POSIX allows (see ipc_transport.h for the mapping):
 *
 *   - one persistent connection per client, set up once (like
 *     ConnectAttach()), reused for every request (like repeated MsgSend());
 *   - each request/reply is a fixed-size struct with no length prefix and
 *     no multi-message framing, mirroring MsgSend()'s single-copy-in/
 *     single-copy-out contract;
 *   - the server handles exactly one client connection at a time, since the
 *     control loop is a single dedicated channel between one vision process
 *     and one robot arm — matching the actual deployment, not a general
 *     purpose RPC fan-in.
 *
 * This file is the ONLY place in control_core that calls socket()/connect()/
 * accept() for the vision<->core link (robot_link.c separately owns the
 * *outbound* TCP socket to the physical robot on port 30003, which is a
 * different concern and stays a plain TCP client on every target, QNX
 * included, since the robot's interface is fixed by Universal Robots).
 */
#define _POSIX_C_SOURCE 200809L
#include "ipc_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

struct ipc_server {
    int listen_fd;
    int client_fd;   /* -1 until a client has connected */
    char endpoint[108];
};

struct ipc_client {
    int fd;
};

/* The request context on POSIX is a trivial wrapper: there is only ever one
 * connected fd per server, so "replying to ctx" and "replying to the
 * server's current client" are the same operation. The struct still exists
 * (rather than passing the fd directly) so control_loop.c is written
 * against the portable ipc_transport.h contract, not this detail. */
struct ipc_request_ctx {
    int fd;
};

static int set_cloexec(int fd) {
    /* best-effort; failure here is not fatal */
    (void)fd;
    return 0;
}

ipc_server_t *ipc_server_open(const char *endpoint) {
    if (!endpoint || strlen(endpoint) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        errno = EINVAL;
        return NULL;
    }

    ipc_server_t *srv = calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->client_fd = -1;
    snprintf(srv->endpoint, sizeof(srv->endpoint), "%s", endpoint);

    /* Remove a stale socket file from a previous crashed run. */
    unlink(endpoint);

    srv->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) { free(srv); return NULL; }
    set_cloexec(srv->listen_fd);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", endpoint);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }
    if (listen(srv->listen_fd, 1) != 0) {
        close(srv->listen_fd);
        unlink(endpoint);
        free(srv);
        return NULL;
    }
    return srv;
}

static int read_exact(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r == 0) return -1;              /* peer closed */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

static int write_exact(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = send(fd, p + sent, n - sent, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)w;
    }
    return 0;
}

int ipc_server_receive(ipc_server_t *srv, vcr_cmd_msg_t *out_cmd,
                        ipc_request_ctx_t **out_ctx) {
    if (!srv || !out_cmd || !out_ctx) { errno = EINVAL; return -1; }

    if (srv->client_fd < 0) {
        int fd = accept(srv->listen_fd, NULL, NULL);
        if (fd < 0) return -1;
        srv->client_fd = fd;
    }

    if (read_exact(srv->client_fd, out_cmd, sizeof(*out_cmd)) != 0) {
        /* Client gone — close so the next call re-accepts a fresh one. */
        close(srv->client_fd);
        srv->client_fd = -1;
        return -1;
    }

    if (out_cmd->magic != VCR_MAGIC || out_cmd->version != VCR_PROTO_VERSION) {
        errno = EPROTO;
        return -1;
    }

    ipc_request_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) { errno = ENOMEM; return -1; }
    ctx->fd = srv->client_fd;
    *out_ctx = ctx;
    return 0;
}

int ipc_server_reply(ipc_server_t *srv, ipc_request_ctx_t *ctx,
                      const vcr_status_msg_t *status) {
    (void)srv;
    if (!ctx || !status) { errno = EINVAL; return -1; }
    int rc = write_exact(ctx->fd, status, sizeof(*status));
    free(ctx);
    return rc;
}

void ipc_server_close(ipc_server_t *srv) {
    if (!srv) return;
    if (srv->client_fd >= 0) close(srv->client_fd);
    close(srv->listen_fd);
    unlink(srv->endpoint);
    free(srv);
}

ipc_client_t *ipc_client_connect(const char *endpoint) {
    if (!endpoint || strlen(endpoint) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        errno = EINVAL;
        return NULL;
    }

    ipc_client_t *cli = calloc(1, sizeof(*cli));
    if (!cli) return NULL;

    cli->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cli->fd < 0) { free(cli); return NULL; }
    set_cloexec(cli->fd);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", endpoint);

    if (connect(cli->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(cli->fd);
        free(cli);
        return NULL;
    }
    return cli;
}

int ipc_client_send_request(ipc_client_t *cli, const vcr_cmd_msg_t *cmd,
                             vcr_status_msg_t *out_status) {
    if (!cli || !cmd || !out_status) { errno = EINVAL; return -1; }
    if (write_exact(cli->fd, cmd, sizeof(*cmd)) != 0) return -1;
    if (read_exact(cli->fd, out_status, sizeof(*out_status)) != 0) return -1;
    if (out_status->magic != VCR_MAGIC || out_status->version != VCR_PROTO_VERSION) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

void ipc_client_disconnect(ipc_client_t *cli) {
    if (!cli) return;
    close(cli->fd);
    free(cli);
}
