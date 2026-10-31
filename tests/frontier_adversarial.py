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

prefix, suffix = b"abcdefgh", b"ijklmnop"
words = [prefix + middle * 16 + suffix for middle in (b"a", b"b", b"c")]
docs = [words[0], words[1], words[2], b" ".join(words), words[1].upper()]
queries = [b"BOOL " + word for word in words]
queries += [b"BOOL " + words[0] + b" NOT " + words[1]]
queries += [b'PHRASE "' + words[0] + b" " + words[1] + b'"']
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Distinct long tokens with identical hash endpoints: PASS")

words = [(b"abc123xyz" * 4)[:size] for size in range(1, 33)]
docs = [b"." * pad + word + b" END" for word in words for pad in range(64)]
queries = [b"BOOL " + word for word in words]
queries += [b'PHRASE "' + word + b' end"' for word in words]
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("All token lengths and SIMD boundary offsets: PASS")

words = [
    (f"t{index:07x}" if index % 2 else f"longprefix{index:08x}").encode()
    for index in range(338000)
]
queries = []
position = 0
for _ in range(31000):
    queries.append(b"BOOL " + b" OR ".join(words[position : position + 6]))
    position += 6
for _ in range(19000):
    queries.append(b'PHRASE "' + b" ".join(words[position : position + 8]) + b'"')
    position += 8
assert position == len(words)
docs = [
    b" ".join(words[:8]),
    b" ".join(words[-16:]),
    b" ".join(words[91744:91760]).upper(),
    b" ".join(words[183496:183512]),
] + [b""] * 996
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("338,000 distinct terms across dictionary growth and mixed key lengths: PASS")

seen = {}
pair = None
for _ in range(300000):
    word = bytes(rng.randrange(97, 123) for _ in range(24))
    encoded = word[:16] + bytes([word[16] ^ 24]) + word[17:]
    fingerprint = 2166136261
    for value in encoded:
        fingerprint = table[(fingerprint ^ value) & 255] ^ (fingerprint >> 8)
    fingerprint = fingerprint or 1
    if fingerprint in seen and seen[fingerprint] != word:
        pair = (seen[fingerprint], word)
        break
    seen[fingerprint] = word
assert pair
x, y = pair
docs = [x, y, x + b" " + y, x.upper(), y.upper()]
queries = [b"BOOL " + x, b"BOOL " + y, b"BOOL " + x + b" NOT " + y]
queries += [b'PHRASE "' + x + b" " + y + b'"']
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Long-token CRC32C collision with full-byte identity checks: PASS")
