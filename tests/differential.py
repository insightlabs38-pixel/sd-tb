#!/usr/bin/env python3
"""Independent differential oracle for the Track B binary."""
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

def oracle(docs, queries):
    tokens = [re.findall(rb"[a-z0-9]+", text.lower()) for text in docs]
    postings = {}
    for i, words in enumerate(tokens):
        for word in set(words):
            postings.setdefault(word, set()).add(i)
    lines = []
    for query in queries:
        kind, _, tail = query.partition(b" ")
        if kind == b"BOOL":
            parts = tail.split(b" ")
            result = postings.get(parts[0], set()).copy()
            for op, word in zip(parts[1::2], parts[2::2]):
                other = postings.get(word, set())
                if op == b"AND": result &= other
                elif op == b"OR": result |= other
                elif op == b"NOT": result -= other
                else: raise AssertionError(op)
        else:
            phrase = tail[1:-1].split(b" ")
            result = {i for i, words in enumerate(tokens)
                      if any(words[j:j + len(phrase)] == phrase
                             for j in range(len(words) - len(phrase) + 1))}
        ids = sorted(result)
        lines.append(b" ".join(str(x).encode() for x in
                              [len(ids), sum(ids), *ids[:10]]))
    return b"\n".join(lines) + b"\n"

def run(binary, docs, queries):
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "data").mkdir()
        (root / "out").mkdir()
        (root / "data/docs.txt").write_bytes(
            b"".join(str(i).encode() + b"\t" + d + b"\n"
                     for i, d in enumerate(docs)))
        (root / "data/queries.txt").write_bytes(b"\n".join(queries) + b"\n")
        subprocess.run([binary, "--data", str(root / "data"),
                        "--out", str(root / "out")], check=True, timeout=45)
        return (root / "out/results.txt").read_bytes()

def test(binary):
    docs = [b"The surety posted bail; the BAIL BOND was forfeited.",
            b"Writ of Habeas-Corpus: see 42 U.S.C. \xc2\xa7 1983 (bail bond).",
            b"", b"Z\xc3\xbcrich bail\xe2\x80\x94bond, bail bail bond. Case 007.",
            b"In the matter of the estate. Bond not posted, No. 7."]
    queries = [b"BOOL bail AND bond", b"BOOL bail",
               b"BOOL estate OR surety AND bail", b"BOOL bond NOT bail",
               b"BOOL bail NOT bail", b"BOOL zurich",
               b"BOOL rich AND z", b"BOOL not AND bond", b"BOOL 7",
               b"BOOL the OR writ OR case", b'PHRASE "bail bond"',
               b'PHRASE "bail bail bond"', b'PHRASE "bond bail"',
               b'PHRASE "habeas corpus"', b'PHRASE "u s c 1983"',
               b'PHRASE "estate bond"', b'PHRASE "forfeited writ"',
               b'PHRASE "in the matter of"']
    expected = b"""3 4 0 1 3
3 4 0 1 3
1 0 0
1 4 4
0 0
0 0
1 3 3
1 4 4
1 4 4
4 8 0 1 3 4
3 4 0 1 3
1 3 3
1 3 3
1 1 1
1 1 1
1 4 4
0 0
1 4 4
"""
    assert oracle(docs, queries) == expected
    assert run(binary, docs, queries) == expected
    print("Organizer worked example: PASS")
    vocab = [b"the", b"of", b"and", b"or", b"not", b"bail", b"bond",
             b"007", b"7", b"z", b"rich", b"s", b"habeas", b"corpus", b"absent"]
    separators = [b" ", b". ", b"-", b"/", b"'", b"\xe2\x80\x94", b"\xc2\xa0"]
    for seed in (1, 2, 3, 42, 123, 2026, 99991):
        rng = random.Random(seed)
        docs = []
        for _ in range(240):
            words = [rng.choice(vocab[:-1])
                     for _ in range(rng.choice([0, 1, 2, 5, 20, 80, 200]))]
            if rng.random() < .2: words = [w.upper() for w in words]
            docs.append(b"".join(w + (rng.choice(separators) if j + 1 < len(words) else b"")
                                 for j, w in enumerate(words)))
        queries = []
        for _ in range(1200):
            if rng.random() < .65:
                parts = [rng.choice(vocab)]
                for _ in range(rng.randint(0, 5)):
                    parts += [rng.choice([b"AND", b"OR", b"NOT"]), rng.choice(vocab)]
                queries.append(b"BOOL " + b" ".join(parts))
            else:
                words = [rng.choice(vocab) for _ in range(rng.randint(2, 8))]
                queries.append(b'PHRASE "' + b" ".join(words) + b'"')
        got = run(binary, docs, queries)
        want = oracle(docs, queries)
        assert got == want, f"seed {seed}: byte mismatch"
        print(f"seed {seed}: PASS")
    print("All differential checks passed")

if __name__ == "__main__":
    test(sys.argv[1] if len(sys.argv) > 1 else "./solution")
