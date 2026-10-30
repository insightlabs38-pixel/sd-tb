#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <thread>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

struct MappedFile {
    int fd = -1;
    size_t size = 0;
    const char *data = nullptr;
    explicit MappedFile(const string &path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) throw runtime_error("cannot open " + path);
        struct stat st{};
        if (fstat(fd, &st)) throw runtime_error("fstat failed");
        size = static_cast<size_t>(st.st_size);
        if (size) {
            void *p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p == MAP_FAILED) throw runtime_error("mmap failed");
            data = static_cast<const char *>(p);
        }
    }
    ~MappedFile() { if (data) munmap(const_cast<char *>(data), size); if (fd >= 0) close(fd); }
    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;
};

static inline unsigned char lower_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}
static inline bool token_char(unsigned char c) {
    const unsigned folded = static_cast<unsigned>(c | 0x20u);
    return ((folded - 'a') < 26u) | ((static_cast<unsigned>(c) - '0') < 10u);
}
struct FoldEqual {
    using is_transparent = void;
    bool operator()(string_view a, string_view b) const noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if ((static_cast<unsigned char>(a[i]) | 0x20u) != (static_cast<unsigned char>(b[i]) | 0x20u)) return false;
        return true;
    }
};

class FlatDictionary {
    struct Slot {
        uint32_t hash = 0;
        uint32_t id = 0;
    };
    vector<Slot> slots;
    vector<string_view> keys;

    static uint32_t fingerprint(string_view word) {
        uint32_t hash = 2166136261u;
        for (unsigned char c : word) {
            hash ^= lower_ascii(c);
            hash *= 16777619u;
        }
        return hash ? hash : 1;
    }
    void rehash(size_t capacity) {
        vector<Slot> fresh(capacity);
        for (const Slot &slot : slots) {
            if (!slot.hash) continue;
            size_t i = slot.hash & (capacity - 1);
            while (fresh[i].hash) i = (i + 1) & (capacity - 1);
            fresh[i] = slot;
        }
        slots.swap(fresh);
    }
public:
    void reserve(size_t count) {
        keys.reserve(count);
        size_t capacity = 16;
        while (capacity < count * 2) capacity *= 2;
        rehash(capacity);
    }
    pair<uint32_t, bool> intern(string_view word) {
        if (slots.empty()) rehash(16);
        if ((keys.size() + 1) * 10 >= slots.size() * 7)
            rehash(slots.size() * 2);
        const uint32_t hash = fingerprint(word);
        size_t i = hash & (slots.size() - 1);
        for (;;) {
            Slot &slot = slots[i];
            if (!slot.hash) {
                uint32_t id = static_cast<uint32_t>(keys.size());
                keys.push_back(word);
                slot = {hash, id};
                return {id, true};
            }
            if (slot.hash == hash && FoldEqual{}(keys[slot.id], word))
                return {slot.id, false};
            i = (i + 1) & (slots.size() - 1);
        }
    }
    uint32_t lookup(string_view word, uint32_t known_hash = 0) const {
        if (slots.empty()) return numeric_limits<uint32_t>::max();
        const uint32_t hash = known_hash ? known_hash : fingerprint(word);
        size_t i = hash & (slots.size() - 1);
        for (;;) {
            const Slot &slot = slots[i];
            if (!slot.hash) return numeric_limits<uint32_t>::max();
            if (slot.hash == hash && FoldEqual{}(keys[slot.id], word))
                return slot.id;
            i = (i + 1) & (slots.size() - 1);
        }
    }
};

struct Query {
    bool phrase = false;
    vector<uint32_t> terms;
    vector<char> ops;
};
struct Result {
    uint32_t count = 0;
    uint64_t sum = 0;
    array<uint32_t, 10> first{};
    void add(uint32_t id) {
        if (count < first.size()) first[count] = id;
        ++count;
        sum += id;
    }
    void print(ostream &os) const {
        os << count << ' ' << sum;
        for (size_t i = 0; i < min<size_t>(count, first.size()); ++i) os << ' ' << first[i];
        os << '\n';
    }
};

