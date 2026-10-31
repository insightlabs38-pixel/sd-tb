#!/usr/bin/env python3
"""Run a differential fixture through the pinned production image."""
from pathlib import Path
import subprocess
import sys

label = Path(sys.argv[0]).name.removeprefix("frontier-")
data = Path(sys.argv[sys.argv.index("--data") + 1])
out = Path(sys.argv[sys.argv.index("--out") + 1])
data.chmod(0o755)
for path in data.iterdir():
    path.chmod(0o644)
out.chmod(0o777)
subprocess.run([
    "docker", "run", "--rm", "--network=none", "--read-only", "--user=65534:65534",
    "--cap-drop=ALL", "--memory=8g", "--memory-swap=8g", "--pids-limit=512",
    "--tmpfs", "/tmp:rw,noexec,nosuid,size=1g", "-v", f"{data}:/data:ro",
    "-v", f"{out}:/out:rw", "frontier:" + label,
], check=True)
