#!/usr/bin/env python3
"""Randomized shape fuzz for the window tokenizer, tail padding and block seeking."""
import random
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import differential as d

binary = sys.argv[1]
ALPHA = "abcdefghijklmnopqrstuvwxyz0123456789"
SEPS = [b" ", b"  ", b". ", b"-", b"\xc3\xa9", b"\xe2\x80\x94", b"'", b",", b"\x01", b"/"]

def word(rng):
    length = rng.choice([1, 2, 3, 7, 8, 9, 11, 12, 13, 16, 17, 24, 31, 32, 33, 40])
    text = "".join(rng.choice(ALPHA[:26] if rng.random() < .8 else ALPHA) for _ in range(length))
    return text.upper().encode() if rng.random() < .15 else text.encode()

def make_docs(rng, count, vocab, lengths):
    docs = []
    for _ in range(count):
        target = rng.choice(lengths)
        parts, size = [], 0
        while size < target:
            w = rng.choice(vocab) if rng.random() < .85 else word(rng)
            sep = rng.choice(SEPS)
            parts.append(w + sep)
            size += len(w) + len(sep)
        docs.append(b"".join(parts)[:target] if rng.random() < .5 else b"".join(parts))
    return docs

def make_queries(rng, vocab, count):
    names = [w.lower() for w in vocab] + [b"absent", b"and", b"or", b"not"]
    queries = []
    for _ in range(count):
        if rng.random() < .6:
            parts = [rng.choice(names)]
            for _ in range(rng.randint(0, 5)):
                parts += [rng.choice([b"AND", b"OR", b"NOT"]), rng.choice(names)]
            queries.append(b"BOOL " + b" ".join(parts))
        else:
            queries.append(b'PHRASE "' + b" ".join(rng.choice(names) for _ in range(rng.randint(2, 6))) + b'"')
    return queries

def run_case(rng, docs, queries, trailing_newline=True):
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "data").mkdir()
        (root / "out").mkdir()
        body = b"\n".join(str(i).encode() + b"\t" + doc for i, doc in enumerate(docs))
        (root / "data/docs.txt").write_bytes(body + (b"\n" if trailing_newline else b""))
        (root / "data/queries.txt").write_bytes(b"\n".join(queries) + (b"\n" if trailing_newline else b""))
        import subprocess
        subprocess.run([binary, "--data", str(root / "data"), "--out", str(root / "out")], check=True, timeout=120)
        return (root / "out/results.txt").read_bytes()

for seed in range(1, 25):
    rng = random.Random(seed * 7919)
    vocab = [word(rng) for _ in range(rng.choice([5, 30, 200]))]
    count = rng.choice([1, 2, 63, 64, 65, 300, 2047, 2048, 2049, 4097])
    lengths = rng.choice([[0, 1, 5, 63, 64, 65, 127, 128, 200], [10, 90, 300], [64, 128, 192, 256]])
    docs = make_docs(rng, count, vocab, lengths)
    queries = make_queries(rng, vocab, 150)
    newline = rng.random() < .8
    # Documents never contain tabs or newlines by construction; ensure the oracle sees the same text.
    got = run_case(rng, docs, queries, newline)
    want = d.oracle(docs, queries)
    assert got == want, f"seed {seed}: mismatch (docs={count}, trailing newline={newline})"
    print(f"seed {seed}: docs={count} PASS")
print("Randomized window/tail/block fuzz: PASS")
