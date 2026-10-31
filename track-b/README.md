# Track B: Index

A C++20, standard-library-only solution to [Track B](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

Queries are parsed before the corpus. A case-insensitive ASCII token dictionary selects only query-visible terms, with byte-exact comparison and no Unicode normalization. A four-worker corpus scan builds ascending per-term document postings and a compact, per-worker segmented phrase-token tape with 16-bit IDs and an exact 32-bit high-vocabulary fallback. Retaining worker-owned segments avoids the global tape copy. Four query workers preserve output order. Boolean expressions use sorted-set intersection, union and difference in strict left-to-right order, with rare-term candidate probes and adaptive intersections for unbalanced lists. Phrase queries choose their least-frequent term as a candidate filter and verify exact adjacency around that anchor. High-frequency phrases are deduplicated and evaluated by a shared token-level Aho-Corasick automaton with parallel document-range scans. Output is deterministic and includes the exact 64-bit sum and first ten IDs.

## Validation

The [segmented-tape qualification](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41149380822) passed seven-seed differential tests, ASan/UBSan, exact full seed-1 hash, independent full seed-2 reference, and pinned offline Docker under uid 65534 on a read-only root. It reduced full-run peak RSS by about 69–71 MiB without a consistent wall-time regression. The [independent ownership comparison](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41137002236) also passed the 80,000-phrase-term fallback. A direct post-promotion exactness run is tracked in [CI 41154133283](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41154133283). Runner timings are not official board measurements.

## Dependencies

None beyond the C++ standard library and system C runtime. Build image is the pinned, organizer-allowlisted `gcc:15`; runtime image is the pinned `debian:13-slim`.

## With another 24 hours

Profile the organizer full corpus, compare selective positional indexing and query-batch evaluation against the measured hybrid baseline, and explore SIMD tokenization. The phrase automaton is already implemented. Replace the placeholder `team` value before official submission.
