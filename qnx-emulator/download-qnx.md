# Download QNX SDP 8.0 for QEMU (free non-commercial license)

You do not need to buy a board. QNX SDP 8.0 has a **free license for
non-commercial personal use** ("QNX Everywhere"). You only need a myQNX account.

## 1. Account and license

Use your myQNX account (personal data is not included in this repository). The
profile shows "View My Registered Products", "View My Registered Support Plans",
"QNX Software Center" and "myQNX License Manager".

1. Go to <https://www.qnx.com/account/login.html> with your myQNX account.
2. Open <https://www.qnx.com/products/everywhere/> while logged in.
3. Read "Free Access to QNX SDP 8.0 for Non-Commercial Use" and **accept the
   non-commercial terms**. It is free; do not buy the commercial evaluation.
4. Open **myQNX License Manager**: <https://www.qnx.com/account/dashboard>.
5. Under "Product Licenses" / "Licenses", check that **QNX SDP 8.0** appears.
6. If it is available but not assigned, **deploy the license to your own user**
   (self). Accepting the terms is not enough: it must be deployed.
7. If it does not appear, accept the non-commercial license again; if it still
   does not appear, email `myQNX@qnx.com`.
8. Check "View My Registered Products" to confirm that QNX SDP 8.0 is registered.
9. Review the terms: personal non-commercial use, no support, no redistribution
   of QNX and no use in a commercial product; it may require renewal.
10. Open **QNX Software Center**:
    <https://www.qnx.com/download/group.html?programid=29178>.

## 2. Install SDP + Quick Start Target Image (QSTI) for QEMU

### 2.1 Install QNX Software Center

Download the installer from the QNX site and install it without `sudo`:

```bash
sh qnx-setup-2.0.4-202501021438-linux.run --nox11 -- -y -d -t "$HOME/qnx"
```

It is installed in `~/qnx/qnxsoftwarecenter/qnxsoftwarecenter`. To open it:

```bash
~/qnx/qnxsoftwarecenter/qnxsoftwarecenter
```

### 2.2 Install the products

In QNX Software Center install:

- **QNX Software Development Platform 8.0** (`qcc`, headers, libs).
- The **`com.qnx.qnx800.quickstart.qemu`** package, which contains the
  official *Quick Start Target Image* (QSTI) for QEMU.

> This is the path documented by QNX in the "Quick Start Target Image (QSTI)
> for QEMU" guide:
> <https://www.qnx.com/developers/docs/qnxeverywhere/com.qnx.doc.target_images/topic/qsti_qemu/getting_started.html>

## 3. Unpack the image

The package leaves the files in `~/qnx800/images/qemu`. Run the script it
includes:

```bash
cd ~/qnx800/images/qemu
./unpack_qemu_image.sh
```

When it finishes, `~/qnx800/images/qemu/qemu/output/` contains:

- `ifs.bin` — QNX kernel/image
- `disk-qemu.vmdk` — VM disk

`run-qnx.sh` finds them automatically at that path. If they are elsewhere:

```bash
QNX_KERNEL=/path/ifs.bin QNX_DISK=/path/disk-qemu.vmdk ./run-qnx.sh
```

## 4. Build and boot

```bash
# 1) cross-compile control_core (needs qnxsdp-env.sh)
QNX_ENV=~/qnx800/qnxsdp-env.sh QNX_ARCH=x86_64 ./build-control-core.sh

# 2) boot QNX in QEMU (headless, user-mode network)
QNX_SHARE_IF=ide ./run-qnx.sh

#    if you want the QNX desktop (SDL/GL), with QEMU 10 + virgl:
QNX_GUI=1 ./run-qnx.sh

#    if you prefer a bridged network (requires root and virbr0):
sudo QNX_NET=bridge ./run-qnx.sh
```

Inside QNX, the shared disk appears as an `hd*` device. Mount the FAT (adjust
the name according to `ls /dev/hd*`):

```sh
mkdir -p /data/share
mount -t dos /dev/hd1t6 /data/share
cd /data/share
sh bench-inside-qnx.sh
```

If QNX does not see the `vvfat` disk, boot with `QNX_SHARE_IF=ide` or copy the
binaries over `scp` to port 2222 (if sshd is running in the VM).

> `/mnt` does not exist in the QSTI: use `/data/share`. The FAT does not allow
> executing binaries, so copy the `.qnx` files to `/tmp` and run them there.

## 5. Simulated UR server (host)

`control_core` on QNX can talk to the real robot or to a test double. On the host:

```bash
python3 mock_ur_server.py --host 0.0.0.0 --port 30003
```

Inside QNX, the QEMU host (user networking) is `10.0.2.2`:

```sh
./control_core.qnx --robot-ip 10.0.2.2 --endpoint /tmp/vcr_control.sock --with-telemetry
```

The type-64 telemetry parser is pinned to the deterministic mock; the real
URSim stream is big-endian with different offsets, so `--with-telemetry` is not
verified against URSim in this repository.

## 6. What the demo measures

- `bench_client.qnx` → `bench_qnx.csv` (RTT min/mean/max/stddev, p99).
- `bench_client` POSIX on Linux → `bench_linux.csv`.
- The Linux vs QNX comparison uses the **same client binary** recompiled for each
  target and the **same 108-byte message type**.

Measured in this submission: 2000 commands at 20 Hz on QNX, p99 0.238 ms vs
0.408 ms on Linux; IPC mean 0.025 ms vs 0.078 ms.

Honest limitation: QEMU is not an RTOS on real hardware, so the absolute jitter
does not represent a physical QNX board. It validates the port, the message path
and the architecture; for a "production" figure, repeat it on a compatible
x86_64/ARM board (a Raspberry Pi 4/5 or a cheap mini-PC) or the judges' hardware.
