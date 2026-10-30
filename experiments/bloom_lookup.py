#!/usr/bin/env python3
"""Compare one- and two-probe fingerprint filters before query-dictionary lookups."""
from pathlib import Path
import sys

if len(sys.argv) != 4 or sys.argv[3] not in ("one", "two"):
    raise SystemExit("usage: bloom_lookup.py SOURCE OUTPUT one|two")

src = Path(sys.argv[1]).read_text()

def replace_once(old, new):
    global src
    if src.count(old) != 1:
        raise SystemExit("unexpected source marker: " + old[:65])
    src = src.replace(old, new)

replace_once(
    "    vector<string_view> keys;\n",
    "    vector<string_view> keys;\n"
    "    static constexpr uint32_t filter_bits = 1u << 22;\n"
    "    vector<uint64_t> filter;\n"
)
second_set = (
    "            filter[bit2 >> 6] |= 1ull << (bit2 & 63);\n"
    if sys.argv[3] == "two" else ""
)
second_check = (
    "            if (!(filter[bit2 >> 6] & (1ull << (bit2 & 63))))\n"
    "                return numeric_limits<uint32_t>::max();\n"
    if sys.argv[3] == "two" else ""
)
replace_once(
    "public:\n    void reserve(size_t count) {",
    "public:\n"
    "    void build_filter() {\n"
    "        filter.assign(filter_bits / 64, 0);\n"
    "        for (string_view word : keys) {\n"
    "            const uint32_t hash = fingerprint(word);\n"
    "            const uint32_t bit1 = hash & (filter_bits - 1);\n"
    "            const uint32_t bit2 = (hash * 0x9e3779b1u) >> 10;\n"
    "            filter[bit1 >> 6] |= 1ull << (bit1 & 63);\n"
    + second_set +
    "        }\n"
    "    }\n"
    "    void reserve(size_t count) {"
)
replace_once(
    "        const uint32_t hash = known_hash ? known_hash : fingerprint(word);\n"
    "        size_t i = hash & (slots.size() - 1);",
    "        const uint32_t hash = known_hash ? known_hash : fingerprint(word);\n"
    "        if (!filter.empty()) {\n"
    "            const uint32_t bit1 = hash & (filter_bits - 1);\n"
    "            if (!(filter[bit1 >> 6] & (1ull << (bit1 & 63))))\n"
    "                return numeric_limits<uint32_t>::max();\n"
    + (
        "            const uint32_t bit2 = (hash * 0x9e3779b1u) >> 10;\n"
        if sys.argv[3] == "two" else ""
    )
    + second_check +
    "        }\n"
    "        size_t i = hash & (slots.size() - 1);"
)
replace_once(
    "        parse_queries(query_file);\n",
    "        parse_queries(query_file);\n"
    "        dictionary.build_filter();\n"
)
Path(sys.argv[2]).write_text(src)
