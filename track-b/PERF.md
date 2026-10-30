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

## Final-AND Boolean anchor hybrid (October 30)

[Two-full-seed CI run 41085284449](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41085284449) compared the flat-dictionary champion to a hybrid that probes the rarest mandatory AND term after the last OR, preserving left-to-right semantics. Full seed 1: flat **5.15/5.10 s**, combined **4.77/4.85 s**; full seed 2: flat **4.23/4.12 s**, combined **3.96/4.03 s**. Mean improvements were 6.1% and 4.3%, with essentially unchanged peak RSS. Both full seeds matched their respective organizer reference (seed 1 published SHA-256, seed 2 Python reference). Seven randomized differential seeds, repeated/suffix phrase oracle, sample seed-2 reference and pinned offline Docker build as uid 65534 on a read-only root filesystem passed. Promoted source blob: `2855b570818ef7f48370d200885dbfa89f35bff3`. Runner measurements only; no official board K or run ID claimed.

## Compact 32-bit dictionary fingerprints (October 30)

[Full-seed comparison 41086157114](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41086157114) measured the combined champion versus a compact 8-byte hash slot using collision-safe full token comparison. Seed 1 full: champion **5.33/5.54 s**, compact **5.12/5.08 s**; seed 2 full: champion **4.94/4.92 s**, compact **4.76/4.71 s**. Mean runtime gains were 6.2% and 4.0%, while peak RSS fell by roughly 4 MiB. Full seed 1 matched the published SHA-256 and seed 2 matched the independent Python reference; randomized differential tests passed. [Regression CI 41087026631](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41087026631) additionally passed deliberately colliding FNV32 token keys, ASan/UBSan, organizer sample hash, and pinned offline Docker execution as uid 65534 on a read-only root filesystem. Source blob after unused-hash cleanup: `a5637aae0e87182b9daef8a35496bbb83ff7a9f6`. All timings are runner-local, not board scores.

## AC root-transition direct table (October 30)

[Paired full-seed comparison 41087839664](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41087839664) compared the verified 32-bit dictionary champion to an AC automaton with direct root transitions and one lookup per non-root state. Seed 1 full: champion **3.62/3.59 s**, challenger **3.19/3.19 s** (11.5% mean faster). Seed 2 full: champion **3.55/3.50 s**, challenger **3.10/3.07 s** (12.5% mean faster). Peak RSS remained about **662 MiB / 654 MiB** on the runner. All seed-1 outputs matched the published SHA-256; seed-2 outputs matched the independent Python reference. Seven differential seeds, fingerprint collision, and common-phrase/suffix tests passed. Promoted source blob: `68bc2544bd842de00269c5797b7e33cb44cd6f54`. These are runner-local times, not official leaderboard scores.

## Short-token direct table challenger (not promoted)

[Two-seed comparison 41088809876](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41088809876) evaluated a collision-free 1–3-byte token lookup table atop the AC-root champion. Seed 1 full: champion **4.25/4.10 s**, challenger **3.91/4.23 s**; seed 2 full: champion **4.26/3.90 s**, challenger **4.15/4.05 s**. The seed-1 mean improved only 2.5% and seed-2 mean regressed 0.5%; RSS increased slightly. All full outputs and differential tests passed, but the inconsistent timing does not justify additional source and maintenance. The AC-root champion remains unchanged.

## Four-worker parallel indexing (October 30)

[Two-full-seed comparison 41089300298](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41089300298) compiled the serial AC-root champion and parallel corpus-index challengers with 2, 4 and 8 worker caps. All 16 full runs were byte-exact against the published seed-1 SHA-256 and the independent seed-2 Python reference; differential, collision and phrase-suffix tests also passed. Seed 1 serial **3.36/3.15 s**, 4-worker **1.97/1.95 s** (39.8% mean faster). Seed 2 serial **3.16/3.23 s**, 4-worker **1.85/2.05 s** (39.0% mean faster). Peak RSS increased from roughly **662 to 767 MiB** on seed 1 and **653 to 764 MiB** on seed 2. The 2-worker variant took **2.26/2.30 s** and **2.32/2.41 s**; 8-worker cap was no faster than 4 on this runner. Promoted source blob: `2fbf4a6ddb6e0ffe43068f5a182a0703e803068c`. Source grew from 472 to 540 physical lines, with no additional runtime dependencies. Runner timings and RSS are not official board scores; final K and judged quality remain subject to Luna's official run and review.

