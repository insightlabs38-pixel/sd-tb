# Paused dictionary experiment

This branch is unfinished and has no PR. Do not integrate or submit it as a champion.
It groups four exact keys and their metadata into one 64-byte bucket, with SIMD matching and one-based internal IDs.
Both six- and twelve-worker prototypes match full seed-1/2 hashes; sanitizers and pinned qualification have not run for this dictionary.
Warmed paired native measurements show small/mixed gains, not the earlier apparent cold-input gain. No winner is established.
The next commit preserves the twelve-worker source; this commit preserves the six-worker source.
Qualified rollback sources are hybrid6 `5bc63780944240c8a9fb302b0cc5f2d91f98be16` and hybrid12 `ea351a5c9a3eb985faab60326e61387b6366b8b6` (PRs #7/#8).
Next: inspect bucket lookup inlining, test two-slot versus four-slot groups, then qualify only credible full-K improvements.
A separate next architecture is a singleton / short-array / bitmap block hybrid; it has not been implemented.
