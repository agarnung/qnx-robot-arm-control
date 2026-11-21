#!/usr/bin/env python3
"""Simulated UR server (port 30003) to test control_core without a robot.

- Accepts the URScript connection from control_core.
- Logs every command received (movej/servoj/get_joint_positions...).
- Emits type-64 packets (1220 bytes, PolyScope X flat little-endian format)
  at the configured rate, with joint positions and robot_mode=2 (READY) /
  safety_mode=1 (NORMAL).

It is not URSim: it is a deterministic test double with no hardware.
"""

from __future__ import annotations

import argparse
import socket
import struct
import threading
import time


def _type64_payload(q, robot_mode=2.0, safety_mode=1.0) -> bytes:
    payload = bytearray(1220)
    payload[0] = 64  # message type
    for i, value in enumerate(q[:6]):
        struct.pack_into("<d", payload, 249 + i * 8, float(value))
    struct.pack_into("<d", payload, 753, float(robot_mode))
    struct.pack_into("<d", payload, 809, float(safety_mode))
    return bytes(payload)


class MockUR:
    def __init__(self, host: str, port: int, hz: float, log_path: str | None):
        self.host = host
        self.port = port
        self.period = 1.0 / hz if hz > 0 else 0.1
        self.log_path = log_path
        self.stop = threading.Event()
        self.lock = threading.Lock()

    def _log(self, line: str):
        with self.lock:
            print(line, flush=True)
            if self.log_path:
                with open(self.log_path, "a", encoding="utf-8") as fh:
                    fh.write(line + "\n")

    def _telemetry_loop(self, conn: socket.socket):
        q = [0.0, -1.57, 0.0, -1.57, 0.0, 0.0]
        phase = 0.0
        while not self.stop.is_set():
            try:
                payload = _type64_payload(q)
                conn.sendall(struct.pack(">I", len(payload) + 4) + payload)
                # smooth joint motion so the telemetry is not flat
                phase += 0.05
                q[0] = 0.15 * __import__("math").sin(phase)
                time.sleep(self.period)
            except OSError:
                break

    def _handle(self, conn: socket.socket, addr):
        self._log(f"[mock-ur] connection from {addr[0]}:{addr[1]}")
        threading.Thread(target=self._telemetry_loop, args=(conn,), daemon=True).start()
        buf = b""
        try:
            while not self.stop.is_set():
                data = conn.recv(4096)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", "replace").strip()
                    if text:
                        self._log(f"[mock-ur] CMD {text}")
        except OSError:
            pass
        finally:
            self._log(f"[mock-ur] disconnected {addr[0]}:{addr[1]}")
            try:
                conn.close()
            except OSError:
                pass

    def run(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind((self.host, self.port))
            srv.listen(4)
            srv.settimeout(0.5)
            self._log(f"[mock-ur] listening on {self.host}:{self.port} (type-64)")
            try:
                while not self.stop.is_set():
                    try:
                        conn, addr = srv.accept()
                    except socket.timeout:
                        continue
                    threading.Thread(target=self._handle, args=(conn, addr), daemon=True).start()
            except KeyboardInterrupt:
                pass
            finally:
                self.stop.set()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=30003)
    ap.add_argument("--hz", type=float, default=10.0)
    ap.add_argument("--log", default=None)
    args = ap.parse_args()
    MockUR(args.host, args.port, args.hz, args.log).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
