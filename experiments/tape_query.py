#!/usr/bin/env python3
"""Generate isolated exact Track B packed-tape and work-stealing challengers."""
from pathlib import Path
import sys

if len(sys.argv) != 4 or sys.argv[3] not in ("packed", "dynamic", "combined"):
    raise SystemExit("usage: tape_query.py SOURCE OUTPUT packed|dynamic|combined")
src = Path(sys.argv[1]).read_text()
variant = sys.argv[3]

def swap(old, new):
    global src
    if src.count(old) != 1:
        raise SystemExit(f"expected one source marker: {old[:65]!r}, got {src.count(old)}")
    src = src.replace(old, new)

if variant in ("packed", "combined"):
    swap("    vector<uint32_t> tape;\n    vector<uint32_t> offsets;\n    vector<Query> queries;",
         "    vector<array<uint8_t, 3>> tape;\n    vector<uint32_t> offsets;\n    vector<Query> queries;")
    swap("    vector<Result> pattern_results;\n",
         "    vector<Result> pattern_results;\n\n"
         "    uint32_t tape_at(uint32_t i) const {\n"
         "        const auto &v = tape[i];\n"
         "        return uint32_t(v[0]) | (uint32_t(v[1]) << 8) | (uint32_t(v[2]) << 16);\n"
         "    }\n")
    swap("uint32_t tok = tape[i];", "uint32_t tok = tape_at(i);")
    swap("if (tape[i] != key) continue;", "if (tape_at(i) != key) continue;")
    swap("tape[start + j] == terms[j] + 1", "tape_at(start + j) == terms[j] + 1")
    swap("        tape.reserve(total_tokens);",
         '        if (postings.size() >= (1u << 24)) throw runtime_error("packed tape vocabulary overflow");\n'
         "        tape.reserve(total_tokens);")
    swap("            tape.insert(tape.end(), c.tape.begin(), c.tape.end());",
         "            for (uint32_t tok : c.tape)\n"
         "                tape.push_back({static_cast<uint8_t>(tok), static_cast<uint8_t>(tok >> 8), static_cast<uint8_t>(tok >> 16)});")

if variant in ("dynamic", "combined"):
    swap("#include <algorithm>\n", "#include <algorithm>\n#include <atomic>\n")
    swap("        vector<exception_ptr> errors(workers);\n        auto evaluate = [&](size_t worker) {",
         "        vector<exception_ptr> errors(workers);\n"
         "        atomic<size_t> next_query{0};\n"
         "        auto evaluate = [&](size_t worker) {")
    swap("                const size_t first = queries.size() * worker / workers;\n"
         "                const size_t last = queries.size() * (worker + 1) / workers;\n"
         "                for (size_t qi = first; qi < last; ++qi) {",
         "                for (;;) {\n"
         "                    const size_t first = next_query.fetch_add(64, memory_order_relaxed);\n"
         "                    if (first >= queries.size()) break;\n"
         "                    const size_t last = min(first + 64, queries.size());\n"
         "                    for (size_t qi = first; qi < last; ++qi) {")
    swap("                        : (q.phrase ? run_phrase(q) : run_bool(q, work, tmp));\n"
         "                }\n            } catch",
         "                        : (q.phrase ? run_phrase(q) : run_bool(q, work, tmp));\n"
         "                    }\n                }\n            } catch")

Path(sys.argv[2]).write_text(src)
