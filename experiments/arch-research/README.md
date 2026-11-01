# Track B architecture research (paused November 1)

Unfinished or rejected experiments from the compact-engine campaign, kept for reuse. None of these files is judged source; the candidate lives on `frontier/b-architecture-compact`.

## Variants (each relative to the compact engine at that point)

| File | Idea | Result |
|---|---|---|
| `V13.cpp` | Precomputed bitmap of hot-guarded queries instead of walking guard lists | Neutral single-core; extra evaluations offset the saved marking |
| `V14.cpp` | Cold columns numbered by sample frequency | L2-share proxy 48.6M to 47.9M misses; no timing change |
| `V15.cpp` | 1,024-document blocks | L2 misses -1.9M, but +0.22G instructions and +6M mispredicts |
| `V16.cpp` | 2 MiB five-way dictionary, 32-bit metadata, unified term numbering | Exact; L2 misses unchanged (48.8M); slower |
| `V17.cpp` | Branch-free keys up to 12 bytes fused with hash and prefetch | -2.5% instructions on sample; not measurably faster |
| `V18.cpp` | Lookup four tokens ahead with row prefetch in one loop | More instructions, slower |

## Prototypes and ablations (timing only, outputs deliberately wrong)

Single core, full seed 1, minimum of 5 (current engine 0.851 s): no evaluation 0.649, no cold pass 0.571, no hot-row update 0.680, no phrases 0.792, no lookup or anything after it 0.262; `lookuponly.cpp` (tokenize, key, hash, bucket lookup) 0.41 s. `gscan2.cpp` scans for a global design (dense terms to whole-corpus bitmaps, sparse terms deduplicated and appended) in 0.73 s before any evaluation or postings build; its L2 proxy showed 36.0M misses, of which 8.8M came from 32 KiB-stride set aliasing of the bitmap rows. `gscan3c.cpp` pads that stride; its proxy run was stopped unfinished and is the first thing to re-run if the global design is revisited. `fused_tokenizer_bench.cpp` showed a branchy single-pass tokenizer is slower than the edge list (0.25 s vs 0.21 s; its token count also differs, so it has a cross-window bug).

## Tools

`ab.sh` interleaved timing (median and minimum), `mem.sh` cgroup peak in Docker (run with THP=always for box-like results), `cgcost.sh` / `cgl2.sh` cachegrind cost proxy (`cgl2.sh` uses a 640 KiB last level, a per-thread share of a box P-core L2), `gmodel.py` cost model for global bitmap/postings evaluation, `guard.py` guard-pruning simulation, `stats.py` term statistics, `phase.py` phase-lap instrumentation, `k.py` official K. Paths inside the scripts point at the original scratch directory and need adjusting.