class Engine {
    FlatDictionary dictionary;
    vector<vector<uint32_t>> postings;
    vector<uint32_t> seen;
    vector<uint32_t> tape;
    vector<uint16_t> tape16;
    vector<uint32_t> phrase_token;
    bool compact_tape = false;
    vector<uint32_t> offsets;
    vector<Query> queries;
    vector<int32_t> precomputed_pattern;
    vector<Result> pattern_results;

    uint32_t tape_at(uint32_t i) const {
        return compact_tape ? tape16[i] : tape[i];
    }
    uint32_t phrase_id(uint32_t term) const {
        return compact_tape ? phrase_token[term] : term + 1;
    }

    struct ACNode {
        unordered_map<uint32_t, uint32_t> edges;
        uint32_t fail = 0;
        vector<uint32_t> outputs;
    };

    void precompute_common_phrases() {
        const size_t threshold = 500;
        precomputed_pattern.assign(queries.size(), -1);
        vector<ACNode> nodes(1);
        unordered_map<string, uint32_t> dedup;
        for (size_t qi = 0; qi < queries.size(); ++qi) {
            const Query &q = queries[qi];
            if (!q.phrase) continue;
            size_t least = numeric_limits<size_t>::max();
            for (uint32_t t : q.terms) least = min(least, postings[t].size());
            if (least < threshold) continue;
            string key;
            key.reserve(q.terms.size() * sizeof(uint32_t));
            for (uint32_t t : q.terms)
                key.append(reinterpret_cast<const char *>(&t), sizeof(t));
            auto it = dedup.find(key);
            if (it != dedup.end()) {
                precomputed_pattern[qi] = static_cast<int32_t>(it->second);
                continue;
            }
            uint32_t pid = static_cast<uint32_t>(pattern_results.size());
            dedup.emplace(move(key), pid);
            pattern_results.emplace_back();
            precomputed_pattern[qi] = static_cast<int32_t>(pid);
            uint32_t state = 0;
            for (uint32_t t : q.terms) {
                const uint32_t tok = phrase_id(t);
                auto edge = nodes[state].edges.find(tok);
                if (edge == nodes[state].edges.end()) {
                    uint32_t next = static_cast<uint32_t>(nodes.size());
                    nodes[state].edges.emplace(tok, next);
                    nodes.emplace_back();
                    state = next;
                } else state = edge->second;
            }
            nodes[state].outputs.push_back(pid);
        }
        if (pattern_results.empty()) return;
        vector<uint32_t> root_edges(postings.size() + 1, 0);
        queue<uint32_t> bfs;
        for (const auto &[tok, next] : nodes[0].edges) {
            root_edges[tok] = next;
            nodes[next].fail = 0;
            bfs.push(next);
        }
        while (!bfs.empty()) {
            uint32_t state = bfs.front(); bfs.pop();
            for (const auto &[tok, next] : nodes[state].edges) {
                uint32_t f = nodes[state].fail;
                while (f && !nodes[f].edges.contains(tok)) f = nodes[f].fail;
                auto it = nodes[f].edges.find(tok);
                nodes[next].fail = it == nodes[f].edges.end() ? 0 : it->second;
                const auto &suffix = nodes[nodes[next].fail].outputs;
                nodes[next].outputs.insert(nodes[next].outputs.end(), suffix.begin(), suffix.end());
                bfs.push(next);
            }
        }
        const size_t doc_count = offsets.size() - 1;
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
                        uint32_t tok = tape_at(i);
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
        }
    }


    uint32_t intern(string_view word) {
        auto [id, fresh] = dictionary.intern(word);
        if (fresh) {
            postings.emplace_back();
            seen.push_back(numeric_limits<uint32_t>::max());
        }
        return id;
    }

    void parse_queries(const MappedFile &input) {
        const char *p = input.data, *end = p + input.size;
        dictionary.reserve(40000);
        while (p < end) {
            const char *line_end = static_cast<const char *>(memchr(p, '\n', end - p));
            if (!line_end) line_end = end;
            string_view line(p, line_end - p);
            Query q;
            if (line.starts_with("BOOL ")) {
                size_t start = 5;
                bool term = true;
                while (start < line.size()) {
                    size_t stop = line.find(' ', start);
                    if (stop == string_view::npos) stop = line.size();
                    string_view word = line.substr(start, stop - start);
                    if (term) q.terms.push_back(intern(word));
                    else q.ops.push_back(word[0]); // A, O, N are unique
                    term = !term;
                    start = stop + 1;
                }
            } else if (line.starts_with("PHRASE \"") && line.size() >= 9 && line.back() == '"') {
                q.phrase = true;
                size_t start = 8, last = line.size() - 1;
                while (start < last) {
                    size_t stop = line.find(' ', start);
                    if (stop == string_view::npos || stop > last) stop = last;
                    q.terms.push_back(intern(line.substr(start, stop - start)));
                    start = stop + 1;
                }
            } else throw runtime_error("invalid query line");
            if (q.terms.empty() || (!q.phrase && q.ops.size() + 1 != q.terms.size()))
                throw runtime_error("invalid query grammar");
            queries.push_back(move(q));
            p = line_end < end ? line_end + 1 : end;
        }
    }

    void configure_tape() {
        phrase_token.assign(postings.size(), 0);
        uint32_t next = 0;
        for (const Query &q : queries) {
            if (!q.phrase) continue;
            for (uint32_t term : q.terms)
                if (!phrase_token[term]) phrase_token[term] = ++next;
        }
        compact_tape = next <= numeric_limits<uint16_t>::max();
        fprintf(stderr, "PHRASE_TAPE16=%d PHRASE_TERMS=%u QUERY_TERMS=%zu\n",
                int(compact_tape), next, postings.size());
        if (!compact_tape) phrase_token.clear();
    }

    void build_index(const MappedFile &input) {
        struct Chunk {
            vector<uint32_t> tape;
            vector<uint16_t> tape16;
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
                if (compact_tape) c.tape16.reserve(static_cast<size_t>(stop - p) / 7);
                else c.tape.reserve(static_cast<size_t>(stop - p) / 7);
                c.offsets.reserve(static_cast<size_t>(stop - p) / 500);
                while (p < stop) {
                    c.offsets.push_back(static_cast<uint32_t>(compact_tape ? c.tape16.size() : c.tape.size()));
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
                        uint32_t hash = 2166136261u;
                        do {
                            const unsigned char c = static_cast<unsigned char>(*s);
                            hash ^= static_cast<unsigned char>(c | 0x20u);
                            hash *= 16777619u;
                            ++s;
                        } while (s < line_end && token_char(static_cast<unsigned char>(*s)));
                        if (!hash) hash = 1;
                        uint32_t term = dictionary.lookup(string_view(start, s - start), hash);
                        uint32_t id = term == numeric_limits<uint32_t>::max() ? 0 : term + 1;
                        if (compact_tape)
                            c.tape16.push_back(id ? static_cast<uint16_t>(phrase_token[id - 1]) : 0);
                        else c.tape.push_back(id);
                        if (id && c.seen[id - 1] != doc) {
                            c.seen[id - 1] = doc;
                            c.postings[id - 1].push_back(doc);
                        }
                    }
                    p = line_end < stop ? line_end + 1 : stop;
                }
                c.offsets.push_back(static_cast<uint32_t>(compact_tape ? c.tape16.size() : c.tape.size()));
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
            total_tokens += compact_tape ? c.tape16.size() : c.tape.size();
            total_docs += c.docs;
        }
        if (compact_tape) tape16.reserve(total_tokens);
        else tape.reserve(total_tokens);
        offsets.reserve(total_docs + 1);
        uint32_t expected_doc = 0;
        for (const Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc)
                throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            const uint32_t base = static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size());
            for (size_t j = 0; j < c.docs; ++j)
                offsets.push_back(base + c.offsets[j]);
            if (compact_tape) tape16.insert(tape16.end(), c.tape16.begin(), c.tape16.end());
            else tape.insert(tape.end(), c.tape.begin(), c.tape.end());
        }
        offsets.push_back(static_cast<uint32_t>(compact_tape ? tape16.size() : tape.size()));
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

    bool contains_phrase(uint32_t doc, const vector<uint32_t> &terms,
                         size_t anchor) const {
        const uint32_t begin = offsets[doc], end = offsets[doc + 1];
        const size_t k = terms.size();
        if (end - begin < k) return false;
        const uint32_t key = phrase_id(terms[anchor]);
        for (uint32_t i = begin + anchor; i + k - anchor <= end; ++i) {
            if (tape_at(i) != key) continue;
            const uint32_t start = i - static_cast<uint32_t>(anchor);
            size_t j = 0;
            while (j < k && tape_at(start + j) == phrase_id(terms[j])) ++j;
            if (j == k) return true;
        }
        return false;
    }

    Result run_phrase(const Query &q) const {
        size_t best = 0;
        for (size_t i = 1; i < q.terms.size(); ++i)
            if (postings[q.terms[i]].size() < postings[q.terms[best]].size()) best = i;
        Result r;
        for (uint32_t doc : postings[q.terms[best]])
            if (contains_phrase(doc, q.terms, best)) r.add(doc);
        return r;
    }

    Result run_bool(const Query &q, vector<uint32_t> &work, vector<uint32_t> &tmp) const {
        const vector<uint32_t> &initial = postings[q.terms[0]];
        if (q.ops.empty()) {
            Result r;
            for (uint32_t id : initial) r.add(id);
            return r;
        }
        // A final AND/NOT suffix can only remove documents from its AND terms.
        size_t last_or = q.ops.size();
        for (size_t i = 0; i < q.ops.size(); ++i)
            if (q.ops[i] == 'O') last_or = i;
        size_t anchor = last_or == q.ops.size() ? 0 : q.terms.size();
        for (size_t i = 0; i < q.ops.size(); ++i) {
            if (q.ops[i] != 'A' || (last_or != q.ops.size() && i <= last_or))
                continue;
            if (anchor == q.terms.size() ||
                postings[q.terms[i + 1]].size() < postings[q.terms[anchor]].size())
                anchor = i + 1;
        }
        if (anchor != q.terms.size()) {
            const auto &candidates = postings[q.terms[anchor]];
            uint64_t merge_work = initial.size();
            for (size_t i = 1; i < q.terms.size(); ++i)
                merge_work += postings[q.terms[i]].size();
            if (candidates.empty() ||
                (candidates.size() <= 8192 &&
                 candidates.size() * (q.terms.size() * 12) < merge_work)) {
                Result result;
                for (uint32_t doc : candidates) {
                    bool match = anchor == 0 ||
                        binary_search(initial.begin(), initial.end(), doc);
                    if (!match && last_or == q.ops.size()) continue;
                    for (size_t step = 0; step < q.ops.size(); ++step) {
                        const auto &rhs = postings[q.terms[step + 1]];
                        bool present = step + 1 == anchor ||
                            binary_search(rhs.begin(), rhs.end(), doc);
                        switch (q.ops[step]) {
                            case 'A': match = match && present; break;
                            case 'O': match = match || present; break;
                            case 'N': match = match && !present; break;
                            default: throw runtime_error("invalid boolean operator");
                        }
                        if (!match && (last_or == q.ops.size() || step >= last_or))
                            break;
                    }
                    if (match) result.add(doc);
                }
                return result;
            }
        }
        work.assign(initial.begin(), initial.end());
        for (size_t step = 0; step < q.ops.size(); ++step) {
            const vector<uint32_t> &rhs = postings[q.terms[step + 1]];
            char op = q.ops[step];
            tmp.clear();
            if (op == 'A') {
                if (work.size() * 16 < rhs.size()) {
                    for (uint32_t doc : work)
                        if (binary_search(rhs.begin(), rhs.end(), doc))
                            tmp.push_back(doc);
                } else if (rhs.size() * 16 < work.size()) {
                    for (uint32_t doc : rhs)
                        if (binary_search(work.begin(), work.end(), doc))
                            tmp.push_back(doc);
                } else {
                    size_t i = 0, j = 0;
                    while (i < work.size() && j < rhs.size()) {
                        if (work[i] < rhs[j]) ++i;
                        else if (rhs[j] < work[i]) ++j;
                        else { tmp.push_back(work[i]); ++i; ++j; }
                    }
                }
            } else if (op == 'O') {
                size_t i = 0, j = 0;
                while (i < work.size() && j < rhs.size()) {
                    if (work[i] < rhs[j]) tmp.push_back(work[i++]);
                    else if (rhs[j] < work[i]) tmp.push_back(rhs[j++]);
                    else { tmp.push_back(work[i]); ++i; ++j; }
                }
                tmp.insert(tmp.end(), work.begin() + i, work.end());
                tmp.insert(tmp.end(), rhs.begin() + j, rhs.end());
            } else if (op == 'N') {
                size_t i = 0, j = 0;
                while (i < work.size() && j < rhs.size()) {
                    if (work[i] < rhs[j]) tmp.push_back(work[i++]);
                    else if (rhs[j] < work[i]) ++j;
                    else { ++i; ++j; }
                }
                tmp.insert(tmp.end(), work.begin() + i, work.end());
            } else throw runtime_error("invalid boolean operator");
            work.swap(tmp);
        }
        Result r;
        for (uint32_t id : work) r.add(id);
        return r;
    }

