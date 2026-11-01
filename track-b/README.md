# Track B: Index

A C++20, standard-library-only solution to [Track B](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

The query file is tokenized with the same 64-byte AVX2 window scanner as the corpus, which fingerprints every token (exact packed keys up to eight bytes, packed letters up to twelve, CRC32C beyond) while splitting it. Queries select a collision-safe four-way bucket dictionary whose single 64-bit answer carries the term ID, Boolean column and phrase head; lookup is one SIMD compare and is branch-free except for rare long-token candidates. Twelve workers claim document-aligned 2,048-document blocks from an atomic counter. Boolean terms that a short corpus sample shows to be frequent own permanent dense bitmap rows marked with one branch-free update per token; remaining terms keep up to five inline positions and spill to dynamic rows. Plans are grouped by operator shape, then every operator is applied strictly left to right on sparse/dense states, and AVX2 nibble tables give exact counts and ID sums of dense rows. Phrase queries use sampled rare anchors and verify adjacency against the same exact probes. Sampling changes work selection only. Workers keep their ten smallest IDs per query and the threads merge them by value while formatting output. No corpus-wide postings, positions, token tape or phrase automaton is retained; a complete postings pipeline was measured and is kept as a research probe (`experiments/frontier/B-CLAUDE-FRONTIER.md`).

## Validation

[Pinned production qualification](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41165413401) passes three full seeds, published sample/full hashes, an independent tiny reference, ASan/UBSan, TSan, expanded differential/adversarial oracles and restricted offline Docker. The oracle covers 338,000-term dictionary growth, all token lengths/alignment offsets all byte classes at every SIMD alignment, and deliberate short/long CRC32C collisions including repeated and reversed phrase terms. See PERF.md for full T/M/S/D/K comparisons. Runner measurements are not official board scores.

## Dependencies

None beyond the C++ standard library and system C runtime. Build image is the pinned, organizer-allowlisted `gcc:15`; runtime image is the pinned `debian:13-slim`.

## With another 24 hours

Profile the streaming scan, compare block sizes by the full coefficient, and test faster dictionary probes and phrase-anchor layouts. The official integrator owns promotion and board measurements.

This qualified hybrid uses exact 9–12-letter encodings and packed count/sum headers. Boolean sets stay sparse until a bitmap is needed. Luna owns official A/B tests and promotion; rollback sources remain preserved.
