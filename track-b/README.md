# Track B: Index

A C++20, standard-library-only solution to [Track B](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

Queries are parsed before the corpus. A case-insensitive ASCII token dictionary selects only query-visible terms, with byte-exact comparison and no Unicode normalization. A single corpus scan builds ascending per-term document postings and a compact document-delimited tape of selected term IDs. Boolean expressions use sorted-set intersection, union and difference in strict left-to-right order. Phrase queries normally choose their least-frequent term as a candidate filter and verify exact consecutive token IDs in each candidate document. High-frequency phrases are deduplicated and evaluated by a shared token-level Aho-Corasick automaton over the compact tape, avoiding repeated scans. Output is deterministic and includes the exact 64-bit sum and first ten IDs.

## Dependencies

None beyond the C++ standard library and system C runtime. Build image is the pinned, organizer-allowlisted `gcc:15`; runtime image is the pinned `debian:13-slim`.

## With another 24 hours

Profile the organizer full corpus, compare selective positional indexing and query-batch evaluation against the measured hybrid baseline, and explore SIMD tokenization. The phrase automaton is already implemented. Replace the placeholder `team` value before official submission.
