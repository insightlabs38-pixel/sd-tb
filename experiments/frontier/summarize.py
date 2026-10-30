"""Apply the frozen coefficient to local paired production measurements."""

import json
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
sys.path.insert(0, "/tmp/organizer/harness")
from scoring import k_from_measurements

calibration = json.loads(
    Path("/tmp/organizer/harness/calibration-seed1.json").read_text()
)["tracks"]["b"]
reference = dict(T=calibration["T_ref_s"], M=calibration["M_ref_bytes"], S=91, D=1)
rows = [
    json.loads(line) for line in (root / "measurements.jsonl").read_text().splitlines()
]
summary = []
for seed in sorted({row["seed"] for row in rows}):
    coefficients = {}
    for variant in ("official", "development", "candidate"):
        selected = [
            row
            for row in rows
            if row["seed"] == seed and row["image"] == "frontier:" + variant
        ]
        static = json.loads((root / ("static-" + variant + ".json")).read_text())
        axes = dict(
            T=statistics.median(row["T"] for row in selected),
            M=max(row["M"] for row in selected),
            S=sum(language["Code"] for language in static),
            D=1,
        )
        coefficient = k_from_measurements(reference, axes)
        coefficients[variant] = coefficient
        entry = dict(seed=seed, variant=variant, **axes, K_local=coefficient)
        summary.append(entry)
        print(json.dumps(entry))
    assert coefficients["candidate"] > coefficients["development"], coefficients
    print(
        "seed",
        seed,
        "K ratio to official:",
        coefficients["candidate"] / coefficients["official"],
    )
print(
    "K_local uses board reference axes with runner measurements; it is not an official score."
)
(root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
