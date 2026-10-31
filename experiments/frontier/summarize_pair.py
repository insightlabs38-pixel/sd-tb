"""Apply the frozen coefficient to paired champion/candidate measurements."""

import json
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
sys.path.insert(0, "/tmp/organizer/harness")
from scoring import k_from_measurements

calibration = json.loads(Path("/tmp/organizer/harness/calibration-seed1.json").read_text())["tracks"]["b"]
reference = dict(T=calibration["T_ref_s"], M=calibration["M_ref_bytes"], S=91, D=1)
rows = [json.loads(line) for line in (root / "measurements.jsonl").read_text().splitlines()]
summary = []
for seed in sorted({row["seed"] for row in rows}):
    k = {}
    for variant in ("champion", "candidate"):
        selected = [r for r in rows if r["seed"] == seed and r["image"] == "frontier:" + variant]
        static = json.loads((root / ("static-" + variant + ".json")).read_text())
        axes = dict(
            T=statistics.median(r["T"] for r in selected),
            M=max(r["M"] for r in selected),
            S=sum(language["Code"] for language in static),
            D=1,
        )
        k[variant] = k_from_measurements(reference, axes)
        entry = dict(seed=seed, variant=variant, runs=len(selected), **axes, K_local=k[variant])
        summary.append(entry)
        print(json.dumps(entry))
    print("seed", seed, "K ratio candidate/champion:", k["candidate"] / k["champion"])
(root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