public:
    void run(const string &data_dir, const string &out_dir) {
        MappedFile query_file(data_dir + "/queries.txt");
        parse_queries(query_file);
        configure_tape();
        MappedFile docs_file(data_dir + "/docs.txt");
        build_index(docs_file);
        precompute_common_phrases();
        filesystem::create_directories(out_dir);
        ofstream output(out_dir + "/results.txt", ios::binary);
        if (!output) throw runtime_error("cannot open output");
        vector<Result> results(queries.size());
        const size_t workers = queries.size() >= 1000
            ? min<size_t>(4, max(1u, thread::hardware_concurrency())) : 1;
        vector<exception_ptr> errors(workers);
        atomic<size_t> next_query{0};
        auto evaluate = [&](size_t worker) {
            try {
                vector<uint32_t> work, tmp;
                for (;;) {
                    const size_t first = next_query.fetch_add(64, memory_order_relaxed);
                    if (first >= queries.size()) break;
                    const size_t last = min(first + 64, queries.size());
                    for (size_t qi = first; qi < last; ++qi) {
                    const Query &q = queries[qi];
                    results[qi] = precomputed_pattern[qi] >= 0
                        ? pattern_results[precomputed_pattern[qi]]
                        : (q.phrase ? run_phrase(q) : run_bool(q, work, tmp));
                    }
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
        for (const Result &r : results) r.print(output);
        if (!output) throw runtime_error("output write failure");
    }
};

int main(int argc, char **argv) {
    string data_dir = "/data", out_dir = "/out";
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--data") == 0) data_dir = argv[i + 1];
        else if (strcmp(argv[i], "--out") == 0) out_dir = argv[i + 1];
        else { cerr << "unknown argument: " << argv[i] << '\n'; return 2; }
    }
    try { Engine().run(data_dir, out_dir); }
    catch (const exception &e) { cerr << e.what() << '\n'; return 1; }
    return 0;
}
