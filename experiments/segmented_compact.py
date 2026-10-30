#!/usr/bin/env python3
"""Build a zero-copy segmented compact phrase tape challenger."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: segmented_compact.py SOURCE OUTPUT")
src = Path(sys.argv[1]).read_text()

def swap(old, new, count=1):
    global src
    actual = src.count(old)
    if actual != count:
        raise SystemExit(f"marker count {actual} != {count}: {old[:70]!r}")
    src = src.replace(old, new)

swap("""    vector<uint32_t> tape;
    vector<uint16_t> tape16;
    vector<uint32_t> phrase_token;
    bool compact_tape = false;
    vector<uint32_t> offsets;""",
"""    vector<vector<uint32_t>> tapes32;
    vector<vector<uint16_t>> tapes16;
    vector<uint32_t> tape_bases;
    vector<uint8_t> doc_chunks;
    vector<uint32_t> phrase_token;
    bool compact_tape = false;
    vector<uint32_t> offsets;""")
swap("""    uint32_t tape_at(uint32_t i) const {
        return compact_tape ? tape16[i] : tape[i];
    }""",
"""    uint32_t tape_at(uint32_t i, uint8_t chunk) const {
        const uint32_t local = i - tape_bases[chunk];
        return compact_tape ? tapes16[chunk][local] : tapes32[chunk][local];
    }""")
swap("""                    uint32_t state = 0;
                    for (uint32_t i = offsets[doc]; i < offsets[doc + 1]; ++i) {
                        uint32_t tok = tape_at(i);""",
"""                    uint32_t state = 0;
                    const uint8_t chunk = doc_chunks[doc];
                    for (uint32_t i = offsets[doc]; i < offsets[doc + 1]; ++i) {
                        uint32_t tok = tape_at(i, chunk);""")
swap("""        const uint32_t begin = offsets[doc], end = offsets[doc + 1];
        const size_t k = terms.size();""",
"""        const uint8_t chunk = doc_chunks[doc];
        const uint32_t begin = offsets[doc], end = offsets[doc + 1];
        const size_t k = terms.size();""")
swap("tape_at(i) != key", "tape_at(i, chunk) != key")
swap("tape_at(start + j) == phrase_id(terms[j])",
     "tape_at(start + j, chunk) == phrase_id(terms[j])")
swap("""        size_t total_tokens = 0, total_docs = 0;
        for (const Chunk &c : chunks) {
            total_tokens += compact_tape ? c.tape16.size() : c.tape.size();
            total_docs += c.docs;
        }
        if (compact_tape) tape16.reserve(total_tokens);
        else tape.reserve(total_tokens);
        offsets.reserve(total_docs + 1);
        uint32_t expected_doc = 0;
        for (const Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc)
                throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            const uint32_t base = static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size());
            for (size_t j = 0; j < c.docs; ++j)
                offsets.push_back(base + c.offsets[j]);
            if (compact_tape) tape16.insert(tape16.end(), c.tape16.begin(), c.tape16.end());
            else tape.insert(tape.end(), c.tape.begin(), c.tape.end());
        }
        offsets.push_back(static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size()));""",
"""        size_t total_docs = 0;
        for (const Chunk &c : chunks) total_docs += c.docs;
        tapes32.reserve(chunks.size());
        tapes16.reserve(chunks.size());
        tape_bases.reserve(chunks.size());
        doc_chunks.reserve(total_docs);
        offsets.reserve(total_docs + 1);
        uint32_t expected_doc = 0, base = 0;
        for (Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc)
                throw runtime_error("nonconsecutive document chunks");
            tape_bases.push_back(base);
            doc_chunks.insert(doc_chunks.end(), c.docs, static_cast<uint8_t>(tapes32.size()));
            for (size_t j = 0; j < c.docs; ++j)
                offsets.push_back(base + c.offsets[j]);
            base += static_cast<uint32_t>(compact_tape ? c.tape16.size() : c.tape.size());
            expected_doc += static_cast<uint32_t>(c.docs);
            tapes32.push_back(move(c.tape));
            tapes16.push_back(move(c.tape16));
        }
        offsets.push_back(base);""")
Path(sys.argv[2]).write_text(src)
