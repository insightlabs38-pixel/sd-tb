#!/usr/bin/env python3
"""Fuse the corpus token scan and query-dictionary fingerprint calculation."""
from pathlib import Path
import sys

if len(sys.argv) != 4 or sys.argv[3] not in ("fold", "or"):
    raise SystemExit("usage: fused_hash.py SOURCE OUTPUT fold|or")
source = Path(sys.argv[1]).read_text()
old = """    uint32_t lookup(string_view word) const {
        if (slots.empty()) return numeric_limits<uint32_t>::max();
        const uint32_t hash = fingerprint(word);"""
new = """    uint32_t lookup(string_view word, uint32_t known_hash = 0) const {
        if (slots.empty()) return numeric_limits<uint32_t>::max();
        const uint32_t hash = known_hash ? known_hash : fingerprint(word);"""
if source.count(old) != 1:
    raise SystemExit("dictionary lookup marker changed")
source = source.replace(old, new)
old = """                        do { ++s; } while (s < line_end && token_char(static_cast<unsigned char>(*s)));
                        uint32_t term = dictionary.lookup(string_view(start, s - start));"""
fold = ("lower_ascii(c)" if sys.argv[3] == "fold"
        else "static_cast<unsigned char>(c | 0x20u)")
new = """                        uint32_t hash = 2166136261u;
                        do {
                            const unsigned char c = static_cast<unsigned char>(*s);
                            hash ^= FOLD;
                            hash *= 16777619u;
                            ++s;
                        } while (s < line_end && token_char(static_cast<unsigned char>(*s)));
                        if (!hash) hash = 1;
                        uint32_t term = dictionary.lookup(string_view(start, s - start), hash);""".replace("FOLD", fold)
if source.count(old) != 1:
    raise SystemExit("corpus token scan marker changed")
source = source.replace(old, new)
Path(sys.argv[2]).write_text(source)
