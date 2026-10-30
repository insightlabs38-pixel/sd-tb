#!/usr/bin/env python3
"""Generate a short-token direct-table challenger from the verified engine."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
replacements = (
    (
        "    vector<string_view> keys;\n",
        """    vector<string_view> keys;
    vector<uint32_t> short_ids;

    static size_t short_key(string_view word) {
        size_t key = word.size() == 1 ? 0 : word.size() == 2 ? 36 : 1332;
        size_t value = 0;
        for (unsigned char c : word) {
            c = lower_ascii(c);
            value = value * 36 + (c >= 'a' ? c - 'a' : c - '0' + 26);
        }
        return key + value;
    }
""",
    ),
    (
        "    pair<uint32_t, bool> intern(string_view word) {\n",
        """    pair<uint32_t, bool> intern(string_view word) {
        if (word.size() <= 3) {
            if (short_ids.empty()) short_ids.resize(47988);
            uint32_t &slot = short_ids[short_key(word)];
            if (slot) return {slot - 1, false};
            uint32_t id = static_cast<uint32_t>(keys.size());
            keys.push_back(word);
            slot = id + 1;
            return {id, true};
        }
""",
    ),
    (
        "    uint32_t lookup(string_view word) const {\n",
        """    uint32_t lookup(string_view word) const {
        if (word.size() <= 3) {
            if (short_ids.empty()) return numeric_limits<uint32_t>::max();
            uint32_t slot = short_ids[short_key(word)];
            return slot ? slot - 1 : numeric_limits<uint32_t>::max();
        }
""",
    ),
)
for old, new in replacements:
    if source.count(old) != 1:
        raise SystemExit("Expected exactly one source anchor")
    source = source.replace(old, new)
Path(sys.argv[2]).write_text(source)
