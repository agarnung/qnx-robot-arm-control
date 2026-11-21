#!/usr/bin/env python3
"""Convert the evidence/ logs into terminal MP4 clips for the video.

- Linux: evidence/linux-baseline.log
- QNX:   evidence/qnx-session.log

Output: evidence/clips/<clip>.mp4 (1280x720, H.264).
The clips are animated transcripts of the real captured output, not a real-time
recording: the video must say so.

Usage: ./evidence/render-clips.py
"""
import os
import re
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "clips")
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

W, H = 1280, 720
MARGIN = 22
LINE_H = 22
HEADER_H = 40
FPS = 30

BG = (16, 20, 24)
HEADER = (28, 34, 42)
TEXT = (216, 222, 233)
CMD = (136, 192, 120)
DIM = (120, 130, 145)
ACCENT = (94, 129, 172)
CURSOR = (216, 222, 233)

ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b\][^\x07]*\x07|\x1b[()][A-Za-z0-9]")


REPO = os.path.dirname(HERE)
HOME = os.path.expanduser("~")
REPLACEMENTS = [
    (REPO, "~/visual_control_robot-qnx"),
    (os.path.join(HOME, "qnx800"), "/opt/qnx800"),
    (HOME, "/home/dev"),
    (re.compile(r"[A-Za-z0-9._-]+@10\.0\.2\.2"), "dev@10.0.2.2"),
]


def clean(line: str) -> str:
    line = ANSI.sub("", line.replace("\r", ""))
    for old, new in REPLACEMENTS:
        line = line.replace(old, new)
    if "QEMU: Terminated" in line:
        return ""
    return line


def read_lines(path: str) -> list[str]:
    with open(path, encoding="utf-8", errors="replace") as fh:
        return [clean(line.rstrip("\n")) for line in fh]


def split_markers(lines: list[str]) -> dict[str, list[str]]:
    segs: dict[str, list[str]] = {}
    cur = None
    for line in lines:
        m = re.search(r"@@CLIP:([A-Za-z0-9_-]+)@@", line)
        if m:
            cur = m.group(1)
            segs.setdefault(cur, [])
            continue
        if cur is not None:
            segs[cur].append(line)
    for seg in segs.values():
        while seg and not seg[0].strip():
            seg.pop(0)
        while seg and not seg[-1].strip():
            seg.pop()
    return segs


def login_segment(lines: list[str], tail: int = 40) -> list[str]:
    start = None
    for i, line in enumerate(lines):
        if re.search(r"login:", line):
            start = i
            break
    if start is None:
        return []
    seg: list[str] = []
    for line in lines[start:]:
        if "@@CLIP:07-qnx-login@@" in line:
            break
        seg.append(line)
    while seg and not seg[0].strip():
        seg.pop(0)
    while seg and not seg[-1].strip():
        seg.pop()
    return seg[-tail:]


def boot_segment(lines: list[str], tail: int = 22) -> list[str]:
    for i, line in enumerate(lines):
        if re.search(r"login:", line) or re.search(r"[Pp]assword:", line):
            return lines[max(0, i - tail):i]
    return lines[-tail:]


def wrap(lines: list[str], cols: int) -> list[str]:
    out: list[str] = []
    for line in lines:
        if not line:
            out.append("")
            continue
        while len(line) > cols:
            out.append(line[:cols])
            line = line[cols:]
        out.append(line)
    return out


def frame_states(lines: list[str], cps: int = 3, pause: int = 5) -> list[tuple[list[str], str]]:
    states: list[tuple[list[str], str]] = []
    done: list[str] = []
    for line in lines:
        for i in range(0, len(line) + 1, cps):
            states.append((list(done), line[:i]))
        done.append(line)
        states.extend((list(done), "") for _ in range(pause))
    states.extend((list(done), "") for _ in range(FPS * 2))
    return states


def draw(state, title: str, index: int, total: int, font, font_bold, visible: int, cols: int):
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, HEADER_H], fill=HEADER)
    d.text((MARGIN, 10), title, font=font_bold, fill=TEXT)
    d.text((W - MARGIN - 90, 10), f"clip {index}/{total}", font=font_bold, fill=DIM)

    done, partial = state
    shown = done + ([partial] if partial else [])
    shown = shown[-visible:]
    y = HEADER_H + 14
    for line in shown:
        if line.startswith("$ ") or line.startswith("[root@"):
            color = CMD
        elif line.startswith("OK:") or line.startswith("[ok]"):
            color = ACCENT
        else:
            color = TEXT
        d.text((MARGIN, y), line, font=font, fill=color)
        y += LINE_H

    if shown:
        cx = MARGIN + min(len(shown[-1]), cols) * font.getlength("M")
        cy = y - LINE_H
        d.rectangle([cx + 1, cy + 2, cx + 10, cy + LINE_H - 4], fill=CURSOR)
    return img


