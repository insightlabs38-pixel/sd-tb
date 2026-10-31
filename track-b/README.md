# Track B: Index

A C++20, standard-library-only solution to [Track B](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

Queries select a collision-safe ASCII token dictionary. Up to six workers scan disjoint document ranges with AVX2 byte classification and CRC32C fingerprints with exact packed short keys and full-byte long-token verification. Document-local batches prefetch dictionary probes, whose slots pack term IDs, Boolean columns and phrase heads. Boolean terms populate 1,152-document bitmaps using lazy singleton rows and packed summaries; compact plans apply every operator strictly left to right. Mandatory-term pruning skips blocks that cannot satisfy a left-associative Boolean query. Population counts compute exact counts and ID sums without materializing answer lists. Phrase queries use sampled rare anchors and verify adjacency against the same exact lookup probes. Sampling changes work selection only. Workers merge count, sum and first-ten summaries in document order. No corpus-wide postings, positions, token tape or phrase automaton is retained.

## Validation

[Fine-size qualification](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41196378364) passed three full seeds, sample/tiny hashes, differential/adversarial cases, sanitizers and offline Docker. [Five-seed confirmation](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41199946808) passed 105 full-output comparisons with seven repeats per variant. The oracle covers 338,000-term dictionary growth, all token lengths/alignment offsets all byte classes at every SIMD alignment, and deliberate short/long CRC32C collisions including repeated and reversed phrase terms. See PERF.md for full T/M/S/D/K comparisons. Runner measurements are not official board scores.

## Dependencies

None beyond the C++ standard library and system C runtime. Build image is the pinned, organizer-allowlisted `gcc:15`; runtime image is the pinned `debian:13-slim`.

## Next optimization targets

Retain the verified 1,152-document baseline while evaluating isolated challengers against exact reference outputs and the full T/M/S/D coefficient. The official integrator owns promotion and board measurements.
