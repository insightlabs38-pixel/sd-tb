#!/usr/bin/env python3
"""Deterministic synthetic Track B performance stress, not the organizer generator."""
import argparse
import random
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--out", required=True)
p.add_argument("--docs", type=int, default=25000)
p.add_argument("--queries", type=int, default=5000)
p.add_argument("--seed", type=int, default=123)
a = p.parse_args()
rng = random.Random(a.seed)
folder = Path(a.out)
folder.mkdir(parents=True, exist_ok=True)
vocab = ["the", "of", "and", "to", "in", "a", "not", "bail", "bond", "writ",
         "habeas", "corpus"] + [f"w{i}" for i in range(12000)]
with (folder / "docs.txt").open("w") as f:
    for doc in range(a.docs):
        n = rng.randint(40, 250)
        words = [vocab[int((rng.random() ** 4) * len(vocab))] for _ in range(n)]
        f.write(str(doc) + "\t" + " ".join(words) + "\n")
with (folder / "queries.txt").open("w") as f:
    for q in range(a.queries):
        if q % 20 < 12:
            words = [vocab[int((rng.random() ** 2) * len(vocab))]
                     for _ in range(rng.randint(1, 5))]
            expr = words[0]
            for word in words[1:]:
                expr += " " + rng.choice(["AND", "OR", "NOT"]) + " " + word
            f.write("BOOL " + expr + "\n")
        else:
            words = [vocab[int((rng.random() ** 2) * len(vocab))]
                     for _ in range(rng.randint(2, 5))]
            if q % 20 == 19: words = ["the", "of"]
            f.write('PHRASE "' + " ".join(words) + '"\n')
