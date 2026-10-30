#!/usr/bin/env python3
"""Generate an isolated SSE4.2 CRC32C token-fingerprint challenger."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: crc32c_tokens.py SOURCE OUTPUT")
src = Path(sys.argv[1]).read_text()

def swap(old: str, new: str, count: int = 1) -> None:
    global src
    actual = src.count(old)
    if actual != count:
        raise SystemExit(f"marker count {actual} != {count}: {old[:80]!r}")
    src = src.replace(old, new)

swap("#include <limits>\n", "#include <limits>\n#include <nmmintrin.h>\n")
swap("""        for (unsigned char c : word) {
            hash ^= lower_ascii(c);
            hash *= 16777619u;
        }""",
"""        for (unsigned char c : word)
            hash = _mm_crc32_u8(hash, static_cast<unsigned char>(c | 0x20u));""")
swap("""                            hash ^= static_cast<unsigned char>(c | 0x20u);
                            hash *= 16777619u;""",
"""                            hash = _mm_crc32_u8(
                                hash, static_cast<unsigned char>(c | 0x20u));""")
Path(sys.argv[2]).write_text(src)
