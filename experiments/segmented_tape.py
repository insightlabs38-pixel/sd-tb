#!/usr/bin/env python3
"""Isolated zero-copy segmented token tape, retaining document-global offsets."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: segmented_tape.py SOURCE OUTPUT")
src = Path(sys.argv[1]).read_text()
def swap(old, new):
    global src
    n = src.count(old)
    if n != 1:
        raise SystemExit(f"expected one marker, got {n}: {old[:75]!r}")
    src = src.replace(old, new)

swap("    vector<uint32_t> tape;\n    vector<uint32_t> offsets;",
"""    vector<vector<uint32_t>> tapes;
    vector<uint32_t> tape_bases;
    vector<uint8_t> doc_chunks;
    vector<uint32_t> offsets;""")
swap("""                    for (uint32_t i = offsets[doc]; i < offsets[doc + 1]; ++i) {
                        uint32_t tok = tape[i];""",
"""                    const uint8_t chunk = doc_chunks[doc];
                    const auto &local_tape = tapes[chunk];
                    const uint32_t base = tape_bases[chunk];
                    for (uint32_t i = offsets[doc] - base; i < offsets[doc + 1] - base; ++i) {
                        uint32_t tok = local_tape[i];""")
swap("""        size_t total_tokens = 0, total_docs = 0;
        for (const Chunk &c : chunks) {
            total_tokens += c.tape.size();
            total_docs += c.docs;
        }
        tape.reserve(total_tokens);
        offsets.reserve(total_docs + 1);
        uint32_t expected_doc = 0;
        for (const Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc)
                throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            const uint32_t base = static_cast<uint32_t>(tape.size());
            for (size_t j = 0; j < c.docs; ++j)
                offsets.push_back(base + c.offsets[j]);
            tape.insert(tape.end(), c.tape.begin(), c.tape.end());
        }
        offsets.push_back(static_cast<uint32_t>(tape.size()));""",
"""        size_t total_docs = 0;
        for (const Chunk &c : chunks) total_docs += c.docs;
        tapes.reserve(chunks.size());
        tape_bases.reserve(chunks.size());
        doc_chunks.reserve(total_docs);
        offsets.reserve(total_docs + 1);
        uint32_t expected_doc = 0, base = 0;
        for (Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc)
                throw runtime_error("nonconsecutive document chunks");
            tape_bases.push_back(base);
            doc_chunks.insert(doc_chunks.end(), c.docs, static_cast<uint8_t>(tapes.size()));
            for (size_t j = 0; j < c.docs; ++j)
                offsets.push_back(base + c.offsets[j]);
            base += static_cast<uint32_t>(c.tape.size());
            expected_doc += static_cast<uint32_t>(c.docs);
            tapes.push_back(move(c.tape));
        }
        offsets.push_back(base);""")
swap("        const uint32_t begin = offsets[doc], end = offsets[doc + 1];",
"""        const uint8_t chunk = doc_chunks[doc];
        const auto &local_tape = tapes[chunk];
        const uint32_t base = tape_bases[chunk];
        const uint32_t begin = offsets[doc] - base, end = offsets[doc + 1] - base;""")
swap("            if (tape[i] != key) continue;", "            if (local_tape[i] != key) continue;")
swap("            while (j < k && tape[start + j] == terms[j] + 1) ++j;",
     "            while (j < k && local_tape[start + j] == terms[j] + 1) ++j;")
Path(sys.argv[2]).write_text(src)
