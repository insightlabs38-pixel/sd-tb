# Track B frontier: adaptive containers, window tokenizer, bucket dictionary (Claude, Oct 31)

Base: `d7845bc` (production blob `76946cc`, official run `run-dcebadac870d`, K 23.250490).
Branch: `branch/festive-hawking-wn31k1`. Rollback: `d7845bc`. Nothing here touches `sd-sm`.

## Verdict on the postings family

A complete exact pipeline was built and measured (`postings_probe.cpp`: same tokenizer, dictionary and
phrase matcher; deduplicated event log, CSR postings, 24 MB of dense bitmaps for 754 terms, sparse
sorted lists elsewhere, exact left-to-right evaluation of all 31,000 Boolean queries). Seed-1 output
hash matches the published SHA-256. Single thread, full corpus, pinned core:

| Stage | ms |
|---|---|
| scan + tokenize + resolve + event log + phrase | 941 |
| counting-sort CSR (21.09 M pairs) | 239 |
| dense bitmap build | 187 |
| exact query evaluation (all Boolean) | 176 |
| **total** | **~1,545** |

For comparison the champion takes ~1,420 ms and the candidate below ~795 ms on the same pinned core.
Resident cost is ~170 MB event log + 84 MB postings + 24 MB bitmaps. Evaluation is fast (5.7 us/query,
embarrassingly parallel) but it is not where time goes: a dedupe-and-append log alone costs +120-200 ms
over resolve-only (branchless; +300 ms with a branch), and the counting sort adds 70-240 ms, against 330 ms
for the champion's whole branchy bitmap fill. Even zero-cost evaluation could not pay for >=+60 MB:
memory has exponent 0.2, so +60 MB costs ~11% K while the entire eval stage is ~20-26% of CPU (+10-14% K).
Posting construction is the hard floor, and the block-streaming design already pays less than that floor.

## What the candidate is

The selective hybrid that did win is block-granular rather than corpus-granular:

- Hot Boolean terms (>=4 sample hits, <=3000) own permanent per-worker dense bitmap rows and are marked
  with one branch-free read-modify-write per token. Cold terms keep five inline 12-bit positions and spill
  to dynamic rows. Eval uses the same sparse/dense operators as before.
- 64-byte window tokenizer fused with 8-byte-key fingerprinting; one SIMD compare against a four-way
  bucket dictionary that returns term ID, Boolean column and phrase head in one 64-bit word.
- Branch-free dictionary resolve; the rare cases (long-token candidates, full-bucket misses) are the only loop.
- AVX2 exact count and weighted position sum for dense result rows (nibble LUTs, SAD, maddubs).
- Dynamic claiming of document-aligned blocks (interpolated seek on dense IDs); first-ten IDs merged by value.
- Query file tokenized with the same SIMD tokenizer; output merged and formatted on all threads.
- 18-bit packed first-ten lists; 512 KB frequency sample (ties prefer longer words).

TBD: CI numbers, pinned-compiler results, S/M/D, K_local.
