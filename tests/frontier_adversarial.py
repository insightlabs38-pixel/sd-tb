import importlib.util
import itertools
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    "diff", str(Path(__file__).with_name("differential.py"))
)
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)
binary = sys.argv[1]
docs = [
    b" ".join(("t" + str(i)).encode() for i in range(6) if mask >> i & 1)
    for mask in range(64)
]
queries = []
for n in range(2, 7):
    for ops in itertools.product((b"AND", b"OR", b"NOT"), repeat=n - 1):
        parts = [b"t0"]
        for i, op in enumerate(ops):
            parts.extend((op, ("t" + str(i + 1)).encode()))
        queries.append(b"BOOL " + b" ".join(parts))
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Exhaustive left-associative Boolean truth assignments: PASS")
rng = random.Random(818)
vocab = [
    b"alpha",
    b"beta",
    b"gamma",
    b"0",
    b"007",
    b"7",
    b"and",
    b"or",
    b"not",
    b"x" * 32,
]
docs = []
for doc in range(4101):
    words = [rng.choice(vocab) for _ in range(400)]
    if doc % 1024 == 1023:
        words[-2:] = [b"alpha", b"beta"]
    if doc % 1024 == 0:
        words[:2] = [b"beta", b"gamma"]
    sep = rng.choice([b" ", b"\xc2\xa0", b"\xe2\x80\x94", b"'", b"\xc4\xb0"])
    text = sep.join(words)
    if doc % 43 == 0:
        text = text.upper()
    if doc % 73 == 0:
        text = b""
    docs.append(text)
queries = [
    b'PHRASE "alpha beta gamma"',
    b'PHRASE "alpha beta"',
    b'PHRASE "beta gamma"',
    b'PHRASE "' + b"x" * 32 + b" " + b"x" * 32 + b'"',
]
for _ in range(120):
    terms = rng.choices(vocab, k=rng.randint(1, 6))
    parts = [terms[0]]
    for t in terms[1:]:
        parts.extend((rng.choice([b"AND", b"OR", b"NOT"]), t))
    queries.append(b"BOOL " + b" ".join(parts))
for _ in range(120):
    queries.append(
        b'PHRASE "' + b" ".join(rng.choices(vocab, k=rng.randint(2, 8))) + b'"'
    )
assert sum(map(len, docs)) > 8 << 20
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Parallel chunk/block boundaries, UTF-8, 32-byte tokens and partial blocks: PASS")
# CRC32C collisions with the production seed and no final xor.
table = []
for x in range(256):
    for _ in range(8):
        x = (x >> 1) ^ (0x82F63B78 if x & 1 else 0)
    table.append(x)
seen = {}
pair = None
for _ in range(250000):
    word = bytes(rng.randrange(97, 123) for _ in range(8))
    h = 2166136261
    for c in word:
        h = table[(h ^ c) & 255] ^ (h >> 8)
    if h in seen and seen[h] != word:
        pair = (seen[h], word)
        break
    seen[h] = word
assert pair
x, y = pair
docs = [x, y, x + b" " + y, x.upper(), y.upper()]
queries = [
    b"BOOL " + x,
    b"BOOL " + y,
    b"BOOL " + x + b" NOT " + y,
    b'PHRASE "' + x + b" " + y + b'"',
]
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("CRC32C collision with distinct full tokens: PASS")
