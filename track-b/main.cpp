#include <algorithm>
#include <bit>
#include <immintrin.h>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
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

static inline bool token_char(unsigned char c) {
    const unsigned folded = static_cast<unsigned>(c | 0x20u);
    return ((folded - 'a') < 26u) | ((static_cast<unsigned>(c) - '0') < 10u);
}
class TokenScanner {
    const char *cursor, *end, *base;
    uint32_t valid = 0;
    void refill() {
        base = cursor;
        size_t count = min<size_t>(end - cursor, 32);
        alignas(32) char tail[32]{};
        const char *load = cursor;
        if (count < 32) {
            memcpy(tail, cursor, count);
            load = tail;
        }
        __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(load));
        __m256i fold = _mm256_or_si256(bytes, _mm256_set1_epi8(32));
        __m256i letters = _mm256_and_si256(_mm256_cmpgt_epi8(fold, _mm256_set1_epi8('a' - 1)), _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), fold));
        __m256i digits = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, _mm256_set1_epi8('0' - 1)), _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), bytes));
        valid = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_or_si256(letters, digits)));
        cursor += count;
    }
public:
    TokenScanner(const char *begin, const char *last) : cursor(begin), end(last), base(begin) {}
    bool next(string_view &word) {
        while (!valid) {
            if (cursor == end) return false;
            refill();
        }
        unsigned first = countr_zero(valid);
        const char *start = base + first;
        unsigned count = countr_one(valid >> first);
        if (first + count < 32) {
            valid &= ~((uint32_t(1) << (first + count)) - 1);
            word = string_view(start, count);
            return true;
        }
        do {
            refill();
            count = countr_one(valid);
        } while (count == 32);
        const char *stop = base + count;
        valid &= ~((uint32_t(1) << count) - 1);
        word = string_view(start, stop - start);
        return true;
    }
};