[Parallel sanitizer qualification 41090067395](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41090067395) passed ASan+UBSan and TSan on the official seed-1 sample with the parallel worker path active; both produced the published sample SHA-256. No sanitizer diagnostics were reported. This is a sample-level race/memory-safety gate, not a proof against every possible concurrency defect.

[Parallel Docker/full regression 41090030746](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41090030746) passed the offline pinned Docker build with `-pthread`, unprivileged read-only-root sample execution, seven differential seeds, independent sample seeds 2/3 and the published full seed-1 output hash.

## Parallel query evaluation (October 30)

[Paired full-seed comparison 41090220430](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41090220430) tested four-worker query evaluation with per-worker Boolean scratch and ordered result serialization atop the verified parallel index. Seed 1 full: champion **2.43/2.60 s**, challenger **1.76/1.72 s** (30.8% mean faster). Seed 2 full: champion **2.33/2.37 s**, challenger **1.76/1.72 s** (26.0% mean faster). Peak RSS was effectively unchanged at **766 MiB / 764 MiB**. All full seed-1 hashes, seed-2 Python reference comparisons and differential oracle cases passed, including 1,200-query randomized cases that exercise the threaded query path. Promoted source blob: `39a95d7409c8fcccdf65c99a0ff1a7ad2030077c`. Runner measurements only, not official board scores.

## Independent post-parallel challengers (October 30)

[Parallel AC comparisons 41090575219](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41090575219) and [41091976138](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41091976138) each passed differential and exact two-full-seed checks. The second run measured seed 1 serial **1.88/1.76 s** versus parallel **1.74/1.71 s**, and seed 2 serial **1.98/1.95 s** versus parallel **1.89/1.89 s**. The first run showed smaller but positive improvements. Parallel AC adds 31 nonblank source lines and negligible RSS; combined qualification is required before promotion.

[Adaptive AND comparison 41092226625](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41092226625) passed seven randomized differential seeds and exact full seed 1/2 checks. The 16:1 posting-size crossover measured seed 1 baseline **1.88/1.79 s** versus challenger **1.77/1.78 s**, seed 2 baseline **1.90/2.06 s** versus challenger **1.86/1.99 s**, with unchanged RSS. Ratios 32 and 64 were less consistent. This adds 10 nonblank source lines.

[Rare-anchor phrase comparison 41092768680](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41092768680) passed differential and exact full seed 1/2 checks. Seed 1 baseline **1.78/1.74 s** versus anchored **1.69/1.70 s**; seed 2 baseline **1.71/1.68 s** versus anchored **1.67/1.66 s**, with unchanged RSS. This adds three nonblank source lines. All measurements are GitHub runner timings, not official board results.

## Promoted combined query optimizations (October 30)

[Combined qualification 41093166652](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41093166652) passed seven-seed differential oracles, exact full seed 1/2 outputs and pinned offline Docker as uid 65534 on a read-only root filesystem for both AC+AND and AC+AND+phrase variants. Full seed 1 baseline **1.77/1.76 s** versus AC+AND+phrase **1.75/1.67 s**; full seed 2 baseline **1.79/1.68 s** versus combined **1.65/1.65 s**. [Independent combined repeat 41094072407](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41094072407) passed the same exactness and Docker gates; seed 1 baseline **1.77/1.72 s** versus combined **1.67/1.68 s**, seed 2 baseline **1.70/1.72 s** versus combined **1.65/1.68 s**. The lightweight AND+phrase variant without parallel AC showed inconsistent/no full-run improvement and was not promoted.

