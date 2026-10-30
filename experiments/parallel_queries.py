#!/usr/bin/env python3
"""Generate an independently parallelized query-evaluation challenger."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
old = """        vector<uint32_t> work, tmp;
        for (size_t qi = 0; qi < queries.size(); ++qi) {
            const Query &q = queries[qi];
            Result r = precomputed_pattern[qi] >= 0
                ? pattern_results[precomputed_pattern[qi]]
                : (q.phrase ? run_phrase(q) : run_bool(q, work, tmp));
            r.print(output);
        }"""
new = """        vector<Result> results(queries.size());
        const size_t workers = queries.size() >= 1000
            ? min<size_t>(4, max(1u, thread::hardware_concurrency())) : 1;
        vector<exception_ptr> errors(workers);
        auto evaluate = [&](size_t worker) {
            try {
                vector<uint32_t> work, tmp;
                const size_t first = queries.size() * worker / workers;
                const size_t last = queries.size() * (worker + 1) / workers;
                for (size_t qi = first; qi < last; ++qi) {
                    const Query &q = queries[qi];
                    results[qi] = precomputed_pattern[qi] >= 0
                        ? pattern_results[precomputed_pattern[qi]]
                        : (q.phrase ? run_phrase(q) : run_bool(q, work, tmp));
                }
            } catch (...) {
                errors[worker] = current_exception();
            }
        };
        vector<thread> threads;
        threads.reserve(workers - 1);
        for (size_t i = 1; i < workers; ++i) threads.emplace_back(evaluate, i);
        evaluate(0);
        for (thread &t : threads) t.join();
        for (const exception_ptr &e : errors) if (e) rethrow_exception(e);
        for (const Result &r : results) r.print(output);"""
if source.count(old) != 1:
    raise SystemExit("Expected exactly one serial query loop")
source = source.replace(old, new)
Path(sys.argv[2]).write_text(source)
