#!/usr/bin/env python3
"""Compare compact linear AC transitions against unordered-map edges."""
from pathlib import Path
import sys

src = Path(sys.argv[1]).read_text()
def change(old, new):
    global src
    if src.count(old) != 1:
        raise SystemExit("AC source marker changed: " + old[:55])
    src = src.replace(old, new)

change(
    "        unordered_map<uint32_t, uint32_t> edges;\n",
    "        vector<pair<uint32_t, uint32_t>> edges;\n"
)
change(
    "    void precompute_common_phrases() {",
    """    static uint32_t find_edge(const vector<ACNode> &nodes, uint32_t state,
                              uint32_t token) {
        for (const auto &[key, next] : nodes[state].edges)
            if (key == token) return next;
        return 0;
    }

    void precompute_common_phrases() {"""
)
change(
    """                auto edge = nodes[state].edges.find(tok);
                if (edge == nodes[state].edges.end()) {
                    uint32_t next = static_cast<uint32_t>(nodes.size());
                    nodes[state].edges.emplace(tok, next);
                    nodes.emplace_back();
                    state = next;
                } else state = edge->second;""",
    """                uint32_t next = find_edge(nodes, state, tok);
                if (!next) {
                    next = static_cast<uint32_t>(nodes.size());
                    nodes[state].edges.emplace_back(tok, next);
                    nodes.emplace_back();
                }
                state = next;"""
)
change(
    """                while (f && !nodes[f].edges.contains(tok)) f = nodes[f].fail;
                auto it = nodes[f].edges.find(tok);
                nodes[next].fail = it == nodes[f].edges.end() ? 0 : it->second;""",
    """                while (f && !find_edge(nodes, f, tok)) f = nodes[f].fail;
                nodes[next].fail = find_edge(nodes, f, tok);"""
)
change(
    """                            auto edge = nodes[state].edges.find(tok);
                            if (edge != nodes[state].edges.end()) {
                                state = edge->second;
                                break;
                            }""",
    """                            const uint32_t next = find_edge(nodes, state, tok);
                            if (next) {
                                state = next;
                                break;
                            }"""
)
Path(sys.argv[2]).write_text(src)
