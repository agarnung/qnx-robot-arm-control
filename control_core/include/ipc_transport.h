/*
 * ipc_transport.h — abstract IPC interface used by control_core.
 *
 * ============================================================================
 *  THIS IS THE FILE A QNX PORT REPLACES.
 * ============================================================================
 *
 * Every other file in control_core (control_loop.c, robot_link.c, ...) talks
 * to this interface only — never to a raw socket directly for the vision<->
 * core link. Today it is implemented once, in src/ipc_posix.c, with a
 * POSIX UNIX-domain stream socket, because that is what runs on a plain
 * Linux dev box without the QNX SDP 8.0 toolchain.
 *
 * When QNX SDP 8.0 access is available (see ../docs/qnx_port_plan.md for the
 * full pseudocode mapping), add src/ipc_qnx.c implementing the exact same
 * five functions on top of Neutrino's native message passing:
 *
 *   ipc_server_open      -> name_attach()
 *   ipc_server_receive   -> MsgReceive()            (blocks for a pulse/msg)
 *   ipc_server_reply     -> MsgReply()
 *   ipc_client_connect   -> name_open() + ConnectAttach()   (once)
 *   ipc_client_send_request -> MsgSend()            (blocking request+reply
 *                                                     in a single syscall)
 *
 * No other file needs to change: control_loop.c and bench_client.c are
 * written purely against this header, and the Makefile picks the backend
 * (ipc_posix.c vs. ipc_qnx.c) based on the target OS.
 *
 * Why this mapping is a good fit (the actual QNX pitch):
 *   MsgSend() on QNX is itself a *synchronous, kernel-scheduled* request/
 *   reply primitive with no serialization or syscall overhead beyond two
 *   copies — the kernel moves the message directly between the sender's and
 *   receiver's address spaces at the priority of the sender, which is what
 *   gives Neutrino IPC its deterministic latency bound. A POSIX UNIX-domain
 *   socket approximates the *shape* of that call (same request/reply, same
 *   one-send-one-recv-one-reply structure) but gives none of the scheduling
 *   guarantees — hence the explicit latency/jitter harness in tools/, meant
 *   to quantify exactly that gap once a QNX target is available to compare
 *   against.
 */
#ifndef VCR_IPC_TRANSPORT_H
#define VCR_IPC_TRANSPORT_H

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ipc_server ipc_server_t;
typedef struct ipc_client ipc_client_t;

/*
 * Opaque per-request context handed back by ipc_server_receive() and
 * consumed by exactly one matching ipc_server_reply() call.
 *
 * On POSIX this just carries the connected fd (server and client share the
 * same fd, so it is almost a no-op wrapper). On QNX it would carry the
 * `rcvid` returned by MsgReceive(), which MsgReply() needs to address the
 * reply back to the correct blocked sender — the abstraction exists
 * precisely so that asymmetry does not leak into control_loop.c.
 */
typedef struct ipc_request_ctx ipc_request_ctx_t;

/* ---------------------------------------------------------------- server */

/* Create and bind/listen (POSIX) or name_attach() (QNX) on
 * `endpoint` (a filesystem path for the UNIX-domain socket today; a QNX
 * name_attach() path such as "vcr/control_core" on a future port).
 * Returns NULL on failure (errno set). */
ipc_server_t *ipc_server_open(const char *endpoint);

/* Block until one request arrives. On success, fills *out_cmd and stores an
 * opaque context in *out_ctx that MUST be passed to exactly one
 * ipc_server_reply() call afterwards. Returns 0 on success, -1 on error
 * (errno set) or client disconnect. */
int ipc_server_receive(ipc_server_t *srv, vcr_cmd_msg_t *out_cmd,
                        ipc_request_ctx_t **out_ctx);

/* Send the reply for the request identified by `ctx` and release it.
 * Returns 0 on success, -1 on error. */
int ipc_server_reply(ipc_server_t *srv, ipc_request_ctx_t *ctx,
                      const vcr_status_msg_t *status);

void ipc_server_close(ipc_server_t *srv);

/* ---------------------------------------------------------------- client */

/* Connect once (POSIX: connect(); QNX: name_open()).
 * The returned handle is reused for every subsequent request — this matters
 * because repeating the (far more expensive) connection setup on every
 * command would itself be a source of jitter. Returns NULL on failure. */
ipc_client_t *ipc_client_connect(const char *endpoint);

/* Blocking request/reply in one call, mirroring MsgSend() semantics:
 * send `cmd`, block until the server replies, fill *out_status.
 * Returns 0 on success, -1 on error (errno set) / server gone. */
int ipc_client_send_request(ipc_client_t *cli, const vcr_cmd_msg_t *cmd,
                             vcr_status_msg_t *out_status);

void ipc_client_disconnect(ipc_client_t *cli);

#ifdef __cplusplus
}
#endif

#endif /* VCR_IPC_TRANSPORT_H */
