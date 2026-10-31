# Track B frontier: adaptive block containers, window tokenizer, bucket dictionary (Claude, Oct 31)

Base `d7845bc` (production blob `76946cc`, official `run-dcebadac870d`, K 23.250490). Branch
`branch/festive-hawking-wn31k1`. Rollback: `d7845bc`. `sd-sm` is untouched. All timings below are
development-host or GitHub-runner measurements, never official scores.

## 1. Verdict on the query-aware postings family

I built a **complete exact postings pipeline** (`postings_probe.cpp`, single thread): the candidate's
tokenizer, dictionary and phrase matcher, then a deduplicated (column, document) event log, a counting-sort
CSR index, 754 dense bitmaps (24 MB) for terms with df >= N/64 and sorted lists elsewhere, and exact
left-to-right evaluation of all 31,000 Boolean queries (list/list merge, list/bitmap filter, bitmap ops,
popcount sums, first ten). Seed-1 output matches the published SHA-256 `f86973c2...`.

| Stage (full corpus, one pinned core) | ms |
|---|---|
| scan + tokenize + resolve + event log + phrase | 941 |
| CSR build (21.09 M deduplicated pairs) | 239 |
| dense bitmap build | 187 |
| exact evaluation of every Boolean query | 176 |
| **total** | **~1,545** |

For reference the champion needs ~1,420 ms and the final candidate ~740 ms on the same core.
The postings index holds 21.09 M pairs (84 MB as u32) plus 170 MB of transient event arrays in the probe.

Why it cannot win, from measured components (seed 1, 53,444 Boolean terms; 714 terms with df > 4,096 carry
57% of pairs):

* Evaluation is genuinely cheap and trivially parallel (5.7 us/query, 176 ms single thread), and my own
  block evaluator spends 0.17-0.24 s on the same work. That is the whole prize: at most ~20-25% of CPU.
* Construction is the floor, not evaluation. A *branchless* dedupe-and-append log costs +120-200 ms over
  resolve alone (+300 ms with the data-dependent branch), a counting sort adds 70-240 ms, bitmap building
  another ~190 ms. The champion's entire branchy bitmap fill is 330 ms, and the final hot/cold fill ~230 ms.
* Memory has weight 0.2: +60 MB on a ~50 MB process costs ~10% K, while perfect, free evaluation is worth
  <= +12% K (T^-0.5). Net: no win even at zero evaluation cost.
* Query overlap is nil for factored DAGs: 29,483 distinct two-term prefixes among 29,709 queries with two
  or more terms; 22,836 distinct first terms among 31,000; the 100 most-used terms are 4.1% of term uses.

The *selective hybrid* that does win is block-granular instead of corpus-granular (section 2): permanent dense
rows for the terms that would be bitmap postings anyway, tiny inline sorted containers for the rest, no
persisted index.

## 2. What the candidate is

* **Query file and corpus share one tokenizer.** A 64-byte window classifier yields start/end transitions;
  tokens are fingerprinted as they are found (1-8 byte tokens: exact key plus CRC32C; 9-12 letters: packed
  5-bit key; longer: CRC chain with full-byte verification on candidates).
* **One four-way bucket dictionary** (64-byte buckets, 8-byte keys, 64-bit metadata: term ID, Boolean column,
  phrase head). Lookup is one SIMD compare, branch-free except long-token candidates and full-bucket misses.
  Built directly by interning; nothing is rebuilt or copied afterwards.
* **Hot rows.** Boolean terms with >= 4 hits in a 512 KB corpus sample (at most 3,000) occupy the first
  columns and permanent per-worker 2,048-document bitmap rows, marked with one unconditional OR per token
  (non-hot tokens write a sink row). Remaining terms keep the five-position inline state and spill into dynamic
  rows, exactly as before. Sampling only chooses work; it never decides whether anything matches.
* **Block scheduling.** Workers claim document-aligned blocks from an atomic counter; block starts come from
  an interpolation seek on the dense document IDs (malformed IDs still fail with the champion's message).
  Per-query first-ten IDs are kept by every worker and merged by value.
* **Dense result rows** use an AVX2 nibble-table kernel for exact count and weighted position sum.
* Plans are grouped by operator shape with a counting sort so evaluator branches repeat; output merge and
  formatting run on all threads; first-ten lists use 18-bit packing.

## 3. Measurements

Development host (4 cores, GCC 13, `-O3 -flto -march=x86-64-v3`), seed 1, pinned core, min of 9-11 runs:

| Step | ms |
|---|---|
| champion `d7845bc` | 1,422-1,507 |
| batched tokenizer + pipelined probes | 1,431 |
| bucket dictionary | 1,376 |
| 64-byte window tokenizer | 1,290 |
| dense count/sum kernel | 1,169 |
| single dictionary, shorter sample, parallel output | 1,037 |
| **hot rows** | **802** |
| branch-free resolve | 775 |
| dynamic blocks, fused tokenizer, cold prefetch, shape grouping | ~740 |

Four real cores (`W=4`): champion 392-397 ms, candidate 215-231 ms. Twelve threads on four cores: 425 -> 229 ms.
Peak anonymous memory 38.5 -> 42.1 MB (12 workers). Canonical S 918 -> 1,103 (plus 7 Dockerfile lines), D 1.

CI (pinned `gcc:15` Docker, offline, organizer timer, 4-vCPU SMT runner; five paired runs per seed):
K ratio 1.156 / 1.152 / 1.157 with both sides at 12 workers (T 0.476 -> 0.335 s), and 1.220 / 1.231 / 1.228 with the
final worker-count change (candidate uses the runner's 4 CPUs: T 0.267 s, M 23 MB). Receipts and links: `PERF.md`.
The runner's T gain (0.70x) is smaller than the development host's (0.55-0.6x): SMT siblings share the same
caches, so expect the scoring host to land between the two.

## 4. Rejected or neutral (with reasons)

| Experiment | Result |
|---|---|
| Dense-everywhere branch-free evaluator | exact, +22% slower (970 vs 796 ms): scratch expansion costs more than the mispredicts it removes |
| Dense path for OR queries only | +6% slower |
| Word-major hot rows (L1-resident marking) | exact; marking not L1-bound locally (+2.7% from transposes); kept as a CI sweep variant for small-L2 SMT hosts |
| Eight-group table flatten, branchless long fingerprint, prefix prefetch of active entries for hot columns | no gain or slower (the last: +25%) |
| Query-ordered prefetch of active states, sort by first term | <= 2% |
| Transparent huge pages for the dictionary, `-static`, PGO (GCC 13 sample profile), `-march=native`, `-O2` | no gain |
| Extracting the mark loop into a separate function (restrict/templated) | 9-12% slower from changed inlining |
| Block sizes 1,024-4,096 | 1,024 saves 8 MB but costs ~8% time; 1,536 is within noise on CI; 4,096 the reverse |
| Hot cap 1,500/2,000, hot threshold 8, 256 KB sample (CI sweep) | all within +-2% of base |
| Parallel query-file tokenization, prefaulting dictionary/plans, extra intern prefetch | no change in the 10 ms serial parse (instruction and first-touch bound) |
| 24-byte `Probe` | -1%, within noise; not adopted |

## 5. Next actions

1. Luna: official A/B of exact source (see handoff in `PERF.md`).
2. Parallel query parse (tokenize slices on helper threads, intern serially): ~3 ms of ~120 ms projected.
3. Smaller `Probe` (16 bytes) to cut store traffic in the tokenizer: est. 2-3%.
4. Re-tune `hot_cap`, block size and worker count on the actual 6-core/12-thread host.
5. Shrink dynamic rows with u16 sorted containers (memory only).
