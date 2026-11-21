"""control_client.py — Python-side client for the control_core IPC link.

This replaces direct use of src/robot/connection.py (UR30003 socket to port
30003) from the vision scripts. The vision component now only ever talks to
control_core over this well-defined command/status protocol; control_core
is the only process that opens a socket to the physical robot.

Wire format mirrors control_core/include/protocol.h exactly — see that file
for the authoritative field list and the "why these types" rationale. Keep
_CMD_FMT / _STATUS_FMT in sync with it; test_protocol_sizes() below is run
at import time (cheap, no I/O) specifically to catch a drift early instead
of producing a mysterious EPROTO at runtime.
"""
from __future__ import annotations

import socket
import struct
import time
from dataclasses import dataclass, field

# ── Wire format (must match control_core/include/protocol.h) ───────────────

VCR_MAGIC = 0x56435243  # 'VCRC'
VCR_PROTO_VERSION = 1

VCR_CMD_MOTION = 1
VCR_CMD_PING = 2
VCR_CMD_SHUTDOWN = 3

VCR_MODE_MOVEJ = 0
VCR_MODE_SERVOJ = 1
VCR_MODE_HOME_PROBE = 2  # joints ignored; asks the controller to report its
                         # own position (see protocol.h). Needs control_core
                         # started with --with-telemetry to return anything.

VCR_STATUS_ACK = 1
VCR_STATUS_ERROR = 2

# Same codes/names as src/robot/telemetry.py (_RT_ROBOT_MODE_NAMES) for the
# Polyscope X type-64 flat stream, so the overlay shows the same labels as
# before the split into two components.
_ROBOT_MODE_NAMES = {
    0: "RUNNING", 1: "FREEDRIVE", 2: "READY", 3: "INITIALIZING",
    4: "SECURITY_STOPPED", 5: "ESTOPPED", 6: "FATAL_ERROR", 7: "NO_POWER",
    8: "NOT_CONNECTED", 9: "SHUTDOWN", 10: "SAFEGUARD_STOP",
}

# magic, version, type, seq, t_send,
# mode, enable, dry_run, _pad0,
# joints[6], velocity, acceleration, servoj_t, servoj_lookahead,
# servoj_gain
_CMD_FMT = "<IHHIdBBBBddddddddddI"

# magic, version, type, seq,
# t_send_echo, t_core_recv, t_robot_sent,
# robot_connected, ok, telemetry_valid, _pad0, error_code,
# q_actual[6], robot_mode, safety_mode, loop_jitter_ns
_STATUS_FMT = "<IHHIdddBBBBiddddddiiq"

_CMD_SIZE = struct.calcsize(_CMD_FMT)
_STATUS_SIZE = struct.calcsize(_STATUS_FMT)

assert _CMD_SIZE == 108, f"cmd struct layout drifted: {_CMD_SIZE} bytes (expected 108)"
assert _STATUS_SIZE == 108, f"status struct layout drifted: {_STATUS_SIZE} bytes (expected 108)"


@dataclass
class CoreStatus:
    """Decoded vcr_status_msg_t — see protocol.h for field semantics."""
    seq: int
    t_send_echo: float
    t_core_recv: float
    t_robot_sent: float
    robot_connected: bool
    ok: bool
    telemetry_valid: bool
    error_code: int
    q_actual: list[float]
    robot_mode: int
    safety_mode: int
    loop_jitter_ns: int

    @property
    def rtt_s(self) -> float:
        """Round-trip time as seen by the client: send -> reply received.
        Call with time.monotonic() captured right after recv() for the
        true end-to-end figure; this property only has t_send_echo vs.
        t_robot_sent, i.e. the core-side portion of the trip."""
        return self.t_robot_sent - self.t_send_echo

    @property
    def robot_mode_name(self) -> "str | None":
        """Human label for robot_mode, only when telemetry was actually read
        in this reply (telemetry_valid). Returns None otherwise so callers
        can distinguish "no data" from a real mode code of 0 (RUNNING)."""
        if not self.telemetry_valid:
            return None
        return _ROBOT_MODE_NAMES.get(self.robot_mode, f"UNKNOWN({self.robot_mode})")


