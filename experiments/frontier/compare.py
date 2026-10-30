"""Measure pinned images with the organizer timer and warmed inputs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("--data", type=Path, required=True)
p.add_argument("--expected", required=True)
p.add_argument("--timer", type=Path, required=True)
p.add_argument("--results", type=Path, required=True)
p.add_argument("--seed", type=int, required=True)
p.add_argument("--repeat", type=int, default=3)
p.add_argument("images", nargs="+")
a = p.parse_args()
if os.geteuid() != 0:
    p.error("Run with sudo so the protected timing directory is owned by container root")
for path in a.data.iterdir():
    with path.open("rb") as f:
        while f.read(8 << 20):
            pass
for repeat in range(a.repeat):
    images = a.images[repeat % len(a.images) :] + a.images[: repeat % len(a.images)]
    for image in images:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out, timing = root / "out", root / "timing"
            out.mkdir(mode=0o777)
            out.chmod(0o777)
            timing.mkdir(mode=0o700)
            command = [
                "docker",
                "run",
                "--rm",
                "--no-healthcheck",
                "--user=0:0",
                "--network=none",
                "--read-only",
                "--memory=8g",
                "--memory-swap=8g",
                "--pids-limit=512",
                "--tmpfs",
                "/tmp:rw,size=1g,exec",
                "--cap-drop=ALL",
                "--cap-add=SETUID",
                "--cap-add=SETGID",
                "--security-opt=no-new-privileges",
                "--ulimit",
                "nofile=8192:8192",
                "-v",
                f"{a.data.resolve()}:/data:ro",
                "-v",
                f"{out}:/out:rw",
                "-v",
                f"{timing}:/sd:rw",
                "-v",
                f"{a.timer.resolve()}:/sd-timer:ro",
                "-e",
                "TRACK=b",
                "--entrypoint",
                "/bin/sh",
                image,
                "-c",
                "/sd-timer /sd/time.json 65534 65534 -- /solution; "
                'result=$?; cat /sys/fs/cgroup/memory.peak > /sd/memory; exit "$result"',
            ]
            subprocess.run(command, check=True, timeout=180)
            measured = json.loads((timing / "time.json").read_text())
            digest = hashlib.sha256((out / "results.txt").read_bytes()).hexdigest()
            assert measured["exit_code"] == 0 and measured["signal"] == 0
            assert digest == a.expected, (image, digest, a.expected)
            row = dict(
                seed=a.seed,
                repeat=repeat + 1,
                image=image,
                T=measured["wall_ns"] / 1e9,
                M=int((timing / "memory").read_text()),
                sha256=digest,
            )
            print(json.dumps(row), flush=True)
            with a.results.open("a") as f:
                f.write(json.dumps(row) + "\n")
