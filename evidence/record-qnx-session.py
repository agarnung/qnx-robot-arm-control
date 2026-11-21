#!/usr/bin/env python3
"""Record a complete QNX console session in QEMU (boot, login, version,
mount and benchmark) and save it to evidence/qnx-session.log.

Usage: ./evidence/record-qnx-session.py
"""
import os
import sys
import pexpect

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EMU = os.path.join(ROOT, "qnx-emulator")
LOG = os.path.join(HERE, "qnx-session.log")

PROMPT = r"\[root@qnxqemu[^\]]*\]# "


def main() -> int:
    log = open(LOG, "w", encoding="utf-8")
    env = dict(os.environ, QNX_SHARE_IF="ide")
    child = pexpect.spawn(
        os.path.join(EMU, "run-qnx.sh"), cwd=EMU, env=env, encoding="utf-8",
        codec_errors="replace", timeout=180, dimensions=(40, 120),
    )
    child.logfile = log

    idx = child.expect([r"login:", r"[Pp]assword:"], timeout=180)
    if idx == 0:
        child.sendline("root")
        child.expect(r"[Pp]assword:", timeout=30)
    child.sendline("root")
    child.expect(PROMPT, timeout=90)

    def run(cmd: str, timeout: int = 60) -> None:
        child.sendline(cmd)
        child.expect(PROMPT, timeout=timeout)

    run('echo "@@CLIP:07-qnx-login@@"')
    run('echo "@@CLIP:08-qnx-version@@"')
    run("uname -a")
    run("pidin info")
    run("ls /dev/hd*")
    run('echo "@@CLIP:09-qnx-mount@@"')
    run("mkdir -p /data/share")
    run("mount -t dos /dev/hd1t6 /data/share")
    run("mkdir -p /tmp/share")
    run("cp /data/share/control_core.qnx /tmp/share/")
    run("cp /data/share/bench_client.qnx /tmp/share/")
    run("cp /data/share/bench-inside-qnx.sh /tmp/share/")
    run("cd /tmp/share")
    run("chmod +x *.qnx")
    run('echo "@@CLIP:10-qnx-bench@@"')
    run("COUNT=500 sh bench-inside-qnx.sh", timeout=180)
    run('echo "@@CLIP:11-qnx-csv@@"')
    run("ls -l *.csv")

    child.sendcontrol("a")
    child.send("x")
    child.expect(pexpect.EOF, timeout=30)
    child.close(force=True)
    log.close()
    print(f"ok: {LOG}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