struct FoldEqual {
    using is_transparent = void;
    bool operator()(string_view a, string_view b) const noexcept {
        if (a.size() != b.size()) return false;
        const char *x = a.data(), *y = b.data();
        size_t count = a.size();
        while (count >= 8) {
            uint64_t left, right;
            memcpy(&left, x, 8);
            memcpy(&right, y, 8);
            if ((left | 0x2020202020202020ULL) != (right | 0x2020202020202020ULL)) return false;
            x += 8; y += 8; count -= 8;
        }
        if (count & 4) {
            uint32_t left, right;
            memcpy(&left, x, 4); memcpy(&right, y, 4);
            if ((left | 0x20202020u) != (right | 0x20202020u)) return false;
            x += 4; y += 4;
        }
        if (count & 2) {
            uint16_t left, right;
            memcpy(&left, x, 2); memcpy(&right, y, 2);
            if ((left | 0x2020u) != (right | 0x2020u)) return false;
            x += 2; y += 2;
        }
        if ((count & 1) && ((*x | 32) != (*y | 32))) return false;
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
        uint64_t hash = 2166136261u;
        const char *p = word.data();
        size_t size = word.size();
        while (size >= 8) {
            uint64_t bytes;
            memcpy(&bytes, p, 8);
            hash = _mm_crc32_u64(hash, bytes | 0x2020202020202020ULL);
            p += 8;
            size -= 8;
        }
        if (size & 4) {
            uint32_t bytes;
            memcpy(&bytes, p, 4);
            hash = _mm_crc32_u32(static_cast<uint32_t>(hash), bytes | 0x20202020u);
            p += 4;
        }
        if (size & 2) {
            uint16_t bytes;
            memcpy(&bytes, p, 2);
            hash = _mm_crc32_u16(static_cast<uint32_t>(hash), bytes | 0x2020u);
            p += 2;
        }
        if (size & 1) hash = _mm_crc32_u8(static_cast<uint32_t>(hash), static_cast<unsigned char>(*p) | 0x20u);
        return hash ? static_cast<uint32_t>(hash) : 1;
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
    static constexpr size_t block_docs = 1024, words = block_docs / 64;
    size_t term_count = 0;
    vector<Query> queries;
    vector<int32_t> anchor_head, anchor_next;
    vector<uint32_t> anchor_position;
    vector<uint32_t> boolean_column;
    size_t boolean_count = 0;
    struct BooleanQuery {
        array<uint32_t, 6> terms{};
        array<char, 5> ops{};
        uint8_t count = 0;
        uint32_t output = 0;
    };
    vector<BooleanQuery> boolean_queries;
    vector<Result> answers;

    void compile_queries(const MappedFile &input) {
        // Sampling affects anchor choice, never whether a match is accepted.
        vector<uint32_t> frequency(term_count);
        const char *p = input.data;
        const char *stop = p + min<size_t>(input.size, 2u << 20);
        while (p < stop) {
            while (p < stop && !token_char(static_cast<unsigned char>(*p))) ++p;
            const char *start = p;
            while (p < stop && token_char(static_cast<unsigned char>(*p))) ++p;
            if (start == p) continue;
            uint32_t term = dictionary.lookup(string_view(start, p - start));
            if (term != numeric_limits<uint32_t>::max()) ++frequency[term];
        }
        anchor_head.assign(term_count, -1);
        anchor_next.assign(queries.size(), -1);
        anchor_position.resize(queries.size());
        boolean_column.assign(term_count, numeric_limits<uint32_t>::max());
        answers.resize(queries.size());
        for (size_t qi = 0; qi < queries.size(); ++qi) {
            const Query &q = queries[qi];
            if (!q.phrase) {
                for (uint32_t term : q.terms)
                    if (boolean_column[term] == numeric_limits<uint32_t>::max()) boolean_column[term] = static_cast<uint32_t>(boolean_count++);
                BooleanQuery plan;
                plan.output = static_cast<uint32_t>(qi);
                plan.count = static_cast<uint8_t>(q.terms.size());
                for (size_t i = 0; i < q.terms.size(); ++i) plan.terms[i] = boolean_column[q.terms[i]];
                copy(q.ops.begin(), q.ops.end(), plan.ops.begin());
                boolean_queries.push_back(plan);
                continue;
            }
            size_t best = 0;
            for (size_t i = 1; i < q.terms.size(); ++i)
                if (frequency[q.terms[i]] < frequency[q.terms[best]]) best = i;
            anchor_position[qi] = static_cast<uint32_t>(best);
            uint32_t term = q.terms[best];
            anchor_next[qi] = anchor_head[term];
            anchor_head[term] = static_cast<int32_t>(qi);
        }

    }

    void match_document(uint32_t doc, const vector<uint32_t> &tokens,
                        vector<Result> &results, vector<uint32_t> &last) const {
        for (size_t pos = 0; pos < tokens.size(); ++pos) {
            if (!tokens[pos]) continue;
            for (int32_t qi = anchor_head[tokens[pos] - 1]; qi >= 0; qi = anchor_next[qi]) {
                if (last[qi] == doc) continue;
                const auto &terms = queries[qi].terms;
                const size_t anchor = anchor_position[qi];
                if (pos < anchor || terms.size() - anchor > tokens.size() - pos) continue;
                size_t j = 0;
                while (j < terms.size() && tokens[pos - anchor + j] == terms[j] + 1) ++j;
                if (j == terms.size()) {
                    last[qi] = doc;
                    results[qi].add(doc);
                }
            }
        }
    }

    void evaluate_block(const vector<uint64_t> &matrix, const vector<unsigned char> &active,
                        size_t in_block, uint32_t block_base, vector<Result> &results) const {
        for (const BooleanQuery &q : boolean_queries) {
            const size_t qi = q.output;
            bool possible = active[q.terms[0]] != 0;
            for (size_t i = 0; i < size_t(q.count - 1); ++i) {
                if (q.ops[i] == 'A') possible &= active[q.terms[i + 1]] != 0;
                else if (q.ops[i] == 'O') possible |= active[q.terms[i + 1]] != 0;
            }
            if (!possible) continue;
            array<uint64_t, words> value;
            copy_n(matrix.data() + q.terms[0] * words, words, value.data());
            for (size_t i = 0; i < size_t(q.count - 1); ++i) {
                const uint64_t *rhs = matrix.data() + q.terms[i + 1] * words;
                if (q.ops[i] == 'A') for (size_t w = 0; w < words; ++w) value[w] &= rhs[w];
                else if (q.ops[i] == 'O') for (size_t w = 0; w < words; ++w) value[w] |= rhs[w];
                else for (size_t w = 0; w < words; ++w) value[w] &= ~rhs[w];
            }
            for (size_t word = 0; word < (in_block + 63) / 64; ++word) {
                uint64_t bits = value[word];
                if (!bits) continue;
                Result &r = results[qi];
                uint32_t base = block_base + static_cast<uint32_t>(word * 64);
                uint64_t first = bits;
                size_t kept = min<size_t>(r.count, r.first.size());
                while (first && kept < r.first.size()) {
                    r.first[kept++] = base + countr_zero(first);
                    first &= first - 1;
                }
                unsigned count = popcount(bits);
                r.count += count;
                r.sum += uint64_t(base) * count;
                r.sum += popcount(bits & 0xaaaaaaaaaaaaaaaaULL);
                r.sum += 2u * popcount(bits & 0xccccccccccccccccULL);
                r.sum += 4u * popcount(bits & 0xf0f0f0f0f0f0f0f0ULL);
                r.sum += 8u * popcount(bits & 0xff00ff00ff00ff00ULL);
                r.sum += 16u * popcount(bits & 0xffff0000ffff0000ULL);
                r.sum += 32u * popcount(bits & 0xffffffff00000000ULL);
            }
        }
    }

    void scan_corpus(const MappedFile &input) {
        struct Chunk {
            vector<Result> results;
            uint32_t first_doc = 0, last_doc = 0;
            size_t docs = 0;
        };
        const char *begin = input.data, *end = begin + input.size;
        const size_t workers = input.size >= (8u << 20)
            ? min<size_t>(4, max(1u, thread::hardware_concurrency())) : 1;
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
                c.results.resize(queries.size());
                vector<uint64_t> matrix(boolean_count * words);
                vector<unsigned char> active(boolean_count);
                uint32_t block_base = 0;
                size_t in_block = 0;
                auto flush = [&] {
                    evaluate_block(matrix, active, in_block, block_base, c.results);
                    fill(matrix.begin(), matrix.end(), 0);
                    fill(active.begin(), active.end(), 0);
                    in_block = 0;
                };
                vector<uint32_t> last(queries.size(), numeric_limits<uint32_t>::max());
                vector<uint32_t> tokens;
                tokens.reserve(1024);
                const char *p = bounds[idx], *stop = bounds[idx + 1];
                while (p < stop) {
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
                    if (!in_block) block_base = doc;
                    tokens.clear();
                    TokenScanner scanner(tab + 1, line_end);
                    string_view word;
                    while (scanner.next(word)) {
                        uint32_t term = dictionary.lookup(word);
                        uint32_t id = term == numeric_limits<uint32_t>::max() ? 0 : term + 1;
                        tokens.push_back(id);
                        if (id) {
                            uint32_t column = boolean_column[term];
                            if (column != numeric_limits<uint32_t>::max()) {
                                matrix[column * words + in_block / 64] |= uint64_t(1) << (in_block % 64);
                                active[column] = 1;
                            }
                        }
                    }
                    match_document(doc, tokens, c.results, last);
                    p = line_end < stop ? line_end + 1 : stop;
                    if (++in_block == block_docs) flush();
                }
                if (in_block) flush();
            } catch (...) { errors[idx] = current_exception(); }
        };
        vector<thread> threads;
        for (size_t i = 1; i < workers; ++i) threads.emplace_back(parse_chunk, i);
        parse_chunk(0);
        for (thread &t : threads) t.join();
        for (const exception_ptr &e : errors) if (e) rethrow_exception(e);
        uint32_t expected_doc = 0;
        for (const Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc) throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            for (size_t qi = 0; qi < queries.size(); ++qi) {
                Result &dst = answers[qi];
                const Result &src = c.results[qi];
                size_t kept = min<size_t>(dst.count, dst.first.size());
                size_t add = min<size_t>(src.count, dst.first.size() - kept);
                for (size_t i = 0; i < add; ++i) dst.first[kept + i] = src.first[i];
                dst.count += src.count;
                dst.sum += src.sum;
            }
        }

    }

    uint32_t intern(string_view word) {
        auto [id, fresh] = dictionary.intern(word);
        if (fresh) ++term_count;
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

public:
    void run(const string &data_dir, const string &out_dir) {
        MappedFile query_file(data_dir + "/queries.txt");
        parse_queries(query_file);
        MappedFile docs_file(data_dir + "/docs.txt");
        compile_queries(docs_file);
        scan_corpus(docs_file);
        filesystem::create_directories(out_dir);
        ofstream output(out_dir + "/results.txt", ios::binary);
        if (!output) throw runtime_error("cannot open output");
        for (const Result &r : answers) r.print(output);
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
