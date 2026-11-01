# Track B breakthrough campaign: research receipts, November 1, 2026

**Current pass: multiple full-engine architectures measured; none has qualified for integration.**
All tests in separate `research/b-breakthrough-20261101*` branches; `main`, scheduled developer branches, and `sd-sm` unchanged.

## Official baseline
At 2026-11-01 08:24 UTC the live board records HighVelocity B BEST:
K=26.690516, T=0.159821 s, memory peak 58,052,608 B, S=1111, D=1,
run `run-1d48fc96e1ee`, integration SHA `033819b04f41e7a02d02ab2b1e86241db716fc20`.
Competitive public board evidence: Cause Of Death K=37.235722, T=0.100386 s,
M=42,561,536 B, S=823, D=1, run `run-3eb48513bdc1`.
No competitor source has been identified for that latter run. K uses T^-0.50,
M^-0.20, S^-0.15, D^-0.15. To reach K35 at fixed other axes requires T<=0.0929 s.

## Prior-cost audit
`research/b-index-architecture-probes-20261031/research/BD-20261031-B-adaptive-postings.md`
measured only synthetic pre-tokenized Boolean processing, and expressly did not cover
dictionary, phrase, scan, construction, or RSS. The separate complete
`experiments/frontier/B-CLAUDE-FRONTIER.md` posting prototype found
941 ms scan+phrases+event logging, 239 ms CSR, 187 ms bitmap construction,
176 ms Boolean evaluation, 1,545 ms total vs a later streaming prototype 740 ms on the same pinned core.
Only 226 repeated two-term query prefixes among 29,709 multiple-term queries.

## Full solver family A: per-term uncompressed postings
Branch `research/b-breakthrough-20261101-postings`.
Executable `experiments/breakthrough/query_postings.cpp`, commit
`d0643db26905f515e89e266df60f521e282f9113`.
CI head `cae73beb220a732b768300e45bdca509d84680b7`.
Qualified CI: https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41289073444 .
Full seeds 1/2/3, medians: candidate 2.52/2.73/2.40 seconds vs champion
0.26/0.28/0.26 seconds. Seed 1 phase median approximately
parse 35ms, scan 2,098ms, merge 74ms, evaluate 272ms, output 11ms.
Process RSS 463–467 MB KiB-scale /usr/bin/time output, **not cgroup memory**.
All exact full references, differential, adversarial, ASan/UBSan, and read-only offline Docker passed.
TSan on local 80k-doc/18.3 MB dataset completed without reported race;
full 12-thread TSan process also reproduced correct 64-bit sums.
Verdict: **reject as end-to-end replacement**; build dominates and adds large memory.

## Family B: exact hot-key lexicon
Branch `research/b-breakthrough-20261101-hot-lexicon`.
Complete copied-and-modified champion in `experiments/breakthrough/hot_lexicon.cpp`,
commit `deb7f5d03d78ab0d5ba98d023921cd654e11237b`.
Qualification https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41289362826
completed SUCCESS, exact three full seeds, 5 paired repetitions, differential,
adversarial, sanitizer, Docker gates. Seed medians: champion 0.32/0.32/0.32s;
hot lexicon 0.37/0.37/0.36s, RSS essentially unchanged.
Verdict: **reject**; extra cache lookup lost to current exact-bucket dictionary.

## Family C: sparse mandatory-anchor late materialization
Branch `research/b-breakthrough-20261101-late-materialize`.
Complete compressed candidate is not yet included; current standalone engine
`experiments/breakthrough/late_materialize.cpp` commit
`59482db5b0b7637816cafef38555bf4668935b03`;
baseline pinned on branch at `8e7f79034e64d6314f1dc1c31f5093d7ce609a54`.
Qualification https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41289477233
completed SUCCESS, exact full seeds 1–3, five paired repeats, sanitizer, Docker.
Seed medians base 2.73/2.66/2.54 seconds, late 2.68/2.61/2.48 seconds;
Boolean phase 315–325ms reduced to ~260–269ms.
The full gain is ~2% because the scan costs ~2 seconds.
Verdict: **query-stage mechanism helps**, but this whole solver loses by ~10x.

## Family D: exact-key hardened compressed-stream hybrid
Branch `research/b-breakthrough-20261101-compressed-exact`.
Source `experiments/breakthrough/compressed_exact.rs` is an explicitly
MIT-licensed research derivative of the public `manusingh090/speed-demon-2026-entry`
Rust engine. Attribution/license preserved in `experiments/breakthrough/COMPRESSED-LICENSE`.
Changes include full 32-byte SIMD exact key verification on every fingerprint match
(no probabilistic hash-only term equality), actual CLI input and output paths,
20-bit term IDs and a collision-safe oversized-vocabulary fallback.
The public implementation is mechanism evidence, **not verified as a K35–38 winner**.
CI at https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290098418
is underway at this writing (exact adversarial passed, full-seed testing in progress);
do NOT infer performance or promote until measurements and pinned Docker pass.

