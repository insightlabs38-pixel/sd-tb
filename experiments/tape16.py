#!/usr/bin/env python3
"""Generate an isolated 16-bit token-tape challenger with exact 32-bit fallback."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: tape16.py SOURCE OUTPUT")
src = Path(sys.argv[1]).read_text()

def swap(old, new):
    global src
    if src.count(old) != 1:
        raise SystemExit(f"expected one marker: {old[:60]!r}, found {src.count(old)}")
    src = src.replace(old, new)

swap("    vector<uint32_t> tape;\n    vector<uint32_t> offsets;\n    vector<Query> queries;", "    vector<uint32_t> tape;\n    vector<uint16_t> tape16;\n    bool compact_tape = false;\n    vector<uint32_t> offsets;\n    vector<Query> queries;")
swap("    vector<Result> pattern_results;\n", "    vector<Result> pattern_results;\n\n    uint32_t tape_at(uint32_t i) const {\n        return compact_tape ? tape16[i] : tape[i];\n    }\n")
swap("uint32_t tok = tape[i];", "uint32_t tok = tape_at(i);")
swap("if (tape[i] != key) continue;", "if (tape_at(i) != key) continue;")
swap("tape[start + j] == terms[j] + 1", "tape_at(start + j) == terms[j] + 1")
swap("        tape.reserve(total_tokens);", "        compact_tape = postings.size() <= UINT16_MAX;\n        fprintf(stderr, \"TAPE16=%d QUERY_TERMS=%zu TOKENS=%zu\\n\", int(compact_tape), postings.size(), total_tokens);\n        if (compact_tape) tape16.reserve(total_tokens);\n        else tape.reserve(total_tokens);")
swap("            const uint32_t base = static_cast<uint32_t>(tape.size());", "            const uint32_t base = static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size());")
swap("            tape.insert(tape.end(), c.tape.begin(), c.tape.end());", "            if (compact_tape) {\n                for (uint32_t tok : c.tape) tape16.push_back(static_cast<uint16_t>(tok));\n            } else tape.insert(tape.end(), c.tape.begin(), c.tape.end());")
swap("        offsets.push_back(static_cast<uint32_t>(tape.size()));", "        offsets.push_back(static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size()));")

Path(sys.argv[2]).write_text(src)
