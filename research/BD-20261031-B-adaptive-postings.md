# B: adaptive query-aware postings, independent research

**Research only; unqualified.** Branch forked from main `d7845bc6ac6a77547121daf1fda751e34999b325`, B champion source blob `76946cc77205b4f115aa32bed459e0980c8d143c`. No production changes. Scheduled branches and main untouched.

## Finding

A synthetic, pre-tokenized 100,000-document / 12,000-Boolean-query model produced a materially faster isolated Boolean execution path: query-specific postings constructed in 83–89 ms, then exact Boolean processing in 9–10 ms using four query workers and popcount-based weighted document-ID sums; same-distribution compact block-bitmap baseline ~243 ms. Postings buffer capacity ~23.2MB, **not peak RSS**. Oracle mismatches: zero. This excludes tokenization, dictionary lookups, phrase verification and output; the synthetic generator is not the frozen organizer distribution. Treat the full-K gain as **unproven**, not a submit-ready result.

An event-driven sparse Boolean callback design failed in the same research: fused callback + bitmap ~18ms vs compact bitmap ~14ms at 20k docs/3k queries. Block gating skipped 74% of eligible sparse query/block visits but took ~20–21ms; poor dispatch locality outweighed savings. A negative implementation result is not a proof against the query-aware posting family.

## Design for qualification

- Parse known queries into a query-visible Boolean term dictionary and existing exact phrase-anchor matcher. Resolve query-visible terms once per corpus token; retain **no corpus-wide token tape**.
- Build adaptive per-term postings using U16 offsets in ≤65,536-document chunks, with bitmap containers for high-density chunks. Deduplicate repeated term occurrences in one document. Avoid heap fragmentation and per-worker replication of the entire term universe.
- Apply Boolean operators strictly left-to-right using sparse merge / membership / set subtraction or dense bitmap operations. Do not reorder OR/NOT. Dense result count = popcount; exact sum for 64-bit word x at base b is b·popcount(x)+Σ(2^k·popcount(x&bitplane[k])); take only the earliest ten set bits.
- Run immutable posting queries in parallel; retain the current champion's phrase-anchor verification in the same input scan. Compare a full query-only posting index against **rare-posting/dense-stream fallback**.
- Instrument tokenizer/dictionary probes, build time, index bytes, query time, phrase time, result merge, and peak cgroup memory at phase boundaries. Compare one local medium corpus and an isolated organizer sample, not an official multi-seed CI matrix.
- Enforce exactness for repeated words, ASCII byte splitting, CRC collisions, leading zeros, zero matches, sorted first-ten, and mixed left-associative NOT/OR.

## Score economics

Official B BEST at inspection: `run-dcebadac870d` K=23.250490, T=236.395ms, M=49.90MB, S=925, D=1. A redesigned S=1200/M=60MB needs full-process T below **approximately 203ms** to break even; at S=1250/M=80MB it needs **approximately 179ms**. K ratio is computed from all four axes; the synthetic kernel times alone do not establish a win.

Prior hybrid index with a token tape held ~150MB and was slow; that does **not** evaluate the proposed query-only compressed posting architecture. Earlier callback, direct caches, AND-only reordering, and block-size sweeps are recorded in `track-b/PERF.md`. Do not repeat them without a changed cost mechanism.

**Handoff:** Implement on a new independent branch only; keep main and official integration unchanged until the regular developer qualifies and integrator scores a candidate. The full report and C++ reproducible probes are available in the accompanying research artifact produced in chat.
