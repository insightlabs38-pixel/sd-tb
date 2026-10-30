#!/usr/bin/env python3
"""Generate a rarest-position anchored exact phrase verifier."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
old = """    bool contains_phrase(uint32_t doc, const vector<uint32_t> &terms) const {
        const uint32_t begin = offsets[doc], end = offsets[doc + 1];
        const size_t k = terms.size();
        if (end - begin < k) return false;
        for (uint32_t i = begin; i + k <= end; ++i) {
            if (tape[i] != terms[0] + 1) continue;
            size_t j = 1;
            while (j < k && tape[i + j] == terms[j] + 1) ++j;
            if (j == k) return true;
        }
        return false;
    }"""
new = """    bool contains_phrase(uint32_t doc, const vector<uint32_t> &terms,
                         size_t anchor) const {
        const uint32_t begin = offsets[doc], end = offsets[doc + 1];
        const size_t k = terms.size();
        if (end - begin < k) return false;
        const uint32_t key = terms[anchor] + 1;
        for (uint32_t i = begin + anchor; i + k - anchor <= end; ++i) {
            if (tape[i] != key) continue;
            const uint32_t start = i - static_cast<uint32_t>(anchor);
            size_t j = 0;
            while (j < k && tape[start + j] == terms[j] + 1) ++j;
            if (j == k) return true;
        }
        return false;
    }"""
call_old = "if (contains_phrase(doc, q.terms)) r.add(doc);"
call_new = "if (contains_phrase(doc, q.terms, best)) r.add(doc);"
if source.count(old) != 1 or source.count(call_old) != 1:
    raise SystemExit("expected exact phrase verifier and call site")
source = source.replace(old, new).replace(call_old, call_new)
Path(sys.argv[2]).write_text(source)
