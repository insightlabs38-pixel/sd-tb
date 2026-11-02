#!/usr/bin/env python3
"""Instrument the current Track B engine in a temporary non-submission build."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
changes = [
    ("#include <charconv>\n", "#include <charconv>\n#include <chrono>\n"),
    ("using u16 = uint16_t;\n",
     "using u16 = uint16_t;\n"
     "static u64 now_ns() {\n"
     "  return chrono::duration_cast<chrono::nanoseconds>(\n"
     "    chrono::steady_clock::now().time_since_epoch()).count();\n"
     "}\n"),
    ("struct Worker {\n",
     "struct Worker {\n"
     "  u64 sample_edges=0, sample_keys=0, sample_lookup=0;\n"
     "  u64 sample_evidence=0, sample_phrases=0, sample_docs=0;\n"
     "  u64 flush_prepare=0, flush_evaluate=0, flush_clear=0, flushes=0;\n"),
    ("  void process(Worker &w, u32 doc, u32 slot, const char *text, size_t n) {\n",
     "  void process(Worker &w, u32 doc, u32 slot, const char *text, size_t n) {\n"
     "    const bool sample = (doc & 255u) == 0;\n"
     "    const u64 pt0 = sample ? now_ns() : 0;\n"),
    ("    u64 bit = u64(1) << slot % 64, *rows = w.rows.data(), *keys = w.keys.data();\n",
     "    u64 bit = u64(1) << slot % 64, *rows = w.rows.data(), *keys = w.keys.data();\n"
     "    const u64 pt1 = sample ? now_ns() : 0;\n"),
    ("    // Keep dictionary prefetches near their use instead of prefetching an\n",
     "    const u64 pt2 = sample ? now_ns() : 0;\n"
     "    // Keep dictionary prefetches near their use instead of prefetching an\n"),
    ("    for (u32 k = 0; k < count; k++) {\n      u64 m = keys[k];\n",
     "    const u64 pt3 = sample ? now_ns() : 0;\n"
     "    for (u32 k = 0; k < count; k++) {\n      u64 m = keys[k];\n"),
    ("    for (u32 a = 0; a < anchors; a++)\n",
     "    const u64 pt4 = sample ? now_ns() : 0;\n"
     "    for (u32 a = 0; a < anchors; a++)\n"),
    ("          offer(f.query, &doc, 1);\n      }\n  }\n\n  void write",
     "          offer(f.query, &doc, 1);\n      }\n"
     "    if (sample) {\n"
     "      const u64 pt5 = now_ns();\n"
     "      w.sample_edges += pt1-pt0; w.sample_keys += pt2-pt1;\n"
     "      w.sample_lookup += pt3-pt2; w.sample_evidence += pt4-pt3;\n"
     "      w.sample_phrases += pt5-pt4; w.sample_docs++;\n"
     "    }\n"
     "  }\n\n  void write"),
    ("  void flush(Worker &w, u32 base) {\n",
     "  void flush(Worker &w, u32 base) {\n"
     "    const u64 ft0 = now_ns();\n"),
    ("    evaluate(w, base);\n    for (u32 c : w.touched)\n",
     "    const u64 ft1 = now_ns();\n"
     "    evaluate(w, base);\n"
     "    const u64 ft2 = now_ns();\n"
     "    for (u32 c : w.touched)\n"),
    ("    fill(w.rows.begin(), w.rows.end(), 0);\n  }\n\n  // Marks",
     "    fill(w.rows.begin(), w.rows.end(), 0);\n"
     "    const u64 ft3 = now_ns();\n"
     "    w.flush_prepare += ft1-ft0; w.flush_evaluate += ft2-ft1;\n"
     "    w.flush_clear += ft3-ft2; w.flushes++;\n"
     "  }\n\n  // Marks"),
    ("    Engine engine;\n", "    Engine engine;\n    const u64 t0 = now_ns();\n"),
    ("    engine.parse(queries, qsize, workers.size());\n",
     "    engine.parse(queries, qsize, workers.size());\n    const u64 t1 = now_ns();\n"),
    ("    engine.plan(docs, dsize);\n",
     "    engine.plan(docs, dsize);\n    const u64 t2 = now_ns();\n"),
    ("    engine.planner.join();\n",
     "    engine.planner.join();\n    const u64 t3 = now_ns();\n"),
    ("    engine.write(out, workers);\n",
     "    engine.write(out, workers);\n"
     "    const u64 t4 = now_ns();\n"
     "    fprintf(stderr, \"PROFILE_PHASE_MS parse=%.3f plan=%.3f scan=%.3f write=%.3f total=%.3f\\n\",\n"
     "      (t1-t0)/1e6, (t2-t1)/1e6, (t3-t2)/1e6, (t4-t3)/1e6, (t4-t0)/1e6);\n"
     "    u64 ed=0,ky=0,lk=0,ev=0,ph=0,nd=0,fp=0,fe=0,fc=0,nf=0;\n"
     "    for (const Worker &w : workers) {\n"
     "      ed+=w.sample_edges; ky+=w.sample_keys; lk+=w.sample_lookup;\n"
     "      ev+=w.sample_evidence; ph+=w.sample_phrases; nd+=w.sample_docs;\n"
     "      fp+=w.flush_prepare; fe+=w.flush_evaluate;\n"
     "      fc+=w.flush_clear; nf+=w.flushes;\n"
     "    }\n"
     "    fprintf(stderr, \"PROFILE_SAMPLED_DOC_NS count=%llu edges=%.1f keys=%.1f lookup=%.1f evidence=%.1f phrases=%.1f\\n\",\n"
     "      (unsigned long long)nd, double(ed)/std::max<u64>(1,nd),\n"
     "      double(ky)/std::max<u64>(1,nd), double(lk)/std::max<u64>(1,nd),\n"
     "      double(ev)/std::max<u64>(1,nd), double(ph)/std::max<u64>(1,nd));\n"
     "    fprintf(stderr, \"PROFILE_FLUSH_NS count=%llu prepare=%.1f evaluate=%.1f clear=%.1f\\n\",\n"
     "      (unsigned long long)nf, double(fp)/std::max<u64>(1,nf),\n"
     "      double(fe)/std::max<u64>(1,nf), double(fc)/std::max<u64>(1,nf));\n"),
]
for old, new in changes:
    occurrences = source.count(old)
    if occurrences != 1:
        raise SystemExit(f"expected exactly one marker, found {occurrences}: {old[:72]!r}")
    source = source.replace(old, new)
Path(sys.argv[2]).write_text(source)
