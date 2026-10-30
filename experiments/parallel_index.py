#!/usr/bin/env python3
"""Generate a parallel corpus-indexing challenger from the verified Track B engine."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
if source.count("#include <thread>") or source.count("#include <exception>"):
    raise SystemExit("Expected serial baseline")
source = source.replace("#include <queue>\n", "#include <queue>\n#include <thread>\n#include <exception>\n", 1)
start = source.index("    void build_index(const MappedFile &input) {")
end = source.index("    bool contains_phrase(", start)
replacement = r'''    void build_index(const MappedFile &input) {
        struct Chunk {
            vector<uint32_t> tape;
            vector<uint32_t> offsets;
            vector<vector<uint32_t>> postings;
            vector<uint32_t> seen;
            uint32_t first_doc = 0, last_doc = 0;
            size_t docs = 0;
        };
        const char *begin = input.data, *end = begin + input.size;
        const unsigned cores = max(1u, thread::hardware_concurrency());
        const size_t workers = input.size >= (8u << 20) ? min<size_t>(4, cores) : 1;
        vector<const char *> bounds(workers + 1);
        bounds[0] = begin;
        bounds[workers] = end;
        for (size_t i = 1; i < workers; ++i) {
            const char *split = begin + (input.size / workers) * i;
            const char *newline = static_cast<const char *>(memchr(split, '\n', end - split));
            bounds[i] = newline ? newline + 1 : end;
        }
        vector<Chunk> chunks(workers);
        vector<exception_ptr> errors(workers);
        auto parse_chunk = [&](size_t idx) {
            try {
                Chunk &c = chunks[idx];
                c.postings.resize(postings.size());
                c.seen.assign(postings.size(), numeric_limits<uint32_t>::max());
                const char *p = bounds[idx], *stop = bounds[idx + 1];
                c.tape.reserve(static_cast<size_t>(stop - p) / 7);
                c.offsets.reserve(static_cast<size_t>(stop - p) / 500);
                while (p < stop) {
                    c.offsets.push_back(static_cast<uint32_t>(c.tape.size()));
                    const char *line_end = static_cast<const char *>(memchr(p, '\n', stop - p));
                    if (!line_end) line_end = stop;
                    const char *tab = static_cast<const char *>(memchr(p, '\t', line_end - p));
                    if (!tab) throw runtime_error("missing document tab");
                    uint32_t doc = 0;
                    auto [ptr, ec] = from_chars(p, tab, doc);
                    if (ec != errc() || ptr != tab) throw runtime_error("invalid document id");
                    if (!c.docs) c.first_doc = doc;
                    else if (doc != c.last_doc + 1) throw runtime_error("nonconsecutive document id");
                    c.last_doc = doc;
                    ++c.docs;
                    const char *s = tab + 1;
                    while (s < line_end) {
                        while (s < line_end && !token_char(static_cast<unsigned char>(*s))) ++s;
                        if (s == line_end) break;
                        const char *start = s;
                        do { ++s; } while (s < line_end && token_char(static_cast<unsigned char>(*s)));
                        uint32_t term = dictionary.lookup(string_view(start, s - start));
                        uint32_t id = term == numeric_limits<uint32_t>::max() ? 0 : term + 1;
                        c.tape.push_back(id);
                        if (id && c.seen[id - 1] != doc) {
                            c.seen[id - 1] = doc;
                            c.postings[id - 1].push_back(doc);
                        }
                    }
                    p = line_end < stop ? line_end + 1 : stop;
                }
                c.offsets.push_back(static_cast<uint32_t>(c.tape.size()));
            } catch (...) {
                errors[idx] = current_exception();
            }
        };
        vector<thread> threads;
        threads.reserve(workers - 1);
        for (size_t i = 1; i < workers; ++i) threads.emplace_back(parse_chunk, i);
        parse_chunk(0);
        for (thread &t : threads) t.join();
        for (const exception_ptr &e : errors) if (e) rethrow_exception(e);

        size_t total_tokens = 0, total_docs = 0;
        for (const Chunk &c : chunks) {
            total_tokens += c.tape.size();
            total_docs += c.docs;
        }
        tape.reserve(total_tokens);
        offsets.reserve(total_docs + 1);
        uint32_t expected_doc = 0;
        for (const Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc)
                throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            const uint32_t base = static_cast<uint32_t>(tape.size());
            for (size_t j = 0; j < c.docs; ++j)
                offsets.push_back(base + c.offsets[j]);
            tape.insert(tape.end(), c.tape.begin(), c.tape.end());
        }
        offsets.push_back(static_cast<uint32_t>(tape.size()));
        for (size_t term = 0; term < postings.size(); ++term) {
            size_t total = 0;
            for (const Chunk &c : chunks) total += c.postings[term].size();
            postings[term].reserve(total);
            for (const Chunk &c : chunks) {
                const auto &part = c.postings[term];
                postings[term].insert(postings[term].end(), part.begin(), part.end());
            }
        }
    }

'''
source = source[:start] + replacement + source[end:]
Path(sys.argv[2]).write_text(source)