def render_clip(name: str, title: str, lines: list[str], static: bool, index: int, total: int):
    font = ImageFont.truetype(FONT_PATH, 18)
    font_bold = ImageFont.truetype(FONT_BOLD, 18)
    cols = int((W - 2 * MARGIN) / font.getlength("M"))
    visible = (H - HEADER_H - 26) // LINE_H
    lines = wrap(lines, cols)

    tmp = tempfile.mkdtemp(prefix=f"vcr-{name}-", dir="/tmp/opencode")
    try:
        if static:
            img = draw((lines, ""), title, index, total, font, font_bold, visible, cols)
            img.save(os.path.join(tmp, "00000.png"))
            frames = FPS * 2
            for i in range(1, frames):
                shutil.copyfile(os.path.join(tmp, "00000.png"),
                                os.path.join(tmp, f"{i:05d}.png"))
        else:
            states = frame_states(lines)
            for i, state in enumerate(states):
                draw(state, title, index, total, font, font_bold, visible, cols).save(
                    os.path.join(tmp, f"{i:05d}.png"))

        out = os.path.join(OUT, f"{name}.mp4")
        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS),
             "-i", os.path.join(tmp, "%05d.png"), "-c:v", "libx264",
             "-pix_fmt", "yuv420p", "-crf", "20", "-movflags", "+faststart", out],
            check=True,
        )
        print(f"[clip] {name}.mp4")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> None:
    os.makedirs(OUT, exist_ok=True)

    linux = split_markers(read_lines(os.path.join(HERE, "linux-baseline.log")))
    qnx_lines = read_lines(os.path.join(HERE, "qnx-session.log"))
    qnx = split_markers(qnx_lines)

    clips = [
        ("01-linux-build", "Linux build + QNX toolchain", linux.get("01-linux-build", []), False),
        ("02-linux-tests", "Linux tests", linux.get("02-linux-tests", []), False),
        ("03-linux-hardening", "ASan/UBSan + gcc -fanalyzer", linux.get("03-linux-hardening", []), False),
        ("04-linux-bench", "Linux benchmark: 500 cmds @ 20 Hz", linux.get("04-linux-bench", []), False),
        ("05-linux-compare", "Linux vs QNX comparison", linux.get("05-linux-compare", []), False),
        ("06-qnx-boot", "QNX 8.0 boot (QSTI, QEMU)", boot_segment(qnx_lines), True),
        ("07-qnx-login", "QNX console login", login_segment(qnx_lines), False),
        ("08-qnx-version", "uname / pidin / disks", qnx.get("08-qnx-version", []), False),
        ("09-qnx-mount", "mount shared FAT disk", qnx.get("09-qnx-mount", []), False),
        ("10-qnx-bench", "QNX benchmark (recorded session)", qnx.get("10-qnx-bench", []), False),
        ("11-qnx-csv", "QNX CSV output", qnx.get("11-qnx-csv", []), False),
    ]

    for i, (name, title, lines, static) in enumerate(clips, 1):
        if not lines:
            print(f"[skip] {name}: no data")
            continue
        render_clip(name, title, lines, static, i, len(clips))

    readme = os.path.join(OUT, "README.md")
    with open(readme, "w", encoding="utf-8") as fh:
        fh.write("# Evidence clips\n\n")
        fh.write("Animated transcripts of the real output captured in\n")
        fh.write("`../linux-baseline.log` and `../qnx-session.log`. They are not a\n")
        fh.write("real-time recording: the video must say the clips are an animation\n")
        fh.write("of the real session. The full benchmark (2000 samples) is in\n")
        fh.write("`results/qnx_bench.csv`.\n\n")
        fh.write("| Clip | Content | Suggested use |\n|---|---|---|\n")
        for name, title, lines, static in clips:
            if lines:
                fh.write(f"| `{name}.mp4` | {title} | Video B-roll |\n")
    print(f"[ok] clips in {OUT}")


if __name__ == "__main__":
    main()
