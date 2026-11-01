# Track B: Index

A C++20, standard-library-only solution to [Track B](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

Both files are memory-mapped with trailing zero pages, so every SIMD load may run past the end. Threads parse line-aligned parts of `queries.txt` and intern the terms concurrently (compare-and-swap on four-way buckets; each part numbers its new terms from its own ID range). Tokens of up to eight bytes are their own 64-bit key, 9-12 letters pack into 5-bit codes, and longer or digit-bearing tokens use a CRC fingerprint that is checked byte for byte.

A 512 KiB corpus prefix ranks terms. The 3,000 most frequent Boolean terms get permanent 2,048-bit rows per block; other terms keep up to five 12-bit positions and spill to rows. Strict left-to-right semantics bound each answer by S(t_j) plus the OR terms after j, for the first term and every AND term, so each query is guarded by its rarest such set and evaluated only in blocks where a guard term occurs. Phrases are matched at their rarest term by comparing the document's term-ID tape. The prefix only changes the work done, never the answers.

Workers claim line-aligned byte ranges that shrink towards the end, tokenize each document with an unrolled edge list, prefetch dictionary buckets eight tokens ahead and flush blocks of up to 2,048 documents. Counts and sums are per-worker per plan; the ten smallest IDs live in one shared, spin-locked list per query. A fixed mmap threshold and early release of scan state keep twelve workers near 36 MiB.

## Validation

`.github/workflows/frontier-b-architecture-compact.yml` runs the differential, adversarial and fuzz oracles (plain, ASan/UBSan, TSan), three full organizer seeds against published or independent references, sample and tiny sets, and the pinned offline Docker image as uid 65534 on a read-only root. See PERF.md for receipts. Runner measurements are not official board scores.

## Dependencies

None beyond the C++ standard library and system C runtime. Build image is the pinned `gcc:15`; runtime image is the pinned `debian:13-slim`.
