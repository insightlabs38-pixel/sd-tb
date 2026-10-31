# Qualified compact hybrid campaign

[Qualification 41165413401](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41165413401) passed pinned offline GCC 15 production-Docker differential/adversarial oracles, ASan/UBSan, TSan, published full/sample hashes, independent full seed-2/3 references and tiny seed 7. Expanded tests cover 338,000 mixed short/CRC/encoded-letter terms, all byte classes and alignments, long CRC collisions, 9–13-letter prefixes/digits/case, inline cardinalities and union overflow, repeated terms, and worker/block/first-ten boundaries. Six million independent checks compare the actual twelve-worker packed membership helper with a linear oracle.

The runner exposes four logical CPUs. Timings use five rotated paired runs per image/seed, dropping the minimum and maximum and averaging the middle three, rounded to six decimals. The organizer spread gate and up to two complete paired-batch retries apply. M is the maximum cgroup peak across the selected five runs. S uses clang-format 23.1.3/scc 4.1.0; D=1. All raw batches, source SHAs and machine metadata are retained in the workflow artifact. `hybrid-results.json` records every per-seed axis.

| Source | Workers / block | T, seeds 1 / 2 / 3 (s) | Maximum M across seeds (B) | S / D | K_local, seeds 1 / 2 / 3 |
|---|---|---|---|---|---|
| official | 4 / 2048 | 0.374096 / 0.375650 / 0.376462 | 43,388,928 | 742 / 1 | 19.590892 / 19.550328 / 19.529232 |
| old6 | 6 / 1536 | 0.403526 / 0.395844 / 0.390103 | 42,078,208 | 781 / 1 | 18.718580 / 18.899340 / 19.037899 |
| old12 | 12 / 768 | 0.393637 / 0.397174 / 0.397212 | 46,534,656 | 781 / 1 | 18.782921 / 18.721482 / 18.656544 |
| hybrid6 | 6 / 2048 | 0.334020 / 0.331050 / 0.335371 | 37,122,048 | 857 / 1 | 20.289576 / 20.380386 / 20.248668 |
| hybrid12 | 12 / 2048 | 0.324628 / 0.325807 / 0.326278 | 42,856,448 | 925 / 1 | 20.346611 / 20.309763 / 20.295099 |

This runner is faster than the earlier worker-qualification VM. Compare paired sources within this run; K_local is not an official board score. The `official` source is PR #3's exact production source, also present in official `8d5bc54`. New six-worker K improves 3.6–4.2% over that source and 6.4–8.4% over the preceding six-worker candidate. New twelve-worker K improves 3.9% over that source and 8.3–8.8% over the preceding twelve-worker candidate. Oversubscribed timings do not settle six-P-core/twelve-thread scaling.

## Architecture

Both candidates encode 9–12-letter tokens exactly in 60 bits using BMI2 extraction, retaining collision-checked CRC fingerprints for other long tokens. Distinct tags preserve exact short keys, letter encodings, and CRC/term-ID phrase keys. Ten-byte result headers combine count and sum additively under the frozen sum < 2^35 bound, with 16-bit offsets for at most 50,000 first-ten blocks.

Boolean evaluation preserves left associativity while retaining empty, sparse and borrowed bitmap results until an operation requires materialization. Six workers store singleton block rows inline. Twelve workers store up to five sorted 12-bit document positions inline, using exact packed-lane membership before allocating a bitmap. Both use 2,048-document blocks, bounded capacity, document-aligned static chunks and ordered summary merging. Full inputs select literal worker counts, without environment overrides.

## Controlled official handoff

- Six-worker source `5bc63780944240c8a9fb302b0cc5f2d91f98be16`: [PR #7](https://github.com/insightlabs38-pixel/sd-tb/pull/7); rollback `a5e106a` / PR #5.
- Twelve-worker source `ea351a5c9a3eb985faab60326e61387b6366b8b6`: [PR #8](https://github.com/insightlabs38-pixel/sd-tb/pull/8); rollback `b9ab12a` / PR #6.
- Preserve official `8d5bc54` (PR #3 source `338c7cf`) and PR #2 source `4069aa9`.

Luna owns all official writes. A/B each exact source against the preserved official source with other tracks and Dockerfile unchanged, retain receipts and repeat close results before selecting a champion. The latest verified official BEST remains K=16.912119 at T=0.488654 s, M=47,063,040 B, S=742, D=1; the figures above are runner measurements.
