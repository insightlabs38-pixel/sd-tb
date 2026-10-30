#!/usr/bin/env python3
"""Build a phrase-only 16-bit tape challenger; preserve exact 32-bit fallback."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: phrase_tape16.py SOURCE OUTPUT")
src = Path(sys.argv[1]).read_text()

def swap(old, new, count=1):
    global src
    actual = src.count(old)
    if actual != count:
        raise SystemExit(f"marker count {actual} != {count}: {old[:70]!r}")
    src = src.replace(old, new)

swap("    vector<uint32_t> tape;\n    vector<uint32_t> offsets;",
"""    vector<uint32_t> tape;
    vector<uint16_t> tape16;
    vector<uint32_t> phrase_token;
    bool compact_tape = false;
    vector<uint32_t> offsets;""")
swap("    vector<Result> pattern_results;\n",
"""    vector<Result> pattern_results;

    uint32_t tape_at(uint32_t i) const {
        return compact_tape ? tape16[i] : tape[i];
    }
    uint32_t phrase_id(uint32_t term) const {
        return compact_tape ? phrase_token[term] : term + 1;
    }
""")
swap("            for (uint32_t t : q.terms) {\n                const uint32_t tok = t + 1;",
"""            for (uint32_t t : q.terms) {
                const uint32_t tok = phrase_id(t);""")
swap("    void build_index(const MappedFile &input) {",
"""    void configure_tape() {
        phrase_token.assign(postings.size(), 0);
        uint32_t next = 0;
        for (const Query &q : queries) {
            if (!q.phrase) continue;
            for (uint32_t term : q.terms)
                if (!phrase_token[term]) phrase_token[term] = ++next;
        }
        compact_tape = next <= numeric_limits<uint16_t>::max();
        fprintf(stderr, "PHRASE_TAPE16=%d PHRASE_TERMS=%u QUERY_TERMS=%zu\\n",
                int(compact_tape), next, postings.size());
        if (!compact_tape) phrase_token.clear();
    }

    void build_index(const MappedFile &input) {""")
swap("        struct Chunk {\n            vector<uint32_t> tape;\n            vector<uint32_t> offsets;",
"""        struct Chunk {
            vector<uint32_t> tape;
            vector<uint16_t> tape16;
            vector<uint32_t> offsets;""")
swap("                c.tape.reserve(static_cast<size_t>(stop - p) / 7);",
"""                if (compact_tape) c.tape16.reserve(static_cast<size_t>(stop - p) / 7);
                else c.tape.reserve(static_cast<size_t>(stop - p) / 7);""")
swap("c.offsets.push_back(static_cast<uint32_t>(c.tape.size()));",
     "c.offsets.push_back(static_cast<uint32_t>(compact_tape ? c.tape16.size() : c.tape.size()));", 2)
swap("                        c.tape.push_back(id);",
"""                        if (compact_tape)
                            c.tape16.push_back(id ? static_cast<uint16_t>(phrase_token[id - 1]) : 0);
                        else c.tape.push_back(id);""")
swap("            total_tokens += c.tape.size();",
     "            total_tokens += compact_tape ? c.tape16.size() : c.tape.size();")
swap("        tape.reserve(total_tokens);",
"""        if (compact_tape) tape16.reserve(total_tokens);
        else tape.reserve(total_tokens);""")
swap("            const uint32_t base = static_cast<uint32_t>(tape.size());",
     "            const uint32_t base = static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size());")
swap("            tape.insert(tape.end(), c.tape.begin(), c.tape.end());",
"""            if (compact_tape) tape16.insert(tape16.end(), c.tape16.begin(), c.tape16.end());
            else tape.insert(tape.end(), c.tape.begin(), c.tape.end());""")
swap("        offsets.push_back(static_cast<uint32_t>(tape.size()));",
     "        offsets.push_back(static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size()));")
swap("uint32_t tok = tape[i];", "uint32_t tok = tape_at(i);")
swap("        const uint32_t key = terms[anchor] + 1;",
     "        const uint32_t key = phrase_id(terms[anchor]);")
swap("if (tape[i] != key) continue;", "if (tape_at(i) != key) continue;")
swap("tape[start + j] == terms[j] + 1", "tape_at(start + j) == phrase_id(terms[j])")
swap("        parse_queries(query_file);\n        MappedFile docs_file",
     "        parse_queries(query_file);\n        configure_tape();\n        MappedFile docs_file")
Path(sys.argv[2]).write_text(src)
