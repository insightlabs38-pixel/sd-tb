#!/usr/bin/env python3
"""Generate an adaptive unbalanced-AND challenger from the verified source."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
ratio = int(sys.argv[3])
if ratio not in (16, 32, 64):
    raise SystemExit("unsupported threshold")
old = """            if (op == 'A') {
                size_t i = 0, j = 0;
                while (i < work.size() && j < rhs.size()) {
                    if (work[i] < rhs[j]) ++i;
                    else if (rhs[j] < work[i]) ++j;
                    else { tmp.push_back(work[i]); ++i; ++j; }
                }
            } else if (op == 'O') {"""
new = """            if (op == 'A') {
                if (work.size() * RATIO < rhs.size()) {
                    for (uint32_t doc : work)
                        if (binary_search(rhs.begin(), rhs.end(), doc))
                            tmp.push_back(doc);
                } else if (rhs.size() * RATIO < work.size()) {
                    for (uint32_t doc : rhs)
                        if (binary_search(work.begin(), work.end(), doc))
                            tmp.push_back(doc);
                } else {
                    size_t i = 0, j = 0;
                    while (i < work.size() && j < rhs.size()) {
                        if (work[i] < rhs[j]) ++i;
                        else if (rhs[j] < work[i]) ++j;
                        else { tmp.push_back(work[i]); ++i; ++j; }
                    }
                }
            } else if (op == 'O') {""".replace("RATIO", str(ratio))
if source.count(old) != 1:
    raise SystemExit("expected exactly one AND merge")
Path(sys.argv[2]).write_text(source.replace(old, new))