## Handoff gate
All K numbers from GitHub-hosted runners are non-scoring; promoter must use
organizer scoring cgroup, static axes after canonical formatting and actual board receipts.
Never replace active code on raw synthetic or runner-only evidence.
The next architectural target is a **direct in-place compressed scan that avoids
uncompressed event logs, coupled with exact token-key verification**.
Other research avenue: query-aware rare anchor evaluation integrated into the compact
representation, not the 2-second unordered-map postings prototype.

**No integration performed. This is a dated, interim evidence ledger.**

## Follow-up exact full-engine experiments (November 1)
All repeat timings below are matched seed/runner medians, NOT official scoring host results.

### Exact-key compressed stream, full qualification
Branch `research/b-breakthrough-20261101-compressed-exact`, head
`1bbc318f5e33efb3f230f419fa9311e3850bfa8e`.
[CI 41290098418](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290098418)
passed exact differential/adversarial (including 338k distinct tokens), full seeds
1/2/3, 5 paired repetitions and pinned offline read-only Docker.
Candidate median T=0.27/0.26/0.26 s; contemporaneous C++ champion=0.22/0.20/0.20 s.
Compared sample host, process RSS ~300MB KiB-scale /usr/bin/time KB vs ~272MB KiB-scale.
Loss. Public upstream MIT code hash-only equality was **not** accepted: on every
candidate fingerprint match this research version verifies full exact key bytes.

### Exact compact-key compressed stream
Branch `research/b-breakthrough-20261101-compact-exact`, source commit
`ca1375782d714325604be92e40422198c09efef6`,
[CI 41290613100](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290613100)
passed all three full seeds, five paired repeats, differential/adversarial and Docker.
Candidate scalar packed-key T=0.29/0.27/0.27 s; compressed predecessor
0.26/0.25/0.25 s; C++ champion 0.20/0.19/0.19 s. Loss.

### Exact BMI2/SIMD packing repair
Branch `research/b-breakthrough-20261101-compact-bmi2`, source commit
`30fabf2acea2984d953ab496818d811bd76278f1`, CI head
`fd64e677304c90cf6c030020e84bf81f0a080025`.
[CI 41291072193](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41291072193)
passed all differential/adversarial and three-full-seed five-repeat plus Docker
checks. Candidate T=0.41/0.42/0.41 s, prior scalar=0.44/0.44/0.44 s,
C++ champ=0.32/0.32/0.32 s. Repair gains 5-7% against scalar on that
runner, but not enough to beat production (28-31% slower).

### Bloom-gated exact dictionary prefilter
Branch `research/b-breakthrough-20261101-bloom-filter`,
source `61e57320a3cba00300a5101495b39c55b4ff55b6`,
CI head `2b0c95e8542ef29f3fdc47148d3e22214813b18c`.
[CI 41290748429](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41290748429)
passed full exact seeds 1/2/3 five times, differential/adversarial,
ASan+UBSan and pinned offline Docker. Bloom candidate median 0.41/0.41/0.41 s
vs champion 0.32/0.32/0.32 s, no material memory reduction. Loss.

### Pinned cgroup K qualification in progress
[Run 41291188697](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41291188697)
on `research/b-breakthrough-20261101-report` at
`a36cde782ad488d49b8ceb869fe1469c126f892c` uses organizer pinned
sd-timer, cgroup memory.peak, full exact independent seeds, canonical
formatting, scc source count and D=1. Four independent Docker images:
champion, Bloom gated, compressed exact, BMI2 exact. Results are
runner-local and not submission scores.

## Proven decision and continuation
**Do not integrate any candidate from these rounds:** no consistent full
end-to-end gain was found against main champion.
The difference between exact small-key fusion and hash-only shortcut is critical:
the latter fails the exact-identity requirement even if its probability of
collision is low. The scan-stage bottleneck remains the highest-value research
area, especially replacing redundant dictionary probes, avoiding per-token
side effects and scheduling document work to eliminate memory stalls without
additional lookups.
The temporary `research/b-breakthrough-20261101-query-dag` branch is a reservation
only, not implemented or benchmarked: low prefix reuse argued against this path.
No developer or production branch was changed by this work.
