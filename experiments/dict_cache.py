#!/usr/bin/env python3
"""Build exact dictionary-lookup alternatives without modifying the champion."""
import argparse
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("source")
p.add_argument("destination")
p.add_argument("variant", choices=("fold", "cache", "both"))
args = p.parse_args()
s = Path(args.source).read_text()
def replace_once(old, new):
    global s
    assert s.count(old) == 1, (old, s.count(old))
    s = s.replace(old, new, 1)

if args.variant in ("fold", "both"):
    replace_once(
        "lower_ascii(static_cast<unsigned char>(a[i])) != lower_ascii(static_cast<unsigned char>(b[i]))",
        "(static_cast<unsigned char>(a[i]) | 0x20u) != (static_cast<unsigned char>(b[i]) | 0x20u)"
    )

if args.variant in ("cache", "both"):
    replace_once(
        "public:\n    void reserve(size_t count) {",
        """public:
    struct CacheSlot {
        uint32_t hash = 0;
        uint32_t id = numeric_limits<uint32_t>::max();
        string_view word{};
    };
    uint32_t lookup_cached(string_view word, uint32_t hash, CacheSlot &entry) const {
        if (entry.hash == hash && FoldEqual{}(entry.word, word))
            return entry.id;
        uint32_t id = lookup(word, hash);
        entry = {hash, id, word};
        return id;
    }
    void reserve(size_t count) {"""
    )
    replace_once(
        "c.offsets.reserve(static_cast<size_t>(stop - p) / 500);\n                while (p < stop) {",
        "c.offsets.reserve(static_cast<size_t>(stop - p) / 500);\n                array<FlatDictionary::CacheSlot, 1024> cache{};\n                while (p < stop) {"
    )
    replace_once(
        "uint32_t term = dictionary.lookup(string_view(start, s - start), hash);",
        "uint32_t term = dictionary.lookup_cached(string_view(start, s - start), hash, cache[hash & 1023u]);"
    )
Path(args.destination).write_text(s)
