"""Qualify immutable worker variants; oversubscription is not a scaling verdict."""
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path("/tmp/frontier-workers")
EVIDENCE = Path("/tmp/evidence")
BIN = Path("/tmp/bin")
SOURCES = json.loads(Path("experiments/frontier/worker-sources.json").read_text())
VARIANTS = [row for row in SOURCES if row["label"] != "official"]


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def build():
    for path in (ROOT, EVIDENCE, BIN):
        path.mkdir(exist_ok=True)
    machine = {"logical_cpus": os.cpu_count(), "affinity": sorted(os.sched_getaffinity(0))}
    quota = Path("/sys/fs/cgroup/cpu.max")
    if quota.exists():
        machine["cpu_max"] = quota.read_text().strip()
    print(json.dumps(machine), flush=True)
    (EVIDENCE / "machine.json").write_text(json.dumps(machine, indent=2) + "\n")
    (EVIDENCE / "sources.json").write_text(json.dumps(SOURCES, indent=2) + "\n")
    for row in SOURCES:
        context = ROOT / row["label"]
        context.mkdir(exist_ok=True)
        source = subprocess.check_output(["git", "show", row["sha"] + ":track-b/main.cpp"])
        (context / "main.cpp").write_bytes(source)
        (context / "Dockerfile").write_bytes(Path("track-b/Dockerfile").read_bytes())
        run("docker", "build", "--network=none", "-t", "frontier:" + row["label"], context)
    run("gcc", "-O2", "-static", "/tmp/organizer/harness/timer/sd-timer.c", "-o", BIN / "sd-timer")


def gates():
    wrapper = Path("experiments/frontier/docker-exact.py").resolve()
    wrapper.chmod(0o755)
    for row in VARIANTS:
        label = row["label"]
        print("EXACTNESS", label, row["sha"], flush=True)
        link = BIN / ("frontier-" + label)
        link.symlink_to(wrapper)
        run("python3", "tests/differential.py", link)
        run("python3", "tests/frontier_adversarial.py", link)
        source = ROOT / label / "main.cpp"
        common = ["g++", "-std=c++20", "-O1", "-g", "-march=x86-64-v3", "-pthread",
                  "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", source]
        sanitized = BIN / (label + "-asan")
        run(*common, "-fsanitize=address,undefined", "-o", sanitized)
        run("python3", "tests/differential.py", sanitized)
        run("python3", "tests/frontier_adversarial.py", sanitized)
        threaded = BIN / (label + "-tsan")
        run(*common, "-fsanitize=thread", "-o", threaded)
        run("python3", "tests/frontier_adversarial.py", threaded,
            env=dict(os.environ, TSAN_OPTIONS="halt_on_error=1"))


def corpus():
    for seed in (1, 2, 3):
        run("python3", "/tmp/organizer/generators/b_index.py", "--seed", seed,
            "--scale", "full", "--out", f"/tmp/data-{seed}")
        if seed != 1:
            run("python3", "/tmp/organizer/reference/b_index.py", "--data",
                f"/tmp/data-{seed}", "--out", f"/tmp/ref-{seed}")
    run("python3", "/tmp/organizer/generators/b_index.py", "--seed", "1",
        "--scale", "sample", "--out", "/tmp/sample")
    run("python3", "/tmp/organizer/generators/b_index.py", "--seed", "7",
        "--scale", "tiny", "--out", "/tmp/tiny")
    run("python3", "/tmp/organizer/reference/b_index.py", "--data", "/tmp/tiny", "--out", "/tmp/ref-tiny")


def benchmark():
    import hashlib
    images = ["frontier:" + row["label"] for row in SOURCES]
    for seed in (1, 2, 3):
        expected = "f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927"
        if seed != 1:
            expected = hashlib.sha256(Path(f"/tmp/ref-{seed}/results.txt").read_bytes()).hexdigest()
        run("python3", "experiments/frontier/compare.py", "--data", f"/tmp/data-{seed}",
            "--expected", expected, "--timer", BIN / "sd-timer", "--seed", seed,
            "--results", EVIDENCE / "measurements.jsonl", *images)
    for scale, seed, expected in [
        ("sample", 1, "eeeec1e9319da98a6f3c664c78f93e7078fd603c25c2bb3027174e81e3638a8f"),
        ("tiny", 7, hashlib.sha256(Path("/tmp/ref-tiny/results.txt").read_bytes()).hexdigest()),
    ]:
        run("python3", "experiments/frontier/compare.py", "--data", "/tmp/" + scale,
            "--expected", expected, "--timer", BIN / "sd-timer", "--seed", seed,
            "--repeat", "1", "--results", EVIDENCE / (scale + ".jsonl"), *images)


def summarize():
    sys.path.insert(0, "/tmp/organizer/harness")
    from scoring import k_from_measurements
    ref = json.loads(Path("/tmp/organizer/harness/calibration-seed1.json").read_text())["tracks"]["b"]
    reference = dict(T=ref["T_ref_s"], M=ref["M_ref_bytes"], S=91, D=1)
    measurements = [json.loads(line) for line in (EVIDENCE / "measurements.jsonl").read_text().splitlines()]
    summary = []
    for row in SOURCES:
        count_dir = ROOT / ("count-" + row["label"])
        count_dir.mkdir(exist_ok=True)
        source = ROOT / row["label"] / "main.cpp"
        (count_dir / "main.cpp").write_bytes(subprocess.check_output(["clang-format", "--style=LLVM", str(source)]))
        (count_dir / "Dockerfile").write_bytes(Path("track-b/Dockerfile").read_bytes())
        static = json.loads(subprocess.check_output([str(BIN / "scc"), "--format", "json", str(count_dir)]))
        (EVIDENCE / ("static-" + row["label"] + ".json")).write_text(json.dumps(static) + "\n")
        for seed in (1, 2, 3):
            selected = [m for m in measurements if m["seed"] == seed and m["image"] == "frontier:" + row["label"]]
            assert len(selected) == 3, (row["label"], seed, len(selected))
            axes = dict(T=statistics.median(m["T"] for m in selected), M=max(m["M"] for m in selected),
                        S=sum(item["Code"] for item in static), D=1)
            entry = dict(seed=seed, **row, **axes, K_local=k_from_measurements(reference, axes))
            print(json.dumps(entry), flush=True)
            summary.append(entry)
    (EVIDENCE / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("K_local uses runner measurements. Oversubscribed timings do not predict scoring-box scaling.")


{"build": build, "gates": gates, "corpus": corpus, "benchmark": benchmark, "summarize": summarize}[sys.argv[1]]()
