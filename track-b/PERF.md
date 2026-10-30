# Track B: performance log

The independent development repository is not the official submission. Board run IDs and official K values must be recorded by the official integrator after verified promotions.

| Step | Change | Board run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | Organizer Python reference | Not submitted here | 56.35 | 2,200 | Not measured here |
| 1 | Selective postings plus token-ID tape, exact phrase verification | Pending official run | Pending | Pending | Pending |
| 2 | Shared deduplicated token automaton for high-frequency phrases | Pending official run | Pending | Pending | Pending |

## Local, non-scoring evidence (synthetic inputs)

On the same in-chat Linux host and 100,000-document/20,000-query synthetic workload (75,307,568 input document bytes), baseline wall time was 22.97 s and peak RSS 205,608 KiB. The automaton variant took 3.05 s and peak RSS 206,108 KiB, with identical SHA-256 output (`ebe9a652d36670f1fa07f61a0e0121dbdac27c34cc289ab4ec86418f8abb097a`). This is a 7.53x local speedup on this synthetic workload, not an organizer benchmark.

On a separate 250,000-document/50,000-query synthetic workload (188,596,118 document bytes), the initial automaton crossover took 12.81 s with peak RSS 505,532 KiB. A measured 500-document posting threshold improved this to 6.90 s with peak RSS 509,272 KiB on the same input and identical SHA-256. These inputs differ from the official generator and must not be used as official scores.

The 500-document crossover also reduced the 100,000-document synthetic workload to 2.74 s with 206,400 KiB peak RSS, again retaining the exact prior output digest. Threshold sweeps on the 250,000-document synthetic input measured 7.22 s at 0, 7.42 s at 250, 6.80 s at 500 and 10.95 s at 1,000; these are host-local timings, not board scores, and the chosen threshold remains subject to official-corpus verification.

## Public organizer-generator exactness, GitHub Actions

[GitHub Actions run 41081539702](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41081539702), completed 2026-10-30, verified the 500-document crossover source (Git blob `d4b81fa061aae23c4a2e01fcb8e7368805e7c37c`). Seed-1 `sample` and `full` output SHA-256 matched the organizer's published hashes, sample seeds 2 and 3 matched the organizer's Python reference byte-for-byte, and the pinned offline Docker build produced the correct sample output as uid 65534 under a read-only root filesystem. Seven deterministic local differential seeds passed in the same job.

On that **GitHub-hosted runner, not the scoring machine**, the compiled candidate took 0.21 s at `sample` (maximum RSS 66,908 KiB) and 5.57 s at `full` (maximum RSS 661,156 KiB). These are single-process `/usr/bin/time -v` measurements and are **not official board scores, cgroup memory peaks or run IDs**. The official board entry remains pending Luna's promotion.

## Verified flat-dictionary promotion (October 30)

[One-shot CI run 41084801459](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41084801459) compared the frozen champion and a cache-local flat query dictionary on the same official seed-1 full corpus, using three interleaved runs each. Baseline wall: **5.77, 5.51, 5.48 s**; flat dictionary: **4.49, 4.41, 4.43 s**, a 20.5% mean reduction on this runner. Peak RSS increased from about 661,200 KiB to 665,840 KiB. All six full outputs matched the organizer's SHA-256; seven differential seeds, the common-phrase automaton oracle and sample seed-2 Python reference comparison passed. This is GitHub runner evidence, **not** an official board score. Promoted source blob: `89fdf6d010e1b5bc200c53c7375ca235ddf278b6`.
