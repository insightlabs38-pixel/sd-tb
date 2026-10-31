# Qualified fixed-worker campaign

[Qualification 41159913998](https://github.com/insightlabs38-pixel/sd-tb/actions/runs/41159913998) passed every candidate. The runner exposes four logical CPUs with affinity 0–3. Six-, eight- and twelve-worker timings are oversubscribed and cannot decide official scaling on six physical P-cores/twelve hardware threads.

Full inputs select the literal worker count in the source; inputs below 8 MiB use one worker. No environment override, command option or private build flag is needed. `worker-sources.json` pins every exact source SHA. `worker-results.json` records all per-seed axes and full coefficients; the workflow artifact retains raw rotated measurements and machine metadata.

Every candidate passed production GCC 15 offline pinned Docker differential/adversarial oracles as uid 65534, no network, read-only root and dropped capabilities; ASan/UBSan and TSan; published full/sample hashes; independent full seed-2/3 Python references; tiny seed 7; 338,000-term growth, long CRC collisions, UTF-8/all-byte tokenization, and 4/6/8/12-worker/block/first-ten boundaries. Three rotated restricted-container runs produce each median T and maximum cgroup M. Canonical clang-format 23.1.3/scc 4.1.0 counts S; D=1 throughout.

| Variant | Workers / block | T by full seed 1 / 2 / 3 (s) | Max M across seeds (bytes) | S / D | K_local by full seed 1 / 2 / 3 |
|---|---|---|---|---|---|
| official | 4 / 2048 | 0.703404 / 0.679635 / 0.661388 | 60,121,088 | 672 / 1 | 13.638751 / 13.884918 / 14.049355 |
| control4 | 4 / 2048 | 0.572712 / 0.568277 / 0.568930 | 43,155,456 | 739 / 1 | 15.843150 / 15.904851 / 15.895718 |
| control6 | 6 / 2048 | 0.618306 / 0.631766 / 0.623991 | 60,674,048 | 739 / 1 | 14.303721 / 14.156468 / 14.233408 |
| control8 | 8 / 2048 | 0.640777 / 0.634405 / 0.635681 | 78,012,416 | 739 / 1 | 13.357114 / 13.432775 / 13.410685 |
| control12 | 12 / 2048 | 0.669203 / 0.672549 / 0.672684 | 112,877,568 | 739 / 1 | 12.139779 / 12.114916 / 12.107976 |
| workers4 | 4 / 2048 | 0.592451 / 0.609621 / 0.581445 | 38,416,384 | 781 / 1 | 15.448362 / 15.229259 / 15.593882 |
| workers6 | 6 / 1536 | 0.610445 / 0.620926 / 0.606104 | 41,934,848 | 781 / 1 | 15.218973 / 15.089979 / 15.273377 |
| workers8 | 8 / 1024 | 0.579351 / 0.579205 / 0.583443 | 39,976,960 | 781 / 1 | 15.622046 / 15.624015 / 15.567166 |
| workers12 | 12 / 768 | 0.625277 / 0.625805 / 0.612502 | 46,559,232 | 781 / 1 | 14.881114 / 14.861988 / 15.058191 |
| workers12-singleton | 12 / 768 | 0.634590 / 0.623059 / 0.615148 | 44,740,608 | 794 / 1 | 14.841402 / 14.976742 / 15.091849 |

The `official` label is the historical v2 source `4069aa9`, not the newer live submission. Count-only controls preserve PR #3's architecture and change only its literal worker count. Their S=739 versus PR #3's S=742 reflects removal of the hardware-concurrency expression. Pooled candidates retain compact plans and exact phrase probes, reuse anchor storage, and allocate first-ten blocks only for matching queries; block sizes trade query passes against per-worker bitmap memory. The twelve-worker singleton alternative stores the sole ID in the sum until a second match.

Pure count increases raise peak memory from 43 MB (four workers) to 61/78/113 MB (six/eight/twelve). Pooled layouts reduce those to 42/40/47 MB. Singleton summaries reduce twelve-worker peak further to 45 MB, with mixed K versus the simpler twelve-worker source. These observations motivate controlled scoring-box tests; they do not promote an oversubscribed winner as champion.

## Official A/B handoff

- Six-worker candidate: `a5e106a35ba4339b0b9d95f49194521fa05f1581`, [PR #5](https://github.com/insightlabs38-pixel/sd-tb/pull/5).
- Twelve-worker candidate: `b9ab12ad1de87e476b8bf1a76cfb8b242e0813f1`, [PR #6](https://github.com/insightlabs38-pixel/sd-tb/pull/6).
- Qualified lower-memory twelve-worker alternative: `15286bb8ee8919c151bab595a79dbba25f3b2d99`, branch `frontier/b-sol-workers12-singleton`.
- Four-/eight-worker pooled controls and count-only SHAs are pinned in the source manifest.
- Preserve official `8d5bc54c93a054ba70e651a7d9b802fcf9140276` (PR #3 source `338c7cf8e1585ae770e2faa12834fa50b8138bd2`) and PR #2 `4069aa907506fb5c0cc469421eeb74f93026e367`.

Luna alone owns submission. Keep other tracks and the Dockerfile identical while testing 4 → 6 → rollback → 12 → rollback, using the exact candidate source commit. Record official receipts and paired repeats before choosing a champion. Official v3 at `8d5bc54` reports T=0.488654 s, M=47,063,040 B, S=742, D=1, K=16.912119; runner K values above are not board scores.
