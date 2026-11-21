/*
 * ipc_qnx.c — QNX Neutrino backend for ipc_transport.h.
 *
 * This is the native message-passing counterpart of src/ipc_posix.c. It
 * implements the exact same seven functions, so neither main.c nor
 * tools/bench_client.c changes: the Makefile selects this file instead of
 * ipc_posix.c for the `qnx` target.
 *
 * Mapping to Neutrino primitives:
 *
 *   ipc_server_open        -> name_attach()   (creates the channel, i.e.
 *                                              ChannelCreate, and registers
 *                                              /dev/name/local/<endpoint>)
 *   ipc_server_receive     -> MsgReceive()    (blocks for a message/pulse)
 *   ipc_server_reply       -> MsgReply()      (or MsgError() on bad input)
 *   ipc_server_close       -> name_detach()   (destroys the attached channel)
 *   ipc_client_connect     -> name_open()     (resolves the name and returns
 *                                              the side-channel coid that
 *                                              ConnectAttach would create)
 *   ipc_client_send_request-> MsgSend()       (blocking request+reply)
 *   ipc_client_disconnect  -> name_close()
 *
 * Endpoint handling: the POSIX backend uses a filesystem path such as
 * "/tmp/vcr_control.sock". QNX name_attach() forbids a leading slash, so the
 * endpoint is normalised to a flat name under /dev/name/local/: leading '/'
 * are dropped and any remaining '/' become '_'. That makes both
 * "/tmp/vcr_control.sock" (the compile-time CLI default) and the more
 * idiomatic "vcr/control_core" work unchanged on the target, without
 * requiring pre-existing directory entries in the QNX name space.
 *
 * Fixed 108-byte vcr_cmd_msg_t / vcr_status_msg_t payloads are moved as a
 * single message in each direction, exactly as protocol.h specifies. The low
 * 16 bits of VCR_MAGIC (0x5243) are above the QNX system-message range
 * (0.._IO_MAX), so a normal request is never mistaken for an _IO message.
 *
 * NOTE: this file has not been compiled against a real QNX SDP 8.0 toolchain
 * in this environment. See docs/qnx_port_plan.md.
 */
#include "ipc_transport.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/dispatch.h>
#include <sys/neutrino.h>

/* name_attach()/name_open() take a name relative to /dev/name/local/. */
#define VCR_QNX_NAME_MAX 256

struct ipc_server {
    name_attach_t *attach;
    int chid;
};

struct ipc_client {
    int coid;
};

/* On QNX the per-request context is the rcvid MsgReceive() returned; it is
 * what MsgReply()/MsgError() need to address the blocked sender. */
struct ipc_request_ctx {
    rcvid_t rcvid;
};

/* Normalise the portable endpoint into a flat QNX pathname-space name under
 * /dev/name/local/. Skipping leading '/' and replacing any remaining '/' by
 * '_' means the POSIX default "/tmp/vcr_control.sock" and the idiomatic QNX
 * form "vcr/control_core" both work without requiring pre-created directory
 * entries in the QNX name space. Server and client apply the same rule. */
static int qnx_name(const char *endpoint, char *out, size_t out_sz) {
    if (!endpoint) { errno = EINVAL; return -1; }
    while (*endpoint == '/') endpoint++;
    if (*endpoint == '\0') { errno = EINVAL; return -1; }

    size_t j = 0;
    for (const char *p = endpoint; *p; p++) {
        if (j + 1 >= out_sz) { errno = ENAMETOOLONG; return -1; }
        out[j++] = (*p == '/') ? '_' : *p;
    }
    out[j] = '\0';
    return 0;
}

ipc_server_t *ipc_server_open(const char *endpoint) {
    char name[VCR_QNX_NAME_MAX];
    if (qnx_name(endpoint, name, sizeof(name)) != 0) return NULL;

    ipc_server_t *srv = calloc(1, sizeof(*srv));
    if (!srv) return NULL;

    /* name_attach() creates a channel and registers the name in one step;
     * its chid is what MsgReceive() listens on. A stale name from a crashed
     * previous run is not unlinked here: QNX removes the name when the
     * owning process dies, and name_attach() returns EEXIST otherwise so the
     * failure is visible rather than silently racing another server. */
    srv->attach = name_attach(NULL, name, 0);
    if (!srv->attach) {
        free(srv);
        return NULL;
    }
    srv->chid = srv->attach->chid;
    return srv;
}

