#!/usr/bin/env python3
"""Transfer single-worker posting vectors without copying; merge shared terms."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: move_postings.py SOURCE OUTPUT")
src = Path(sys.argv[1]).read_text()
old = """        for (size_t term = 0; term < postings.size(); ++term) {
            size_t total = 0;
            for (const Chunk &c : chunks) total += c.postings[term].size();
            postings[term].reserve(total);
            for (const Chunk &c : chunks) {
                const auto &part = c.postings[term];
                postings[term].insert(postings[term].end(), part.begin(), part.end());
            }
        }"""
new = """        for (size_t term = 0; term < postings.size(); ++term) {
            size_t total = 0;
            int only = -1;
            for (size_t i = 0; i < chunks.size(); ++i) {
                const size_t n = chunks[i].postings[term].size();
                total += n;
                if (n) only = only == -1 ? static_cast<int>(i) : -2;
            }
            if (only >= 0) {
                postings[term] = move(chunks[only].postings[term]);
                continue;
            }
            postings[term].reserve(total);
            for (const Chunk &c : chunks) {
                const auto &part = c.postings[term];
                postings[term].insert(postings[term].end(), part.begin(), part.end());
            }
        }"""
if src.count(old) != 1:
    raise SystemExit(f"expected one posting merge, got {src.count(old)}")
Path(sys.argv[2]).write_text(src.replace(old, new))