Promoted all three tested changes in [commit 57e6b366](https://github.com/insightlabs38-pixel/sd-tb/commit/57e6b36627533e7a1c769cb88c5245a4e1f2dc86), source blob `f036ae8927aaa24342e8e9a0ca9b6fb5301743f9`. Nonblank source lines increased from 538 to 582, peak RSS stayed near 764-766 MiB on the runner, and the combined speed gain across two seeds/two independent runner jobs is modest but repeatable. These are not official board scores.

## Compact vector AC transitions (not promoted, October 30)

[Two-seed comparison 41096477703](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41096477703) passed seven-seed differential testing, published full seed-1 SHA-256 and independent seed-2 Python reference. Three interleaved full runs per seed: seed 1 map **1.71/1.67/1.64 s**, vector **1.69/1.68/1.67 s**; seed 2 map **1.72/1.73/1.73 s**, vector **1.73/1.74/1.76 s**. Vector edges showed no reliable speed or RSS improvement and are not promoted. The verified main.cpp source remains unchanged. Runner-local timings only.

## Branchless ASCII token classification (promoted, October 30)

[Initial comparison 41097255902](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41097255902) passed differential and exact full seeds 1/2. Three-run seed 1 baseline **1.45/1.48/1.44 s** versus token-only **1.40/1.44/1.39 s** (3.2% mean gain); seed 2 baseline **1.40/1.44/1.44 s** versus token-only **1.40/1.42/1.44 s** (0.5% mean gain). Changing both token recognition and ASCII folding was slower and rejected.

[Independent production-flags repeat 41097913555](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41097913555) used `-O3 -flto -march=x86-64-v3`, exact full seed 1/2 and pinned offline Docker under uid 65534 with read-only root. Seed 1 baseline **1.81/1.77/1.77 s** versus token-only **1.71/1.76/1.77 s** (2.1% mean gain); seed 2 baseline **1.79/1.81/1.79 s** versus token-only **1.79/1.78/1.76 s** (1.1% mean gain). Peak RSS was unchanged (~766 MiB / ~764 MiB). The old and new token predicate were exhaustively checked for equivalence on all 256 byte values. Promoted source blob `77afbb2740358a9c137b412ef2af223b3fcc0e27` in [commit c3bfff507](https://github.com/insightlabs38-pixel/sd-tb/commit/c3bfff507371580077be0e864c43a764f9fc58f0). These are runner-local measurements, not official board results.

[Post-promotion exactness run 41098792365](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41098792365) passed on the promoted source: seven randomized differential seeds, organizer sample seed-1 hash, independent sample seeds 2/3, offline pinned Docker as uid 65534/read-only root, and official full seed-1 SHA-256. This qualifies the promoted main.cpp independently of the experiment build.

## Packed token tape and balanced query scheduling (October 30)

[First full-seed comparison 41106201393](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41106201393) passed seven-seed differential checks, official full seed-1 SHA-256, independent full seed-2 reference and pinned offline Docker. Three interleaved runs per seed: seed 1 baseline **1.72/1.74/1.68 s**, 24-bit tape **1.79/1.76/1.76 s**, dynamic 64-query batches **1.66/1.67/1.66 s**; seed 2 baseline **1.77/1.75/1.81 s**, packed **1.82/1.83/1.89 s**, dynamic **1.72/1.77/1.82 s**. Packing lowered peak RSS from ~766 to ~731 MiB on seed 1 and ~764 to ~728 MiB on seed 2, but increased time by 3.3% and 3.9% on average; the combined packed+dynamic variant also lost on full-run K economics. **Packed and combined variants not promoted.**

[Independent work-stealing comparison 41107191820](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41107191820) passed the same full-seed exactness and differential gates plus dynamic-64 Docker. Three-run means: seed 1 baseline **1.757 s**, dynamic-16 **1.710 s**, dynamic-64 **1.730 s**, dynamic-256 **1.727 s**; seed 2 baseline **1.800 s**, dynamic-16 **1.793 s**, dynamic-64 **1.783 s**, dynamic-256 **1.780 s**. Dynamic-64 was independently repeated across both seeds, consistently improving runtime without measurable RSS growth. Promoted 64-query atomic work batches in [source commit fe049356](https://github.com/insightlabs38-pixel/sd-tb/commit/fe049356d080ae1d8217d045e22730583a3f26fe), source blob `a8673d1b6c9275914471f59d886e29374e8346d6`, with five additional nonblank source lines.

[Post-promotion exactness 41108157199](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41108157199) passed the published full seed-1 hash, sample hash, independent sample seeds 2/3, seven differential seeds and pinned offline Docker as uid 65534/read-only root. [Post-promotion sanitizer qualification 41108753835](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41108753835) passed ASan+UBSan and TSan on the parallel sample, with both published sample hashes matching. All timings and RSS are runner-local, not official board measurements.

## Fused corpus-token fingerprint (promoted, October 30)

[Paired full-seed qualification and independent rerun 41109851588](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41109851588) compared the verified champion to two variants that hash query-visible tokens during the corpus scan rather than rescanning their bytes. The branchless ASCII `c | 0x20` variant won; it is exactly equivalent to ASCII lowercase for all 62 legal token-byte values. Both attempts passed seven-seed differential tests, 32-bit fingerprint collision cases, the published full seed-1 output hash, independent full seed-2 reference, and offline pinned Docker as uid 65534 on a read-only root filesystem.

Three-run means in **attempt 1**: seed 1 baseline **1.763 s** vs fused-OR **1.690 s** (4.2% faster); seed 2 **1.797 s** vs **1.787 s** (0.6% faster). Three-run means in **independent attempt 2**: seed 1 **1.400 s** vs **1.337 s** (4.5% faster); seed 2 **1.437 s** vs **1.390 s** (3.2% faster). The two CI attempts ran at different absolute speeds, so only within-attempt comparisons are meaningful. Peak RSS stayed around 766 MiB for seed 1 and 764 MiB for seed 2. The alternative fused `lower_ascii` variant was slower than fused-OR.

Promoted source commit [bb940fd216110ceda816fd7bc00a8b7ff9b0088d](https://github.com/insightlabs38-pixel/sd-tb/commit/bb940fd216110ceda816fd7bc00a8b7ff9b0088d), source blob `1492bb104bc2825411d69b48dd56709d5423adc1`. These are GitHub runner measurements, not official leaderboard scores.

## Smaller dictionary and branchless folded token equality (promoted, October 30)

[Independent three-seed qualification 41116820675](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41116820675) passed seven deterministic differential seeds, collision and phrase-automaton cases, published full seed-1 SHA-256, independent full seed-2/3 Python references, offline pinned Docker as uid 65534/read-only root, and ASan+UBSan. Replacing the query dictionary's initial 160,000-key reservation with 40,000 (same dynamic growth and exact collision resolution) and using branchless ASCII folding in token equality improved three-run mean full wall time: seed 1 **1.860 to 1.550 s** (16.7%), seed 2 **1.863 to 1.653 s** (11.3%), seed 3 **1.817 to 1.583 s** (12.8%). Peak RSS declined about 3 MiB. The combined changes were promoted in [commit 8902fb9d](https://github.com/insightlabs38-pixel/sd-tb/commit/8902fb9d07b5b671f6fafaaaf054c731c8fd1152), source blob `505fa18787f55c72c0c14dcdd403be585b855d48`. Runner-local measurements, not official board scores.

## Direct-mapped per-worker token cache (rejected, October 30)

[Two-seed comparison 41117825323](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41117825323) tested an exact 1,024-slot per-worker cache of recent dictionary lookups, including negative entries with full byte comparison, and a branchless-fold-only control. Seven-seed differential, FNV collision and suffix/phrase checks, published full seed-1 hash, independent full seed-2 reference and pinned offline Docker all passed. Three-run mean seconds, seed 1: old baseline **1.363**, fold-only **1.250**, cache-only **1.383**, combined **1.300**; seed 2: old baseline **1.407**, fold-only **1.323**, cache-only **1.407**, combined **1.353**. Cache overhead erased part or all of the equality benefit. **Reject the cache; no source promotion.** The branchless equality is already promoted with the smaller dictionary in commit `8902fb9d`; this job used the prior source as its baseline. Timings are runner-local, not official board results.
