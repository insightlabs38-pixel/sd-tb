#!/usr/bin/env python3
"""Compare branchless ASCII equality for legal query and document tokens."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: fold_equal.py SOURCE OUTPUT")
source = Path(sys.argv[1]).read_text()
old = ("            if (lower_ascii(static_cast<unsigned char>(a[i])) != "
       "lower_ascii(static_cast<unsigned char>(b[i]))) return false;")
new = ("            if ((static_cast<unsigned char>(a[i]) | 0x20u) != "
       "(static_cast<unsigned char>(b[i]) | 0x20u)) return false;")
if source.count(old) != 1:
    raise SystemExit("FoldEqual marker changed")
Path(sys.argv[2]).write_text(source.replace(old, new))
