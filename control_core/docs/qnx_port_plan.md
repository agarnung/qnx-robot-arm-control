# QNX SDP 8.0 port — plan and status

`src/ipc_qnx.c` and the `make qnx` target are **implemented**. The QNX backend
was cross-compiled with `qcc` 12.2.0 and executed inside QNX 8.0 in QEMU (QSTI,
x86_64, KVM). The benchmark and CSV are in `results/`. This note documents the
implemented mapping and what remains for a physical target.

## What changes and what does not

`control_core` is deliberately split into two kinds of files:

| File | Changes in the QNX port |
|---|---|
| `include/protocol.h` | No. It is the message format, independent of the OS. |
| `include/ipc_transport.h` | No. It is the abstract interface. |
| `src/ipc_posix.c` | Not touched — it remains the generic Linux/POSIX backend. |
| `src/ipc_qnx.c` | **Implemented** — it is the only new backend of the port. |
| `src/robot_link.c` | No change: it is a normal TCP client (`socket()`/`connect()`/`send()`), and QNX Neutrino implements the standard BSD socket API (`io-sock`), so the same code compiles and runs unchanged. |
| `src/main.c`, `tools/bench_client.c` | No change: they are written only against `ipc_transport.h`. |
| `Makefile` | `qnx` target added: invokes `qcc` and uses `ipc_qnx.c` instead of `ipc_posix.c`. |

## 1. `src/ipc_qnx.c` — function-by-function mapping

`ipc_transport.h` defines 7 functions. These are the real primitives used in
`src/ipc_qnx.c` (`<sys/neutrino.h>`, `<sys/dispatch.h>`):

```c
/* ---- server (inside control_core) --------------------------------- */

ipc_server_t *ipc_server_open(const char *endpoint) {
    /* The endpoint is normalized to a flat name under /dev/name/local/:
     * leading '/' removed and internal ones become '_'
     * ("/tmp/vcr_control.sock" -> "tmp_vcr_control.sock"). */
    name_attach_t *attach = name_attach(NULL, name, 0);
    /* name_attach() already creates the channel and registers the name;
     * store attach and attach->chid. */
}

int ipc_server_receive(ipc_server_t *srv, vcr_cmd_msg_t *out_cmd,
                        ipc_request_ctx_t **out_ctx) {
    struct _msg_info info;
    rcvid_t rcvid = MsgReceive(srv->chid, out_cmd, sizeof(*out_cmd), &info);
    /* rcvid > 0  → message: validate msglen/magic/version and store rcvid
     * rcvid == 0 → pulse (disconnect/unblock): ConnectDetach if needed and
     *              keep waiting; never return to the control loop
     * rcvid < 0  → error */
}

int ipc_server_reply(ipc_server_t *srv, ipc_request_ctx_t *ctx,
                      const vcr_status_msg_t *status) {
    return MsgReply(ctx->rcvid, EOK, status, sizeof(*status));
}

/* Malformed messages are answered with MsgError(rcvid, EMSGSIZE/EPROTO)
 * or MsgError(rcvid, ENOSYS) if they fall in the system-message range. */

/* ---- client (vision process, or bench_client) ---------------------- */

ipc_client_t *ipc_client_connect(const char *endpoint) {
    /* name_open() resolves the name and returns the side-channel coid
     * directly (the client equivalent of ConnectAttach()); store it once and
     * reuse it. */
    int coid = name_open(name, 0);
}

int ipc_client_send_request(ipc_client_t *cli, const vcr_cmd_msg_t *cmd,
                             vcr_status_msg_t *out_status) {
    long n = MsgSend(cli->coid, cmd, sizeof(*cmd),
                     out_status, sizeof(*out_status));
    /* Check that n == sizeof(status) and the magic/version in the reply. */
}
```

`ipc_server_close` uses `name_detach(attach, 0)` (removes the name and destroys
the channel created by `name_attach`); `ipc_client_disconnect` uses
`name_close(coid)`.

