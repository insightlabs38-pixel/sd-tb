#!/usr/bin/env python3
"""Generate a parallel common-phrase automaton challenger."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
old = """        vector<uint32_t> last(pattern_results.size(), numeric_limits<uint32_t>::max());
        for (uint32_t doc = 0; doc + 1 < offsets.size(); ++doc) {
            uint32_t state = 0;
            for (uint32_t i = offsets[doc]; i < offsets[doc + 1]; ++i) {
                uint32_t tok = tape[i];
                while (state) {
                    auto edge = nodes[state].edges.find(tok);
                    if (edge != nodes[state].edges.end()) {
                        state = edge->second;
                        break;
                    }
                    state = nodes[state].fail;
                }
                if (!state) state = root_edges[tok];
                for (uint32_t pid : nodes[state].outputs) {
                    if (last[pid] != doc) {
                        last[pid] = doc;
                        pattern_results[pid].add(doc);
                    }
                }
            }
        }"""
new = """        const size_t doc_count = offsets.size() - 1;
        const size_t workers = doc_count >= 1000
            ? min<size_t>(4, max(1u, thread::hardware_concurrency())) : 1;
        vector<vector<Result>> partial(workers, vector<Result>(pattern_results.size()));
        vector<exception_ptr> errors(workers);
        auto scan = [&](size_t worker) {
            try {
                vector<uint32_t> last(pattern_results.size(), numeric_limits<uint32_t>::max());
                const size_t first_doc = doc_count * worker / workers;
                const size_t last_doc = doc_count * (worker + 1) / workers;
                for (size_t d = first_doc; d < last_doc; ++d) {
                    const uint32_t doc = static_cast<uint32_t>(d);
                    uint32_t state = 0;
                    for (uint32_t i = offsets[doc]; i < offsets[doc + 1]; ++i) {
                        uint32_t tok = tape[i];
                        while (state) {
                            auto edge = nodes[state].edges.find(tok);
                            if (edge != nodes[state].edges.end()) {
                                state = edge->second;
                                break;
                            }
                            state = nodes[state].fail;
                        }
                        if (!state) state = root_edges[tok];
                        for (uint32_t pid : nodes[state].outputs) {
                            if (last[pid] != doc) {
                                last[pid] = doc;
                                partial[worker][pid].add(doc);
                            }
                        }
                    }
                }
            } catch (...) {
                errors[worker] = current_exception();
            }
        };
        vector<thread> threads;
        threads.reserve(workers - 1);
        for (size_t i = 1; i < workers; ++i) threads.emplace_back(scan, i);
        scan(0);
        for (thread &t : threads) t.join();
        for (const exception_ptr &e : errors) if (e) rethrow_exception(e);
        for (size_t pid = 0; pid < pattern_results.size(); ++pid) {
            Result &dst = pattern_results[pid];
            for (size_t worker = 0; worker < workers; ++worker) {
                const Result &src = partial[worker][pid];
                const size_t kept = min<size_t>(dst.count, dst.first.size());
                const size_t add = min<size_t>(src.count, dst.first.size() - kept);
                for (size_t i = 0; i < add; ++i) dst.first[kept + i] = src.first[i];
                dst.count += src.count;
                dst.sum += src.sum;
            }
        }"""
if source.count(old) != 1:
    raise SystemExit("Expected exactly one AC scan loop")
source = source.replace(old, new)
Path(sys.argv[2]).write_text(source)
