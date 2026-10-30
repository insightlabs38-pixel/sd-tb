#!/usr/bin/env python3
"""Insert non-scoring phase timers into a temporary Track B build."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
changes = [
    ("#include <charconv>\n", "#include <charconv>\n#include <chrono>\n"),
    ('        MappedFile query_file(data_dir + "/queries.txt");',
     '        const auto t0 = chrono::steady_clock::now();\n        MappedFile query_file(data_dir + "/queries.txt");'),
    ('        parse_queries(query_file);',
     '        parse_queries(query_file);\n        const auto t1 = chrono::steady_clock::now();'),
    ('        build_index(docs_file);',
     '        build_index(docs_file);\n        const auto t2 = chrono::steady_clock::now();'),
    ('        precompute_common_phrases();',
     '        precompute_common_phrases();\n        const auto t3 = chrono::steady_clock::now();'),
    ('        if (!output) throw runtime_error("output write failure");',
     '        if (!output) throw runtime_error("output write failure");\n'
     '        const auto t4 = chrono::steady_clock::now();\n'
     '        auto ms = [](auto a, auto b) { return chrono::duration<double, milli>(b-a).count(); };\n'
     '        cerr << "PHASE_MS query_parse=" << ms(t0,t1)\n'
     '             << " corpus_index=" << ms(t1,t2)\n'
     '             << " phrase_automaton=" << ms(t2,t3)\n'
     '             << " query_eval_output=" << ms(t3,t4) << "\\n";'),
]
for old, new in changes:
    if source.count(old) != 1:
        raise SystemExit(f"expected exactly one phase marker: {old[:40]}")
    source = source.replace(old, new)
Path(sys.argv[2]).write_text(source)