int ipc_server_receive(ipc_server_t *srv, vcr_cmd_msg_t *out_cmd,
                        ipc_request_ctx_t **out_ctx) {
    if (!srv || !out_cmd || !out_ctx) { errno = EINVAL; return -1; }

    for (;;) {
        struct _msg_info info;
        memset(&info, 0, sizeof(info));

        rcvid_t rcvid = MsgReceive(srv->chid, out_cmd, sizeof(*out_cmd), &info);
        if (rcvid < 0) return -1;

        if (rcvid == 0) {
            /* A pulse, not a request. name_attach() enables disconnect and
             * unblock pulses; consume them and keep waiting for a message.
             * out_cmd is a packed struct (alignment 1), so copying the pulse
             * header out avoids an unaligned struct _pulse access on strict
             * targets such as aarch64. */
            struct _pulse pulse;
            memcpy(&pulse, out_cmd, sizeof(pulse));
            if (pulse.code == _PULSE_CODE_DISCONNECT) {
                ConnectDetach(pulse.scoid);
            }
            continue;
        }

        /* Reject anything that is not our fixed-size protocol message:
         * short/long payloads, system/_IO messages and wrong magic/version.
         * MsgError() unblocks the sender without pretending it succeeded. */
        if (info.msglen != sizeof(*out_cmd)) {
            MsgError(rcvid, EMSGSIZE);
            continue;
        }
        uint16_t mtype;
        memcpy(&mtype, out_cmd, sizeof(mtype));
        if (mtype <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }
        if (out_cmd->magic != VCR_MAGIC ||
            out_cmd->version != VCR_PROTO_VERSION) {
            MsgError(rcvid, EPROTO);
            continue;
        }

        ipc_request_ctx_t *ctx = malloc(sizeof(*ctx));
        if (!ctx) {
            MsgError(rcvid, ENOMEM);
            errno = ENOMEM;
            return -1;
        }
        ctx->rcvid = rcvid;
        *out_ctx = ctx;
        return 0;
    }
}

int ipc_server_reply(ipc_server_t *srv, ipc_request_ctx_t *ctx,
                      const vcr_status_msg_t *status) {
    (void)srv;
    if (!ctx || !status) { errno = EINVAL; return -1; }

    rcvid_t rcvid = ctx->rcvid;
    free(ctx);

    if (MsgReply(rcvid, EOK, status, sizeof(*status)) < 0) return -1;
    return 0;
}

void ipc_server_close(ipc_server_t *srv) {
    if (!srv) return;
    if (srv->attach) name_detach(srv->attach, 0);
    free(srv);
}

ipc_client_t *ipc_client_connect(const char *endpoint) {
    char name[VCR_QNX_NAME_MAX];
    if (qnx_name(endpoint, name, sizeof(name)) != 0) return NULL;

    ipc_client_t *cli = calloc(1, sizeof(*cli));
    if (!cli) return NULL;

    /* name_open() resolves the name and returns a side-channel coid, the
     * client-side equivalent of ConnectAttach(). It is reused for every
     * later MsgSend() so connection setup is not on the per-request path. */
    cli->coid = name_open(name, 0);
    if (cli->coid == -1) {
        free(cli);
        return NULL;
    }
    return cli;
}

int ipc_client_send_request(ipc_client_t *cli, const vcr_cmd_msg_t *cmd,
                             vcr_status_msg_t *out_status) {
    if (!cli || !cmd || !out_status) { errno = EINVAL; return -1; }

    /* MsgSend() is the blocking request+reply in one call that this API was
     * shaped around: it sends the 108-byte command and waits for the 108-byte
     * status reply into out_status. */
    long n = MsgSend(cli->coid, cmd, sizeof(*cmd),
                     out_status, sizeof(*out_status));
    /* MsgSend() returns the status from MsgReply() (0 on success), not the
     * number of bytes. A positive value is a QNX errno to propagate. */
    if (n != 0) {
        if (n > 0) errno = (int)n;
        return -1;
    }

    if (out_status->magic != VCR_MAGIC ||
        out_status->version != VCR_PROTO_VERSION) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

void ipc_client_disconnect(ipc_client_t *cli) {
    if (!cli) return;
    name_close(cli->coid);
    free(cli);
}
