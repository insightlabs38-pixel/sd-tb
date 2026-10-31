# Track B: Index

A C++20, standard-library-only solution to [Track B](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

Queries select a collision-safe ASCII token dictionary. Up to four workers scan disjoint document ranges with AVX2 byte classification and CRC32C fingerprints, always comparing full token bytes. Boolean terms populate reusable 1,024-document bitmaps; compact plans apply every operator strictly left to right. Population counts compute exact counts and ID sums without materializing answer lists. Phrase queries use sampled rare anchors and verify adjacency in document-local token buffers. Sampling changes work selection only. Workers merge count, sum and first-ten summaries in document order. No corpus-wide postings, positions, token tape or phrase automaton is retained.

## Validation

[Pinned production qualification](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41147462315) passed three independent full seeds, published sample/full hashes, an independent tiny reference, differential and adversarial oracles, restricted offline Docker, and ASan/UBSan. Native TSan passed the parallel adversarial fixture locally. See PERF.md for paired T/M/S/D/K evidence. Runner measurements are not official board scores.

## Dependencies

None beyond the C++ standard library and system C runtime. Build image is the pinned, organizer-allowlisted `gcc:15`; runtime image is the pinned `debian:13-slim`.

## With another 24 hours

Profile the streaming scan, compare block sizes by the full coefficient, and test faster dictionary probes and phrase-anchor layouts. The official integrator owns promotion and board measurements.
