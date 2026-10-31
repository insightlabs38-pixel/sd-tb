"""Compare the actual packed-lane helper against an independent linear oracle."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
if "small_contains(" not in source:
    print("No packed-lane helper in this source")
    raise SystemExit(0)


def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


constants = re.search(r"static constexpr unsigned small_shift\s*=.*?;", source, re.S)[0]
program = """#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
""" + constants + "\n" + function("static unsigned small_count(") + "\n" + function("static bool small_contains(") + r'''
int main() {
    std::mt19937 random(20261031);
    for (unsigned size = 0; size <= small_limit; ++size) {
        for (unsigned trial = 0; trial < 100000; ++trial) {
            std::array<uint32_t, small_limit> values{};
            uint64_t state = uint64_t(size) << small_shift;
            for (unsigned j = 0; j < size; ++j) {
                values[j] = random() & ((1u << position_bits) - 1);
                state |= uint64_t(values[j]) << (j * position_bits);
            }
            for (unsigned j = 0; j < 10; ++j) {
                uint32_t position = j < size ? values[j] : random() & ((1u << position_bits) - 1);
                bool expected = std::find(values.begin(), values.begin() + size, position) != values.begin() + size;
                if (small_contains(state, position) != expected) return 1;
            }
        }
    }
    std::puts("6,000,000 independent packed-lane membership checks: PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path / "check.cpp").write_text(program)
    subprocess.run(["g++", "-std=c++20", "-O3", str(path / "check.cpp"), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