def _unpack_status(raw: bytes) -> CoreStatus:
    (magic, version, _type, seq,
     t_send_echo, t_core_recv, t_robot_sent,
     robot_connected, ok, telemetry_valid, _pad0, error_code,
     q0, q1, q2, q3, q4, q5,
     robot_mode, safety_mode, loop_jitter_ns) = struct.unpack(_STATUS_FMT, raw)

    if magic != VCR_MAGIC:
        raise ValueError(f"bad magic in status reply: {magic:#x}")
    if version != VCR_PROTO_VERSION:
        raise ValueError(f"unsupported protocol version: {version}")

    return CoreStatus(
        seq=seq, t_send_echo=t_send_echo, t_core_recv=t_core_recv,
        t_robot_sent=t_robot_sent, robot_connected=bool(robot_connected),
        ok=bool(ok), telemetry_valid=bool(telemetry_valid),
        error_code=error_code, q_actual=[q0, q1, q2, q3, q4, q5],
        robot_mode=robot_mode, safety_mode=safety_mode,
        loop_jitter_ns=loop_jitter_ns,
    )


class ControlCoreClient:
    """Blocking client for one control_core instance over a UNIX-domain
    socket — the POSIX backend of control_core/include/ipc_transport.h.

    One connection, reused for every request/reply pair (connect() is NOT
    called again per command — see ipc_transport.h for why repeating that
    setup on every command would itself add latency/jitter).
    """

    def __init__(self, endpoint: str = "/tmp/vcr_control.sock", timeout: float = 2.0):
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.settimeout(timeout)
        self._sock.connect(endpoint)
        self._seq = 0

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFFFFFFFF
        return self._seq

    def _send_request(self, raw_cmd: bytes) -> CoreStatus:
        self._sock.sendall(raw_cmd)
        buf = b""
        while len(buf) < _STATUS_SIZE:
            chunk = self._sock.recv(_STATUS_SIZE - len(buf))
            if not chunk:
                raise ConnectionError("control_core closed the connection")
            buf += chunk
        return _unpack_status(buf)

    def ping(self) -> CoreStatus:
        seq = self._next_seq()
        raw = struct.pack(
            _CMD_FMT, VCR_MAGIC, VCR_PROTO_VERSION, VCR_CMD_PING, seq,
            time.monotonic(), 0, 0, 0, 0, *([0.0] * 10), 0,
        )
        return self._send_request(raw)

    def send_motion(self, joints: list[float], velocity: float, acceleration: float,
                     mode: int = VCR_MODE_MOVEJ, enable: bool = True, dry_run: bool = False,
                     servoj_t: float = 0.0, servoj_lookahead: float = 0.1,
                     servoj_gain: int = 300) -> CoreStatus:
        """Send one joint-space motion command and block for control_core's
        reply (mirrors MsgSend() semantics — see ipc_transport.h)."""
        if len(joints) != 6:
            raise ValueError("joints must have exactly 6 elements")

        seq = self._next_seq()
        t_send = time.monotonic()
        raw = struct.pack(
            _CMD_FMT, VCR_MAGIC, VCR_PROTO_VERSION, VCR_CMD_MOTION, seq,
            t_send,
            mode, 1 if enable else 0, 1 if dry_run else 0, 0,
            *joints, velocity, acceleration, servoj_t, servoj_lookahead,
            servoj_gain,
        )
        return self._send_request(raw)

    def probe_home(self, velocity: float = 0.5, acceleration: float = 0.5,
                    servoj_t: float = 0.4, servoj_lookahead: float = 0.1,
                    servoj_gain: int = 300) -> CoreStatus:
        """Ask control_core to read the robot's actual joint positions
        without the caller needing to know them in advance (mirrors the
        pre-split src/robot/telemetry.py::read_actual_position trick).
        Only returns usable data (status.telemetry_valid) if control_core
        was started with --with-telemetry; otherwise behaves like a motion
        command with no observable effect on the physical position."""
        return self.send_motion(
            joints=[0.0] * 6,  # ignored by control_core in this mode
            velocity=velocity, acceleration=acceleration,
            mode=VCR_MODE_HOME_PROBE, enable=True, dry_run=False,
            servoj_t=servoj_t, servoj_lookahead=servoj_lookahead,
            servoj_gain=servoj_gain,
        )

    def shutdown_core(self) -> None:
        seq = self._next_seq()
        raw = struct.pack(
            _CMD_FMT, VCR_MAGIC, VCR_PROTO_VERSION, VCR_CMD_SHUTDOWN, seq,
            time.monotonic(), 0, 0, 0, 0, *([0.0] * 10), 0,
        )
        try:
            self._send_request(raw)
        except (ConnectionError, OSError):
            pass  # core may exit before replying — that is fine here

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass

    def __enter__(self) -> "ControlCoreClient":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()
