import importlib.util
import itertools
from pathlib import Path
import random
import sys

spec = importlib.util.spec_from_file_location(
    "diff", Path(__file__).with_name("differential.py")
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

def letter_term(index):
    word = bytearray(b"a" * 12)
    for position in range(11, 3, -1):
        word[position] += index % 26
        index //= 26
    return bytes(word)

words = [
    (letter_term(index) if index % 3 == 2 else
     (f"t{index:07x}" if index % 3 == 0 else f"longprefix{index:08x}").encode())
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

docs = [
    b"." * pad + b"a" + bytes([byte]) + b"b"
    for byte in range(256)
    if byte not in (9, 10, 13)
    for pad in range(32)
]
queries = [b"BOOL a", b"BOOL b", b'PHRASE "a b"']
queries += [
    b"BOOL a" + bytes([byte]).lower() + b"b"
    for byte in range(128)
    if 48 <= byte <= 57 or 65 <= byte <= 90 or 97 <= byte <= 122
]
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Exhaustive byte classification at every SIMD alignment: PASS")

docs = [
    x + b" " + x,
    y + b" " + y,
    y + b" " + x,
    x + b" " + y,
    x.upper() + b" " + y.upper(),
]
queries = [
    b'PHRASE "' + a + b" " + b + b'"' for a, b in ((x, y), (y, x), (x, x), (y, y))
]
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Colliding long tokens preserve phrase order and repetition: PASS")

count = 16417
edges = {0, count - 1}
for size in (768, 1024, 1536, 2048):
    for boundary in range(size, count, size):
        edges.update(range(boundary - 2, min(count, boundary + 3)))
for workers in (4, 6, 8, 12):
    for chunk in range(1, workers):
        boundary = count * chunk // workers
        edges.update(range(boundary - 2, boundary + 3))
docs = [
    b"common " + b"filler " * 80 + (b"edge edge" if index in edges else b"plain")
    for index in range(count)
]
queries = [
    b"BOOL common", b"BOOL edge", b"BOOL edge AND common",
    b"BOOL edge NOT common", b"BOOL unknown OR edge AND common",
    b"BOOL common NOT edge OR absent", b'PHRASE "edge edge"',
    b'PHRASE "common edge"', b'PHRASE "edge common"',
]
assert sum(map(len, docs)) > 8 << 20
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("4/6/8/12-worker and 768/1024/1536/2048-document boundaries: PASS")

vocabulary = set()
for length in range(9, 14):
    base = bytearray(b"abcdefghijklmn"[:length])
    vocabulary.add(bytes(base))
    for position in range(length):
        for letter in b"abmqz07":
            word = base.copy()
            word[position] = letter
            vocabulary.add(bytes(word))
words = sorted(vocabulary)
docs = [word + b" end" for word in words]
docs += [word.upper() + b" END" for word in words]
queries = [b"BOOL " + word for word in words]
queries += [b'PHRASE "' + word + b' end"' for word in words]
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Packed 9–12-letter keys, prefixes, digits and case folding: PASS")

positions = [
    [0, 63, 64, 2047, 2048, 4094, 4095],
    [1, 65, 127, 2046, 2050, 4000, 4093],
    [0, 65, 64, 2046, 2048, 4000, 4095],
]
docwords = [[b"base"] for _ in range(8195)]
for block in (0, 4096, 8192):
    for group, selected in enumerate(positions):
        for size in range(1, 8):
            term = f"r{group}n{size}".encode()
            for position in selected[:size]:
                if block + position < len(docwords):
                    docwords[block + position].extend([term] * 3)
docs = [b" ".join(words) for words in docwords]
queries = []
for size in range(1, 8):
    terms = [f"r{group}n{size}".encode() for group in range(3)]
    terms += [terms[0], b"absent", b"base"]
    for ops in itertools.product((b"AND", b"OR", b"NOT"), repeat=5):
        parts = [terms[0]]
        for op, term in zip(ops, terms[1:]):
            parts.extend((op, term))
        queries.append(b"BOOL " + b" ".join(parts))
assert d.run(binary, docs, queries) == d.oracle(docs, queries)
print("Inline-row counts 1–7, union overflow, repeated tokens and 4096-document boundaries: PASS")
