# Track B independent full-engine architecture campaign (November 1, 2026)

**Status: in progress; no qualified candidate handed to Luna.**
All engines fork from `main` commit `e0d67d08bb505c0839f0e01660f88132ce56b502`;
the source at `track-b/main.cpp` has blob `0bdeeb625cb9e8b6b9d4555d8a7abcb84a04d13d`.
No changes to main, scheduled developer branches, or `sd-sm`.

## Official scoring reference (2026-11-01 08:24 UTC snapshot)
HighVelocity B BEST: K=26.690516, T=0.159821s, M=58,052,608 bytes,
S=1111, D=1, run `run-1d48fc96e1ee`, integration source commit `033819b04f41e7a02d02ab2b1e86241db716fc20`.
Organizers' coefficient has T^-0.50, M^-0.20 (memory factor capped at 50x),
S^-0.15 (cannot reward smaller than reference), D^-0.15.
Comparable competitor: Cause Of Death B K=37.235722, T=0.100386s,
M=42,561,536B, S=823, D=1 (board run `run-3eb48513bdc1`).
**Competitor source not independently verified**, so this is score evidence only.
Public source cross-check: Cache Me's repo `manusingh090/speed-demon-2026-entry`,
board commit `c1ec48304e3f2de5eafd73312caecd53cb494455` has B BEST
K=29.28357, T=0.17206s, M=47,489,024B, S=612.
Its public Track B source blob `96124f0bf745ab99d1bc2ed9b6b41e61e735f43f`.
This code does **not** demonstrate K35–38.

## Prior evidence and motivation
`research/b-index-architecture-probes-20261031` demonstrated sparse
query-aware posting speed only for synthetic, already-tokenized Boolean kernels.
`experiments/frontier/B-CLAUDE-FRONTIER.md` measured a full exact postings
solver at 941ms scan/log +239ms CSR +187ms bitmap construction +176ms
Boolean evaluation, vs ~740ms for a streaming design on the same pinned core.
The fundamental bound was construction work and memory, not Boolean lookup.

## Complete C++ engines (all share the exact champion input/phrase/output)

| Engine and research branch suffix | CI | Paired runner-median speed vs champion | Status |
| --- | --- | --- | --- |
| compiled-gates | [41289966207](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41289966207) | +0.3%, -2.0%, -0.7% | Exact; not promoted |
| inverted-activation | [41289967741](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41289967741) | -3.9%, -3.3%, -3.6% | Exact; not promoted |
| document-major | [41290170972](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290170972) | approx 3.4x slower | Exact; not promoted |
| late-materialization | [41290564523](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290564523) | -10.8%, -7.5%, -6.5% | Exact; not promoted |
| phrase-id-fusion | [41290952082](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290952082) | -9.6%, -10.7%, -10.7% | Exact; not promoted |
| prefix-direct-bitmap | [41291786946](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41291786946) | pending | Corrected one-probe prefix predicate |
| inverted-touched | [41290878805](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290878805) | pending | Sparse activation repair |
| prefix-predicate | [41291069032](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41291069032) | -5.0%, -6.5%, -11.4% | Exact; not promoted |
| selective-document-exec | [41291300267](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41291300267) | -37.2%, -35.5%, -34.8% | Exact; not promoted |

Full-run CI generates official seed 1 and independent seed 2/3 corpus/reference,
passes published SHA for seed1, applies 5 paired pinned-Docker full runs per seed,
and checks the organizer timer plus `cgroup/memory.peak`. CI also performs
seven small differential seeds, adversarial CRC/tokenization/Boolean tests,
ASan/UBSan, TSan, exact sample/tiny outputs, and uploads JSON evidence.
**Runner times and ratios are not official K; source size S is not yet canonical.**

## Findings
1. Full per-document query dispatch spends far more than bitmap/block evaluation;
   the all-document-major design did not amortize watchers.
2. Boolean DNF feasibility conditions are exact but cheap block filtering itself
   does not repay compilation and per-query overhead.
3. Hybrid sparse-anchor per-query probing costs more than the existing
   five-inline-offset containers, even when it produces exact answers.
4. Replacing phrase token fingerprint checks with integer IDs introduces
   36M extra token writes; the full-run regression outweighs small memory savings.
5. Two-probe query prefix filtering before long-token hashing passed exactness,
   but the redundant second hash and Bloom memory probes lost 5–11% wall time.
   An optimized variant reuses the already available token CRC and probes one
   bitset location, eliminating that overhead. Its run is pending.
6. Replacing *all* document-major execution with only rare-trigger queries
   still lost ~35% in the end-to-end run, including larger worker memory. The
   per-document membership and watcher pass is too expensive to add to this
   already-vectorized streaming pipeline.

## Handoff criteria
No integration based on synthetic-only, single-seed, or unpaired timing.
Require consistent runner K gains on >=3 independent full seeds, canonical
clang-format/scc S count, D count, all sanitizers and exact input/outputs,
then independent Luna/board qualification. Preserve SHA of each qualifying blob.