Note: the old pseudocode combined `ChannelCreate` + `name_attach` and
`name_open` + `ConnectAttach`. That is incorrect: `name_attach()` creates the
channel by itself, and `name_open()` already returns a coid, not an fd that has to
be passed to `ConnectAttach()`. The implementation uses the idiomatic form.

## 2. Why this mapping is the selling point, not just a detail

- `MsgSend()`/`MsgReceive()`/`MsgReply()` move the message **by copying it
  directly between address spaces** in the kernel, without socket buffers, without
  Nagle/TCP heuristics, and without the scheduler of a generic Linux UNIX-domain
  socket (which, even when fast, gives no upper latency bound).
- The thread calling `MsgSend()` blocks at its real priority; QNX can propagate
  priority inheritance to the server blocked in `MsgReceive()`, avoiding priority
  inversion — something a POSIX socket under generic Linux CFS does not provide.
- It is exactly the same synchronous request-response pattern already used by
  `ipc_posix.c` — the port is not "rewriting the protocol", it is "replacing the
  transport that carries it", which is precisely the project thesis.

## 3. Build

The `Makefile` already has the target. With the SDK installed and
`qnxsdp-env.sh` sourced:

```bash
make qnx                       # ARM64 by default -> build/qnx/*.qnx
make qnx QNX_ARCH=x86_64        # x86_64
```

Equivalent to:

```makefile
QNX_ARCH   ?= aarch64le
QNX_CC     ?= qcc
QNX_CFLAGS ?= -Vgcc_nto$(QNX_ARCH) -std=c11 -Wall -Wextra -O2 -g
QNX_LDLIBS ?= -lm -lsocket

qnx: $(QNX_BIN) $(QNX_BENCH)
```

`-Vgcc_ntoaarch64le` for ARM64; `-Vgcc_ntox86_64` for x86_64 (choose
according to the real target, see section 4). QNX objects are compiled into
`build/qnx/` so they do not mix with the POSIX build.

`src/ipc_posix.c` is excluded from the link and `src/ipc_qnx.c` is included in
its place; everything else (`robot_link.c`, `latency_log.c`, `clock_util.c`,
`urscript.c`, `main.c`, `tools/bench_client.c`) is compiled unchanged.

## 4. What was needed for the first real build/run on QNX

The first real build/run is done:

1. **QNX SDP 8.0 license** (free for non-commercial use through QNX
   Everywhere; no purchase).
2. **QNX 8.0 target**: QSTI for QEMU x86_64 (`com.qnx.qnx800.quickstart.qemu`),
   booted with KVM.
3. **Network from QNX to the robot substitute**: the mock UR20 on the host at
   `10.0.2.2:30003` (or a real UR20 on the LAN).

What remains is a physical QNX board and a real UR20/UR e-series, not the port
itself.

## 5. Linux-generic vs QNX measurement plan

With a `control_core` binary running on QNX 8.0 in QEMU:

1. Run `bench_client --rate-hz 10/20/50 --count 2000` against
   `control_core --dry-run` on **generic Linux** → save the CSV.
2. Repeat exactly the same command against `control_core.qnx --dry-run` on the QNX
   target → save the CSV.
3. Compare `stddev` (jitter proxy) and the p99 of `metric_s` (RTT) between both
   CSVs — that comparison, with both distributions overlaid in a histogram, is the
   central demo for the judges.
4. Repeat the same experiment with `--robot-ip` (real robot or URSim) instead of
   `--dry-run`, to show the effect on the full loop, not only the IPC part.

Measured in this submission: 2000 commands at 20 Hz on QNX, p99 0.238 ms vs
0.408 ms on Linux/POSIX; IPC mean 0.025 ms vs 0.078 ms. See
[`../../results/README.md`](../../results/README.md).

## 6. Honest limitations

- QEMU is not a real RTOS board: the absolute jitter is not a production
  figure. The comparison validates the port and the message path.
- The type-64 telemetry parser is pinned to the deterministic mock; the real
  URSim stream is big-endian with different offsets, so `--with-telemetry` is not
  verified against URSim in this repository.
- The vision/MediaPipe half is intentionally out of scope; it is the
  soft-real-time producer that talks to this core.
