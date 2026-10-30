#!/usr/bin/env python3
"""Build an AC root-transition challenger without changing the verified champion."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
old_root = """        queue<uint32_t> bfs;
        for (const auto &[tok, next] : nodes[0].edges) {
            nodes[next].fail = 0;
            bfs.push(next);
        }"""
new_root = """        vector<uint32_t> root_edges(postings.size() + 1, 0);
        queue<uint32_t> bfs;
        for (const auto &[tok, next] : nodes[0].edges) {
            root_edges[tok] = next;
            nodes[next].fail = 0;
            bfs.push(next);
        }"""
old_scan = """                uint32_t tok = tape[i];
                while (state && !nodes[state].edges.contains(tok)) state = nodes[state].fail;
                auto edge = nodes[state].edges.find(tok);
                state = edge == nodes[state].edges.end() ? 0 : edge->second;"""
new_scan = """                uint32_t tok = tape[i];
                while (state) {
                    auto edge = nodes[state].edges.find(tok);
                    if (edge != nodes[state].edges.end()) {
                        state = edge->second;
                        break;
                    }
                    state = nodes[state].fail;
                }
                if (!state) state = root_edges[tok];"""
for before, after in ((old_root, new_root), (old_scan, new_scan)):
    if source.count(before) != 1:
        raise SystemExit("Expected exactly one source anchor")
    source = source.replace(before, after)
Path(sys.argv[2]).write_text(source)
