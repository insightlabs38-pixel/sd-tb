#!/usr/bin/env python3
"""Compare branchless ASCII token recognition and folding."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
mode = sys.argv[3] if len(sys.argv) > 3 else "token"
old = """static inline bool token_char(unsigned char c) {
    c = lower_ascii(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}"""
new = """static inline bool token_char(unsigned char c) {
    const unsigned folded = static_cast<unsigned>(c | 0x20u);
    return ((folded - 'a') < 26u) | ((static_cast<unsigned>(c) - '0') < 10u);
}"""
if source.count(old) != 1:
    raise SystemExit("token_char source marker changed")
source = source.replace(old, new)
if mode == "both":
    old = """static inline unsigned char lower_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}"""
    new = """static inline unsigned char lower_ascii(unsigned char c) {
    const unsigned x = static_cast<unsigned>(c) - 'A';
    return static_cast<unsigned char>(c | ((x < 26u) << 5));
}"""
    if source.count(old) != 1:
        raise SystemExit("lower_ascii source marker changed")
    source = source.replace(old, new)
elif mode != "token":
    raise SystemExit("mode must be token or both")
Path(sys.argv[2]).write_text(source)
