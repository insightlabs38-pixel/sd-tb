#!/usr/bin/env python3
"""Compare query-dictionary table footprint against lookup probe cost."""
from pathlib import Path
import sys

if len(sys.argv) != 4 or sys.argv[3] not in ("40000", "80000"):
    raise SystemExit("usage: dictionary_capacity.py SOURCE OUTPUT 40000|80000")
source = Path(sys.argv[1]).read_text()
old = "        dictionary.reserve(160000);"
if source.count(old) != 1:
    raise SystemExit("dictionary reserve marker changed")
Path(sys.argv[2]).write_text(source.replace(old, f"        dictionary.reserve({sys.argv[3]});"))
