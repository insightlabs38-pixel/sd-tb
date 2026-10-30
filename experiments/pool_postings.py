#!/usr/bin/env python3
"""Generate an isolated postings-allocator challenger from the verified source."""
from pathlib import Path
import sys

if len(sys.argv) != 4 or sys.argv[1] not in ("pool", "monotonic"):
    raise SystemExit("usage: pool_postings.py pool|monotonic SOURCE OUTPUT")
mode, input_path, output_path = sys.argv[1:]
src = Path(input_path).read_text()
def swap(old, new):
    global src
    n = src.count(old)
    if n != 1:
        raise SystemExit(f"expected one marker, got {n}: {old[:80]!r}")
    src = src.replace(old, new)
swap("#include <queue>\n", "#include <queue>\n#include <memory>\n#include <memory_resource>\n")
swap("        struct Chunk {\n            vector<uint32_t> tape;",
     "        struct Chunk {\n            unique_ptr<pmr::memory_resource> arena;\n            vector<uint32_t> tape;")
swap("            vector<vector<uint32_t>> postings;\n            vector<uint32_t> seen;",
     "            vector<pmr::vector<uint32_t>> postings;\n            vector<uint32_t> seen;")
kind = "unsynchronized_pool_resource" if mode == "pool" else "monotonic_buffer_resource"
swap("                c.postings.resize(postings.size());",
     f"""                c.arena = make_unique<pmr::{kind}>();
                c.postings.reserve(postings.size());
                for (size_t t = 0; t < postings.size(); ++t)
                    c.postings.emplace_back(pmr::polymorphic_allocator<uint32_t>(c.arena.get()));""")
Path(output_path).write_text(src)
