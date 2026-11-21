#!/usr/bin/env bash
# Start URSim (UR20) in Docker with VNC and TCP 30003, and leave it running.
#
#   ./ursim.sh                 # UR20, ports 5900/6080/30003/29999
#   URSIM_MODEL=UR10 ./ursim.sh
#   docker stop ursim          # stop
#
# UI:   http://localhost:6080/vnc.html?host=localhost&port=6080
# VNC:  localhost:5900
# Usage: ./control_core/build/control_core --robot-ip 127.0.0.1
set -euo pipefail
IMAGE="${URSIM_IMAGE:-universalrobots/ursim_e-series:5.26}"
NAME="${URSIM_NAME:-ursim}"
MODEL="${URSIM_MODEL:-UR20}"

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" \
  -e "ROBOT_MODEL=$MODEL" \
  -p 5900:5900 -p 6080:6080 -p 30003:30003 -p 29999:29999 \
  "$IMAGE" >/dev/null

echo "[i] waiting for the URSim dashboard..."
for _ in $(seq 1 60); do
  if python3 - <<'PY' 2>/dev/null
import socket
s = socket.create_connection(("127.0.0.1", 29999), timeout=1)
s.recv(4096)
s.close()
PY
  then
    break
  fi
  sleep 2
done

python3 - <<'PY'
import socket, time
for cmd in ("power on", "brake release", "robotmode"):
    s = socket.create_connection(("127.0.0.1", 29999), timeout=5)
    s.recv(4096)
    s.sendall((cmd + "\n").encode())
    time.sleep(2)
    print(f"[ursim] {cmd}: {s.recv(4096).decode(errors='replace').strip()}")
    s.close()
PY

cat <<EOF

[ok] URSim $MODEL en Docker ($NAME)
     UI:        http://localhost:6080/vnc.html?host=localhost&port=6080
     VNC:       localhost:5900
     TCP:       localhost:30003
     Dashboard: localhost:29999

Next:
     ./control_core/build/control_core --robot-ip 127.0.0.1 --log /tmp/core_ursim.csv

Note: this is a simulator, not the physical robot. Real type-64 telemetry is
big-endian and does not match the synthetic mock; use the video without
--with-telemetry or fix the parser first.
EOF
