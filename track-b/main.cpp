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
#include <span>
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

class TokenScanner {
    const char *cursor, *end, *base;
    uint64_t valid = 0;
    static uint32_t classify(const char *load) {
        __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(load));
        __m256i fold = _mm256_or_si256(bytes, _mm256_set1_epi8(32));
        __m256i letters = _mm256_and_si256(
            _mm256_cmpgt_epi8(fold, _mm256_set1_epi8('a' - 1)),
            _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), fold));
        __m256i digits = _mm256_and_si256(
            _mm256_cmpgt_epi8(bytes, _mm256_set1_epi8('0' - 1)),
            _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), bytes));
        return static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_or_si256(letters, digits)));
    }
    void refill() {
        base = cursor;
        size_t count = min<size_t>(end - cursor, 64);
        alignas(32) char tail[64]{};
        const char *load = cursor;
        if (count < 64) {
            memcpy(tail, cursor, count);
            load = tail;
        }
        valid = uint64_t(classify(load)) | (uint64_t(classify(load + 32)) << 32);
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
        if (first + count < 64) {
            valid &= ~((uint64_t(1) << (first + count)) - 1);
            word = string_view(start, count);
            return true;
        }
        do {
            refill();
            count = countr_one(valid);
        } while (count == 64);
        const char *stop = base + count;
        valid &= ~((uint64_t(1) << count) - 1);
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
public:
    // Short ASCII keys, exact letter encodings and CRC fingerprints use disjoint tags.
    struct Slot {
        uint64_t key = 0;
        uint64_t metadata = 0;
        uint32_t id() const { return static_cast<uint32_t>(metadata) & 0xfffffu; }
        uint32_t column() const { return static_cast<uint32_t>(metadata >> 20) & 0xfffffu; }
        int32_t head() const { return static_cast<int32_t>(metadata >> 40) - 1; }
    };
private:
    vector<Slot> slots;
    vector<string_view> keys;

    static pair<uint32_t, uint64_t> fingerprint(string_view word, const char *read_end = nullptr) {
        if (word.size() <= 8) {
            uint64_t key = 0;
            size_t count = word.size();
            const uint64_t mask = ~uint64_t(0) >> (64 - count * 8);
            if (read_end && read_end - word.data() >= 8) memcpy(&key, word.data(), 8);
            else {
                const char *p = word.data();
                unsigned shift = 0;
                if (count & 4) { uint32_t bytes; memcpy(&bytes, p, 4); key = bytes; p += 4; shift = 32; }
                if (count & 2) { uint16_t bytes; memcpy(&bytes, p, 2); key |= uint64_t(bytes) << shift; p += 2; shift += 16; }
                if (count & 1) key |= uint64_t(static_cast<unsigned char>(*p)) << shift;
                if (count == 8) memcpy(&key, word.data(), 8);
            }
            key = (key | 0x2020202020202020ULL) & mask;
            uint32_t hash = static_cast<uint32_t>(_mm_crc32_u64(2166136261u, key));
            return {hash ? hash : 1, key};
        }
        uint64_t first;
        memcpy(&first, word.data(), 8);
        first |= 0x2020202020202020ULL;
        uint64_t last;
        memcpy(&last, word.data() + word.size() - 8, 8);
        last |= 0x2020202020202020ULL;
        // Nonzero five-bit letter codes preserve content and length in 60 bits.
        if (word.size() <= 12 && (first & 0x4040404040404040ULL) == 0x4040404040404040ULL &&
            (last & 0x4040404040404040ULL) == 0x4040404040404040ULL) {
            uint64_t tail = last >> ((16 - word.size()) * 8);
            uint64_t key = 0xc000000000000000ULL | _pext_u64(first, 0x1f1f1f1f1f1f1f1fULL) |
                (_pext_u64(tail, 0x1f1f1f1f1f1f1f1fULL) << 40);
            uint32_t hash = static_cast<uint32_t>(_mm_crc32_u64(2166136261u, key));
            return {hash ? hash : 1, key};
        }
        uint64_t hash64 = _mm_crc32_u64(2166136261u, first);
        // The frozen 32-byte maximum makes these chunks cover every token byte.
        if (word.size() > 16) {
            uint64_t middle;
            memcpy(&middle, word.data() + 8, 8);
            hash64 = _mm_crc32_u64(hash64, middle | 0x2020202020202020ULL);
        }
        if (word.size() > 24) {
            uint64_t middle;
            memcpy(&middle, word.data() + 16, 8);
            hash64 = _mm_crc32_u64(hash64, middle | 0x2020202020202020ULL);
        }
        uint32_t hash = static_cast<uint32_t>(_mm_crc32_u64(hash64, last ^ word.size()));
        hash = hash ? hash : 1;
        return {hash, (uint64_t(1) << 63) | hash};
    }
    void rehash(size_t capacity) {
        vector<Slot> fresh(capacity);
        for (const Slot &slot : slots) {
            if (!slot.key) continue;
            uint32_t hash = (slot.key >> 62) == 2 ? static_cast<uint32_t>(slot.key)
                : static_cast<uint32_t>(_mm_crc32_u64(2166136261u, slot.key));
            if (!hash) hash = 1;
            size_t i = hash & (capacity - 1);
            while (fresh[i].key) i = (i + 1) & (capacity - 1);
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
        const auto [hash, key] = fingerprint(word);
        size_t i = hash & (slots.size() - 1);
        for (;;) {
            Slot &slot = slots[i];
            if (!slot.key) {
                uint32_t id = static_cast<uint32_t>(keys.size());
                keys.push_back(word);
                slot = {key, id};
                return {id, true};
            }
            if (slot.key == key && ((key >> 62) != 2 || FoldEqual{}(keys[slot.id()], word)))
                return {slot.id(), false};
            i = (i + 1) & (slots.size() - 1);
        }
    }
    struct Probe {
        string_view word;
        uint32_t hash;
        uint64_t key;
    };
    Probe prepare(string_view word, const char *read_end = nullptr) const {
        const auto [hash, key] = fingerprint(word, read_end);
        __builtin_prefetch(&slots[hash & (slots.size() - 1)], 0, 3);
        return {word, hash, key};
    }
    const Slot *resolve(const Probe &probe) const {
        if (slots.empty()) return nullptr;
        const auto &[word, hash, key] = probe;
        size_t i = hash & (slots.size() - 1);
        for (;;) {
            const Slot &slot = slots[i];
            if (!slot.key) return nullptr;
            if (slot.key == key && ((key >> 62) != 2 || FoldEqual{}(keys[slot.id()], word)))
                return &slot;
            i = (i + 1) & (slots.size() - 1);
        }
    }
    uint32_t lookup(string_view word, const char *read_end = nullptr) const {
        const Slot *slot = resolve(prepare(word, read_end));
        return slot ? slot->id() : numeric_limits<uint32_t>::max();
    }

    uint64_t phrase_key(uint32_t term) const {
        uint64_t key = fingerprint(keys[term]).second;
        return key | ((key >> 62) == 2 ? uint64_t(term) << 32 : 0);
    }
    bool phrase_equal(uint64_t expected, const Probe &probe) const {
        if ((expected >> 62) != 2) return expected == probe.key;
        return probe.key == ((uint64_t(1) << 63) | static_cast<uint32_t>(expected)) &&
            FoldEqual{}(keys[static_cast<uint32_t>(expected >> 32) & 0x7fffffffu], probe.word);
    }
    void configure_metadata(const vector<uint32_t> &columns, const vector<int32_t> &heads) {
        // The frozen query limit keeps IDs/columns within 20 bits and heads within 24.
        if (keys.size() > (1u << 20)) throw runtime_error("query metadata capacity");
        for (Slot &slot : slots) if (slot.key) {
            uint32_t id = slot.id();
            uint32_t column = columns[id] + 1;
            uint32_t head = static_cast<uint32_t>(heads[id] + 1);
            if (column > 0xfffffu || head > 0xffffffu) throw runtime_error("query metadata capacity");
            slot.metadata = id | (uint64_t(column) << 20) | (uint64_t(head) << 40);
        }
    }

};

struct Query {
    array<uint32_t, 8> terms{};
    array<char, 5> ops{};
    uint8_t term_count = 0, op_count = 0;
    bool phrase = false;
    void add_term(uint32_t term) {
        if (term_count == terms.size()) throw runtime_error("too many query terms");
        terms[term_count++] = term;
    }
    void add_operator(char op) {
        if (op_count == ops.size()) throw runtime_error("too many query operators");
        ops[op_count++] = op;
    }
    span<const uint32_t> words() const { return {terms.data(), term_count}; }
};

class Results {
    static constexpr size_t kept_ids = 10;
    // Frozen N <= 250,000 keeps every result sum below 2^35.
    // Counts and sums can be added together; 50,000 queries fit 16-bit offsets.
    struct __attribute__((packed, aligned(2))) Summary {
        uint64_t total = 0;
        uint16_t first_offset = 0;
        uint32_t count() const { return static_cast<uint32_t>(total >> 35); }
        uint64_t sum() const { return total & ((uint64_t(1) << 35) - 1); }
    };
    vector<Summary> rows;
    vector<array<uint32_t, kept_ids>> first_ids;

    array<uint32_t, kept_ids> &first_for(Summary &row) {
        if (!row.count()) {
            row.first_offset = static_cast<uint32_t>(first_ids.size());
            first_ids.emplace_back();
        }
        return first_ids[row.first_offset];
    }
public:
    void reset(size_t count) {
        rows.assign(count, Summary{});
        first_ids.clear();
        // Capacity is reserved, but only matched queries touch first-ID pages.
        first_ids.reserve(count);
    }
    void add(size_t query, uint32_t id) {
        Summary &row = rows[query];
        if (row.count() < kept_ids) first_for(row)[row.count()] = id;
        row.total += (uint64_t(1) << 35) + id;
    }
    void add_bits(size_t query, uint64_t bits, uint32_t base) {
        if (!bits) return;
        Summary &row = rows[query];
        if (row.count() < kept_ids) {
            auto &first = first_for(row);
            size_t kept = row.count();
            uint64_t remaining = bits;
            while (remaining && kept < kept_ids) {
                first[kept++] = base + countr_zero(remaining);
                remaining &= remaining - 1;
            }
        }
        unsigned count = popcount(bits);
        uint64_t delta = (uint64_t(base) + (uint64_t(1) << 35)) * count;
        delta += popcount(bits & 0xaaaaaaaaaaaaaaaaULL);
        delta += 2u * popcount(bits & 0xccccccccccccccccULL);
        delta += 4u * popcount(bits & 0xf0f0f0f0f0f0f0f0ULL);
        delta += 8u * popcount(bits & 0xff00ff00ff00ff00ULL);
        delta += 16u * popcount(bits & 0xffff0000ffff0000ULL);
        delta += 32u * popcount(bits & 0xffffffff00000000ULL);
        row.total += delta;
    }
    void merge(const Results &source) {
        if (source.rows.empty()) return;
        // Worker ranges are merged in document order.
        for (size_t query = 0; query < rows.size(); ++query) {
            Summary &dst = rows[query];
            const Summary &src = source.rows[query];
            if (!src.count()) continue;
            if (dst.count() < kept_ids) {
                auto &first = first_for(dst);
                size_t kept = dst.count();
                size_t add = min<size_t>(src.count(), kept_ids - kept);
                copy_n(source.first_ids[src.first_offset].begin(), add, first.begin() + kept);
            }
            dst.total += src.total;
        }
    }
    void write(ostream &out) const {
        array<char, 256> line;
        for (const Summary &row : rows) {
            char *p = line.data();
            p = to_chars(p, p + 24, row.count()).ptr;
            *p++ = ' ';
            p = to_chars(p, p + 24, row.sum()).ptr;
            for (size_t i = 0; i < min<size_t>(row.count(), kept_ids); ++i) {
                *p++ = ' ';
                p = to_chars(p, p + 24, first_ids[row.first_offset][i]).ptr;
            }
            *p++ = '\n';
            out.write(line.data(), p - line.data());
        }
    }

};

class Engine {
    FlatDictionary dictionary;
    static constexpr uint32_t singleton = 0x80000000u;
    static constexpr uint32_t doubleton = 0xc0000000u;
    size_t term_count = 0;
    vector<Query> queries;
    struct PhrasePlan {
        uint32_t output = 0;
        int32_t next = -1;
        uint8_t count = 0, anchor = 0;
        array<uint64_t, 8> keys{};
    };
    vector<PhrasePlan> phrases;
    size_t query_count = 0;
    size_t boolean_count = 0;
    struct BooleanQuery {
        array<uint32_t, 6> terms{};
        array<char, 5> ops{};
        uint8_t count = 0;
        uint32_t output = 0;
        uint32_t required = numeric_limits<uint32_t>::max();
    };
    vector<BooleanQuery> boolean_queries;
    Results answers;

    void compile_queries(const MappedFile &input) {
        // Sampling affects anchor choice, never whether a match is accepted.
        vector<uint32_t> frequency(term_count);
        const char *stop = input.data + min<size_t>(input.size, 2u << 20);
        TokenScanner scanner(input.data, stop);
        string_view word;
        while (scanner.next(word)) {
            uint32_t term = dictionary.lookup(word, input.data + input.size);
            if (term != numeric_limits<uint32_t>::max()) ++frequency[term];
        }
        vector<int32_t> anchor_head(term_count, -1);
        query_count = queries.size();
        size_t bool_count = static_cast<size_t>(count_if(queries.begin(), queries.end(), [](const Query &q) { return !q.phrase; }));
        boolean_queries.reserve(bool_count);
        phrases.reserve(query_count - bool_count);
        vector<uint32_t> boolean_column(term_count, numeric_limits<uint32_t>::max());
        for (size_t qi = 0; qi < queries.size(); ++qi) {
            const Query &q = queries[qi];
            if (!q.phrase) {
                for (uint32_t term : q.words())
                    if (boolean_column[term] == numeric_limits<uint32_t>::max()) boolean_column[term] = static_cast<uint32_t>(boolean_count++);
                BooleanQuery plan;
                plan.output = static_cast<uint32_t>(qi);
                plan.count = static_cast<uint8_t>(q.term_count);
                for (size_t i = 0; i < q.term_count; ++i) plan.terms[i] = boolean_column[q.terms[i]];
                plan.ops = q.ops;
                uint32_t mandatory = q.terms[0];
                for (size_t i = 0; i + 1 < q.term_count; ++i) {
                    if (q.ops[i] == 'O') mandatory = numeric_limits<uint32_t>::max();
                    else if (q.ops[i] == 'A' &&
                             (mandatory == numeric_limits<uint32_t>::max() ||
                              frequency[q.terms[i + 1]] < frequency[mandatory]))
                        mandatory = q.terms[i + 1];
                }
                if (mandatory != numeric_limits<uint32_t>::max())
                    plan.required = boolean_column[mandatory];
                boolean_queries.push_back(plan);
                continue;
            }
            size_t best = 0;
            for (size_t i = 1; i < q.term_count; ++i)
                if (frequency[q.terms[i]] < frequency[q.terms[best]]) best = i;
            uint32_t term = q.terms[best];
            PhrasePlan plan;
            for (size_t i = 0; i < q.term_count; ++i) plan.keys[i] = dictionary.phrase_key(q.terms[i]);
            plan.output = static_cast<uint32_t>(qi);
            plan.count = q.term_count;
            plan.anchor = static_cast<uint8_t>(best);
            plan.next = anchor_head[term];
            anchor_head[term] = static_cast<int32_t>(phrases.size());
            phrases.push_back(plan);
        }
        dictionary.configure_metadata(boolean_column, anchor_head);
        vector<Query>().swap(queries);

    }

    void match_document(uint32_t doc, const vector<FlatDictionary::Probe> &probes,
                        span<const pair<uint32_t, int32_t>> anchors,
                        Results &results, vector<uint32_t> &last) const {
        for (const auto &[pos, head] : anchors) {
            for (int32_t pid = head; pid >= 0; pid = phrases[pid].next) {
                if (last[pid] == doc) continue;
                const PhrasePlan &plan = phrases[pid];
                const size_t anchor = plan.anchor, count = plan.count;
                if (pos < anchor || count - anchor > probes.size() - pos) continue;
                size_t start = pos - anchor, j = 0;
                while (j < anchor && dictionary.phrase_equal(plan.keys[j], probes[start + j])) ++j;
                if (j != anchor) continue;
                j = anchor + 1;
                while (j < count && dictionary.phrase_equal(plan.keys[j], probes[start + j])) ++j;
                if (j == count) {
                    last[pid] = doc;
                    results.add(plan.output, doc);
                }
            }
        }
    }

    template<size_t block_docs>
    void evaluate_block(const vector<uint64_t> &matrix, const vector<uint32_t> &active,
                        size_t in_block, uint32_t block_base, Results &results) const {
        static_assert(block_docs == 1024);
        static constexpr size_t words = block_docs / 64;
        static constexpr uint32_t local_value = 0x40000000u;
        static constexpr uint32_t position_mask = 1023u;
        auto is_inline = [&](uint32_t x) { return (x & singleton) != 0; };
        auto visit_inline = [&](uint32_t x, auto &&fn) {
            if ((x & doubleton) == doubleton) {
                fn((x >> 10) & position_mask);
                fn(x & position_mask);
            } else fn(x & position_mask);
        };
        auto pack = [&](uint32_t a, uint32_t b, unsigned n) {
            if (!n) return uint32_t(0);
            if (n == 1) return singleton | a;
            if (b < a) swap(a, b);
            return doubleton | (a << 10) | b;
        };
        for (const BooleanQuery &q : boolean_queries) {
            if (q.required != numeric_limits<uint32_t>::max() && !active[q.required]) continue;
            uint32_t state = active[q.terms[0]];
            array<uint64_t, words> value;
            auto materialize = [&] {
                if (state == local_value) return;
                if (is_inline(state)) {
                    value.fill(0);
                    visit_inline(state, [&](uint32_t pos) {
                        value[pos / 64] |= uint64_t(1) << (pos % 64);
                    });
                } else copy_n(matrix.data() + state * words, words, value.data());
                state = local_value;
            };
            auto present = [&](uint32_t source, uint32_t position) {
                if (source == local_value) return bool((value[position / 64] >> (position % 64)) & 1);
                if (is_inline(source)) {
                    bool hit = false;
                    visit_inline(source, [&](uint32_t pos) { hit |= pos == position; });
                    return hit;
                }
                return bool((matrix[source * words + position / 64] >> (position % 64)) & 1);
            };
            for (size_t i = 0; i + 1 < q.count; ++i) {
                uint32_t other = active[q.terms[i + 1]];
                char op = q.ops[i];
                if (op == 'O') {
                    if (!other || state == other) continue;
                    if (!state) { state = other; continue; }
                    if (is_inline(state) && is_inline(other)) {
                        uint32_t vals[4]{};
                        unsigned n = 0;
                        visit_inline(state, [&](uint32_t pos) { vals[n++] = pos; });
                        visit_inline(other, [&](uint32_t pos) {
                            bool seen = false;
                            for (unsigned j = 0; j < n; ++j) seen |= vals[j] == pos;
                            if (!seen) vals[n++] = pos;
                        });
                        if (n <= 2) { state = pack(vals[0], vals[1], n); continue; }
                    }
                    materialize();
                    if (is_inline(other)) {
                        visit_inline(other, [&](uint32_t pos) {
                            value[pos / 64] |= uint64_t(1) << (pos % 64);
                        });
                    } else {
                        const uint64_t *rhs = matrix.data() + other * words;
                        for (size_t w = 0; w < words; ++w) value[w] |= rhs[w];
                    }
                    continue;
                }
                if (!state) continue;
                if (!other) { if (op == 'A') state = 0; continue; }
                if (is_inline(state) || (op == 'A' && is_inline(other))) {
                    uint32_t source = is_inline(state) ? state : other;
                    uint32_t target = is_inline(state) ? other : state;
                    uint32_t vals[2]{};
                    unsigned n = 0;
                    visit_inline(source, [&](uint32_t pos) {
                        bool hit = present(target, pos);
                        if ((op == 'A' && hit) || (op == 'N' && !hit)) vals[n++] = pos;
                    });
                    state = pack(vals[0], vals[1], n);
                } else if (is_inline(other)) {
                    materialize();
                    visit_inline(other, [&](uint32_t pos) {
                        value[pos / 64] &= ~(uint64_t(1) << (pos % 64));
                    });
                } else {
                    materialize();
                    const uint64_t *rhs = matrix.data() + other * words;
                    if (op == 'A') for (size_t w = 0; w < words; ++w) value[w] &= rhs[w];
                    else for (size_t w = 0; w < words; ++w) value[w] &= ~rhs[w];
                }
            }
            if (is_inline(state)) {
                visit_inline(state, [&](uint32_t pos) { results.add(q.output, block_base + pos); });
            } else if (state) {
                const uint64_t *data = state == local_value ? value.data() : matrix.data() + state * words;
                for (size_t w = 0; w < (in_block + 63) / 64; ++w)
                    results.add_bits(q.output, data[w], block_base + static_cast<uint32_t>(w * 64));
            }
        }
    }

    template<size_t block_docs>
    void scan_corpus(const MappedFile &input, size_t workers) {
        static constexpr size_t words = block_docs / 64;
        struct Chunk {
            Results results;
            uint32_t first_doc = 0, last_doc = 0;
            size_t docs = 0;
        };
        const char *begin = input.data, *end = begin + input.size;
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
                c.results.reset(query_count);
                vector<uint64_t> matrix(words);
                matrix.reserve(words * (boolean_count + 1));
                vector<uint32_t> active(boolean_count);
                uint32_t block_base = 0;
                size_t in_block = 0;
                auto flush = [&] {
                    evaluate_block<block_docs>(matrix, active, in_block, block_base, c.results);
                    matrix.resize(words);
                    fill(active.begin(), active.end(), 0);
                    in_block = 0;
                };
                vector<uint32_t> last(phrases.size(), numeric_limits<uint32_t>::max());
                vector<pair<uint32_t, int32_t>> anchors;
                anchors.reserve(128);
                vector<FlatDictionary::Probe> probes;
                probes.reserve(1024);
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
                    size_t anchor_count = 0;
                    probes.clear();
                    TokenScanner scanner(tab + 1, line_end);
                    string_view word;
                    while (scanner.next(word)) probes.push_back(dictionary.prepare(word, end));
                    const uint64_t document_bit = uint64_t(1) << (in_block % 64);
                    const size_t document_word = in_block / 64;
                    if (anchors.size() < probes.size()) anchors.resize(probes.size());
                    for (size_t index = 0; index < probes.size(); ++index) {
                        const auto &probe = probes[index];
                        const auto *entry = dictionary.resolve(probe);
                        if (entry) {
                            int32_t head = entry->head();
                            if (head >= 0) anchors[anchor_count++] = {static_cast<uint32_t>(index), head};
                            uint32_t column = entry->column();
                            if (column) {
                                --column;
                                uint32_t &row = active[column];
                                if (!row) {
                                    row = singleton | static_cast<uint32_t>(in_block);
                                } else if ((row & doubleton) == singleton) {
                                    uint32_t previous = row & ~singleton;
                                    if (previous != in_block)
                                        row = doubleton | (previous << 10) | static_cast<uint32_t>(in_block);
                                } else if ((row & doubleton) == doubleton) {
                                    uint32_t first = (row >> 10) & 1023u;
                                    uint32_t second = row & 1023u;
                                    if (first != in_block && second != in_block) {
                                        row = static_cast<uint32_t>(matrix.size() / words);
                                        matrix.resize(matrix.size() + words, 0);
                                        matrix[row * words + first / 64] |= uint64_t(1) << (first % 64);
                                        matrix[row * words + second / 64] |= uint64_t(1) << (second % 64);
                                        matrix[row * words + document_word] |= document_bit;
                                    }
                                } else matrix[row * words + document_word] |= document_bit;
                            }
                        }
                    }
                    match_document(doc, probes, {anchors.data(), anchor_count}, c.results, last);
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
        answers = move(chunks[0].results);
        uint32_t expected_doc = 0;
        for (const Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc) throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            answers.merge(c.results);
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
        queries.reserve(static_cast<size_t>(count(input.data, input.data + input.size, '\n')));
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
                    if (term) q.add_term(intern(word));
                    else q.add_operator(word[0]); // A, O, N are unique
                    term = !term;
                    start = stop + 1;
                }
            } else if (line.starts_with("PHRASE \"") && line.size() >= 9 && line.back() == '"') {
                q.phrase = true;
                size_t start = 8, last = line.size() - 1;
                while (start < last) {
                    size_t stop = line.find(' ', start);
                    if (stop == string_view::npos || stop > last) stop = last;
                    q.add_term(intern(line.substr(start, stop - start)));
                    start = stop + 1;
                }
            } else throw runtime_error("invalid query line");
            if (q.term_count == 0 || (!q.phrase && q.op_count + 1 != q.term_count))
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
        const size_t workers = docs_file.size >= (8u << 20) ? 6 : 1;
        scan_corpus<1024>(docs_file, workers);
        filesystem::create_directories(out_dir);
        ofstream output(out_dir + "/results.txt", ios::binary);
        if (!output) throw runtime_error("cannot open output");
        answers.write(output);
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
