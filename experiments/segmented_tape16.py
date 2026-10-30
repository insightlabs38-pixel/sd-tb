#!/usr/bin/env python3
"""Generate a zero-copy per-chunk phrase tape, preserving 16-bit fallback semantics."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: segmented_tape16.py SOURCE OUTPUT")
src = Path(sys.argv[1]).read_text()

def swap(old: str, new: str) -> None:
    global src
    n = src.count(old)
    if n != 1:
        raise SystemExit(f"marker count {n} != 1: {old[:90]!r}")
    src = src.replace(old, new)

swap("""    vector<uint32_t> tape;
    vector<uint16_t> tape16;
    vector<uint32_t> phrase_token;""",
"""    vector<vector<uint32_t>> tape_chunks;
    vector<vector<uint16_t>> tape16_chunks;
    vector<uint32_t> tape_bases;
    vector<uint8_t> doc_chunk;
    vector<uint32_t> phrase_token;""")

swap("""    uint32_t tape_at(uint32_t i) const {
        return compact_tape ? tape16[i] : tape[i];
    }
""", "")

swap("""                    for (uint32_t i = offsets[doc]; i < offsets[doc + 1]; ++i) {
                        uint32_t tok = tape_at(i);""",
"""                    const uint32_t chunk = doc_chunk[doc];
                    const uint32_t base = tape_bases[chunk];
                    const uint16_t *t16 = compact_tape ? tape16_chunks[chunk].data() : nullptr;
                    const uint32_t *t32 = compact_tape ? nullptr : tape_chunks[chunk].data();
                    for (uint32_t i = offsets[doc] - base; i < offsets[doc + 1] - base; ++i) {
                        uint32_t tok = compact_tape ? t16[i] : t32[i];""")

swap("""        if (compact_tape) tape16.reserve(total_tokens);
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
"""        tape_chunks.reserve(chunks.size());
        tape16_chunks.reserve(chunks.size());
        tape_bases.reserve(chunks.size());
        doc_chunk.reserve(total_docs);
        offsets.reserve(total_docs + 1);
        uint32_t expected_doc = 0, base = 0;
        for (size_t ci = 0; ci < chunks.size(); ++ci) {
            Chunk &c = chunks[ci];
            if (c.docs && c.first_doc != expected_doc)
                throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            tape_bases.push_back(base);
            for (size_t j = 0; j < c.docs; ++j) {
                offsets.push_back(base + c.offsets[j]);
                doc_chunk.push_back(static_cast<uint8_t>(ci));
            }
            base += static_cast<uint32_t>(compact_tape ? c.tape16.size() : c.tape.size());
            if (compact_tape) tape16_chunks.push_back(move(c.tape16));
            else tape_chunks.push_back(move(c.tape));
        }
        offsets.push_back(base);""")

swap("""        const uint32_t begin = offsets[doc], end = offsets[doc + 1];
        const size_t k = terms.size();
        if (end - begin < k) return false;
        const uint32_t key = phrase_id(terms[anchor]);
        for (uint32_t i = begin + anchor; i + k - anchor <= end; ++i) {
            if (tape_at(i) != key) continue;
            const uint32_t start = i - static_cast<uint32_t>(anchor);
            size_t j = 0;
            while (j < k && tape_at(start + j) == phrase_id(terms[j])) ++j;
            if (j == k) return true;
        }""",
"""        const uint32_t chunk = doc_chunk[doc];
        const uint32_t base = tape_bases[chunk];
        const uint32_t begin = offsets[doc] - base, end = offsets[doc + 1] - base;
        const size_t k = terms.size();
        if (end - begin < k) return false;
        const uint16_t *t16 = compact_tape ? tape16_chunks[chunk].data() : nullptr;
        const uint32_t *t32 = compact_tape ? nullptr : tape_chunks[chunk].data();
        auto at = [&](uint32_t i) -> uint32_t {
            return compact_tape ? t16[i] : t32[i];
        };
        const uint32_t key = phrase_id(terms[anchor]);
        for (uint32_t i = begin + anchor; i + k - anchor <= end; ++i) {
            if (at(i) != key) continue;
            const uint32_t start = i - static_cast<uint32_t>(anchor);
            size_t j = 0;
            while (j < k && at(start + j) == phrase_id(terms[j])) ++j;
            if (j == k) return true;
        }""")

Path(sys.argv[2]).write_text(src)
