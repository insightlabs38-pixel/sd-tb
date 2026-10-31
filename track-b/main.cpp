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
    struct Probe {
        string_view word;
        uint32_t hash;
        uint64_t key;
    };
private:
    // Metadata word: (term ID + 1) in bits 0-19, Boolean column in 20-39, phrase head + 1 above.
    // Four-way buckets hold exact keys; a full bucket spills into the next one.
    struct alignas(64) Bucket { uint64_t keys[4]; uint64_t meta[4]; };
    vector<Bucket> buckets;
    size_t bucket_mask = 0;
    vector<string_view> keys;
    vector<uint64_t> fingerprints;

    // Fingerprint of a token longer than eight bytes; shorter tokens are keyed by prepare_line.
    static pair<uint32_t, uint64_t> fingerprint(string_view word) {
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
    static uint32_t hash_of(uint64_t key) {
        uint32_t hash = (key >> 62) == 2 ? static_cast<uint32_t>(key) : static_cast<uint32_t>(_mm_crc32_u64(2166136261u, key));
        return hash ? hash : 1;
    }
    static unsigned match_mask(const Bucket &bucket, uint64_t key) {
        __m256i eq = _mm256_cmpeq_epi64(_mm256_load_si256(reinterpret_cast<const __m256i *>(bucket.keys)), _mm256_set1_epi64x(static_cast<long long>(key)));
        return static_cast<unsigned>(_mm256_movemask_pd(_mm256_castsi256_pd(eq)));
    }
    void place(uint64_t key, uint64_t meta) {
        for (size_t b = hash_of(key) & bucket_mask;; b = (b + 1) & bucket_mask) {
            Bucket &bucket = buckets[b];
            for (unsigned i = 0; i < 4; ++i) if (!bucket.keys[i]) { bucket.keys[i] = key; bucket.meta[i] = meta; return; }
        }
    }
    void grow(size_t count) {
        vector<Bucket> old(count);
        old.swap(buckets);
        bucket_mask = count - 1;
        for (const Bucket &bucket : old) for (unsigned i = 0; i < 4; ++i) if (bucket.keys[i]) place(bucket.keys[i], bucket.meta[i]);
    }
    static uint32_t alnum_mask(const char *p) {
        const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
        const __m256i fold = _mm256_or_si256(bytes, _mm256_set1_epi8(32));
        const __m256i letters = _mm256_and_si256(_mm256_cmpgt_epi8(fold, _mm256_set1_epi8('a' - 1)), _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), fold));
        const __m256i digits = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, _mm256_set1_epi8('0' - 1)), _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), bytes));
        return static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_or_si256(letters, digits)));
    }
public:
    void reserve(size_t count) {
        keys.reserve(count);
        fingerprints.reserve(count);
        size_t buckets_needed = 1024;
        while (buckets_needed * 2 < count) buckets_needed *= 2;
        grow(buckets_needed);
    }
    uint32_t intern(const Probe &probe) {
        // The frozen query limit keeps IDs and columns within 20 bits.
        if (keys.size() + 2 >= (1u << 20)) throw runtime_error("query metadata capacity");
        if (buckets.empty()) grow(1024);
        else if (keys.size() > buckets.size() * 2) grow(buckets.size() * 2);
        for (size_t b = probe.hash & bucket_mask;; b = (b + 1) & bucket_mask) {
            Bucket &bucket = buckets[b];
            for (unsigned hit = match_mask(bucket, probe.key); hit; hit &= hit - 1) {
                uint32_t id = term_id(bucket.meta[countr_zero(hit)]);
                if ((probe.key >> 62) != 2 || FoldEqual{}(keys[id], probe.word)) return id;
            }
            for (unsigned i = 0; i < 4; ++i) if (!bucket.keys[i]) {
                uint32_t id = static_cast<uint32_t>(keys.size());
                keys.push_back(probe.word);
                fingerprints.push_back(probe.key);
                bucket.keys[i] = probe.key;
                bucket.meta[i] = id + 1;
                return id;
            }
        }
    }
    static uint32_t term_id(uint64_t meta) { return (static_cast<uint32_t>(meta) & 0xfffffu) - 1; }
    size_t size() const { return keys.size(); }
    size_t term_length(uint32_t term) const { return keys[term].size(); }
    void configure_metadata(const vector<uint32_t> &columns, const vector<int32_t> &heads) {
        for (Bucket &bucket : buckets) for (unsigned i = 0; i < 4; ++i) if (bucket.keys[i]) {
            uint32_t id = term_id(bucket.meta[i]);
            uint32_t column = columns[id] + 1;
            uint32_t head = static_cast<uint32_t>(heads[id] + 1);
            // The frozen query limit keeps columns within 20 bits and heads within 24.
            if (column > 0xfffffu || head > 0xffffffu) throw runtime_error("query metadata capacity");
            bucket.meta[i] = (id + 1) | (uint64_t(column) << 20) | (uint64_t(head) << 40);
        }
    }
    // Writes every token boundary (start, end, start, end, ...) of [b, b+len) to bounds.
    // b must be readable for len+64 bytes; returns the boundary count (twice the token count).
    static size_t tokenize(const char *b, size_t len, uint32_t *bounds) {
        uint64_t carry = 0;
        size_t n = 0;
        for (size_t off = 0; off <= len; off += 64) {
            uint64_t m = alnum_mask(b + off) | (uint64_t(alnum_mask(b + off + 32)) << 32);
            size_t rem = len - off;
            if (rem < 64) m &= rem ? ~uint64_t(0) >> (64 - rem) : 0;
            uint64_t t = m ^ ((m << 1) | carry);
            carry = m >> 63;
            const unsigned count = popcount(t);
            uint32_t *out = bounds + n;
            for (unsigned k = 0; k < count; k += 4) {
                out[k] = static_cast<uint32_t>(off) + countr_zero(t); t &= t - 1;
                out[k + 1] = static_cast<uint32_t>(off) + countr_zero(t); t &= t - 1;
                out[k + 2] = static_cast<uint32_t>(off) + countr_zero(t); t &= t - 1;
                out[k + 3] = static_cast<uint32_t>(off) + countr_zero(t); t &= t - 1;
            }
            n += count;
        }
        return n;
    }
    // Fingerprints every token; 1-8 byte tokens take a branch-free batch path, the rest a scalar one.
    void prepare_line(const char *b, size_t n, const uint32_t *bounds, Probe *out, uint32_t *longs) const {
        size_t nl = 0;
        for (size_t i = 0; i < n; ++i) {
            uint32_t s = bounds[2 * i], len = bounds[2 * i + 1] - s;
            uint64_t word;
            memcpy(&word, b + s, 8);
            unsigned used = len < 8 ? len : 8;
            uint64_t key = (word | 0x2020202020202020ULL) & (~uint64_t(0) >> (64 - used * 8));
            uint32_t hash = static_cast<uint32_t>(_mm_crc32_u64(2166136261u, key));
            out[i] = {string_view(b + s, len), hash ? hash : 1, key};
            longs[nl] = static_cast<uint32_t>(i);
            nl += len > 8;
        }
        for (size_t k = 0; k < nl; ++k) {
            Probe &probe = out[longs[k]];
            tie(probe.hash, probe.key) = fingerprint(probe.word);
        }
    }
    void prefetch(const Probe &probe) const { __builtin_prefetch(&buckets[probe.hash & bucket_mask], 0, 3); }
    // Returns the metadata word, or zero on a miss; long-token matches are verified byte for byte.
    uint64_t resolve(const Probe &probe) const {
        const size_t b = probe.hash & bucket_mask;
        const Bucket &bucket = buckets[b];
        const unsigned hit = match_mask(bucket, probe.key);
        // Misses in a full bucket and long-token candidates are the only cases needing a loop.
        if (__builtin_expect(((probe.key >> 62) == 2) | ((hit == 0) & (bucket.keys[3] != 0)), 0)) return resolve_slow(probe, b);
        return bucket.meta[countr_zero(hit) & 3] & -uint64_t(hit != 0);
    }
    // Equal 32-bit fingerprints may name different words, so long-token candidates are compared in full.
    uint64_t resolve_slow(const Probe &probe, size_t b) const {
        const bool exact = (probe.key >> 62) != 2;
        for (;; b = (b + 1) & bucket_mask) {
            const Bucket &bucket = buckets[b];
            for (unsigned i = 0; i < 4; ++i)
                if (bucket.keys[i] == probe.key && (exact || FoldEqual{}(keys[term_id(bucket.meta[i])], probe.word))) return bucket.meta[i];
            if (bucket.keys[3] == 0) return 0;
        }
    }
    uint64_t phrase_key(uint32_t term) const {
        uint64_t key = fingerprints[term];
        return key | ((key >> 62) == 2 ? uint64_t(term) << 32 : 0);
    }
    bool phrase_equal(uint64_t expected, const Probe &probe) const {
        if ((expected >> 62) != 2) return expected == probe.key;
        return probe.key == ((uint64_t(1) << 63) | static_cast<uint32_t>(expected)) &&
            FoldEqual{}(keys[static_cast<uint32_t>(expected >> 32) & 0x7fffffffu], probe.word);
    }
};

// Reusable per-worker buffers that turn one document line into fingerprinted probes.
struct LineTokens {
    vector<uint32_t> bounds, longs;
    vector<FlatDictionary::Probe> probes;
    vector<char> tail;
    size_t count = 0;
    void load(const FlatDictionary &dictionary, const char *text, const char *line_end, const char *file_end) {
        const char *original = text;
        size_t length = line_end - text;
        const bool padded = file_end - line_end < 128;
        if (padded) {
            // Only the file's last lines need a padded copy so SIMD loads stay in bounds.
            tail.assign(length + 128, 0);
            memcpy(tail.data(), text, length);
            text = tail.data();
        }
        size_t capacity = length / 2 + 40;
        if (longs.size() < capacity) { bounds.resize(2 * capacity + 8); longs.resize(capacity); probes.resize(capacity); }
        count = FlatDictionary::tokenize(text, length, bounds.data()) / 2;
        dictionary.prepare_line(text, count, bounds.data(), probes.data(), longs.data());
        // Words always refer to the original bytes, never to the temporary copy.
        if (padded) for (size_t i = 0; i < count; ++i) probes[i].word = string_view(original + (probes[i].word.data() - text), probes[i].word.size());
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
    // Exact count and position sum of a dense bitmap: bytes weigh their index, bits their offset.
    template<size_t words>
    static pair<uint32_t, uint64_t> dense_stats(const uint64_t *data) {
        static_assert(words % 4 == 0);
        const __m256i pop_lut = _mm256_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
        const __m256i weight_lut = _mm256_setr_epi8(0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6);
        const __m256i low = _mm256_set1_epi8(15), zero = _mm256_setzero_si256(), ones = _mm256_set1_epi16(1);
        const __m256i lanes = _mm256_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
        __m256i count = zero, scaled = zero, weighted = zero, bits = zero;
        for (size_t k = 0; k < words / 4; ++k) {
            __m256i x = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data) + k);
            __m256i lo = _mm256_and_si256(x, low), hi = _mm256_and_si256(_mm256_srli_epi16(x, 4), low);
            __m256i pop_hi = _mm256_shuffle_epi8(pop_lut, hi);
            __m256i pop = _mm256_add_epi8(_mm256_shuffle_epi8(pop_lut, lo), pop_hi);
            // High-nibble bits sit four positions higher, adding four per set bit.
            __m256i weight = _mm256_add_epi8(_mm256_add_epi8(_mm256_shuffle_epi8(weight_lut, lo), _mm256_shuffle_epi8(weight_lut, hi)),
                                             _mm256_slli_epi16(_mm256_and_si256(pop_hi, low), 2));
            __m256i sad = _mm256_sad_epu8(pop, zero);
            count = _mm256_add_epi64(count, sad);
            scaled = _mm256_add_epi64(scaled, _mm256_mul_epu32(sad, _mm256_set1_epi64x(static_cast<long long>(32 * k))));
            bits = _mm256_add_epi64(bits, _mm256_sad_epu8(weight, zero));
            weighted = _mm256_add_epi32(weighted, _mm256_madd_epi16(_mm256_maddubs_epi16(pop, lanes), ones));
        }
        auto sum64 = [](__m256i v) {
            alignas(32) uint64_t lane[4];
            _mm256_store_si256(reinterpret_cast<__m256i *>(lane), v);
            return lane[0] + lane[1] + lane[2] + lane[3];
        };
        alignas(32) uint32_t part[8];
        _mm256_store_si256(reinterpret_cast<__m256i *>(part), weighted);
        uint64_t index_sum = sum64(scaled) + part[0] + part[1] + part[2] + part[3] + part[4] + part[5] + part[6] + part[7];
        return {static_cast<uint32_t>(sum64(count)), bits_total(sum64(bits)) + 8 * index_sum};
    }
    static uint64_t bits_total(uint64_t value) { return value; }
    template<size_t words>
    void add_dense(size_t query, const uint64_t *data, size_t in_block, uint32_t base) {
        Summary &row = rows[query];
        if (row.count() < kept_ids) {
            for (size_t w = 0; w < (in_block + 63) / 64; ++w) add_bits(query, data[w], base + static_cast<uint32_t>(w * 64));
            return;
        }
        const auto [count, sum] = dense_stats<words>(data);
        row.total += (uint64_t(base) + (uint64_t(1) << 35)) * count + sum;
    }
    // Merges worker summaries (given in document order) and formats the lines for queries [first, last).
    static void write_range(const vector<const Results *> &parts, size_t first, size_t last, string &out) {
        array<char, 256> line;
        for (size_t query = first; query < last; ++query) {
            Summary merged;
            array<uint32_t, kept_ids> ids;
            size_t kept = 0;
            for (const Results *part : parts) {
                const Summary &row = part->rows[query];
                if (!row.count()) continue;
                size_t add = min<size_t>(row.count(), kept_ids - kept);
                copy_n(part->first_ids[row.first_offset].begin(), add, ids.begin() + kept);
                kept += add;
                merged.total += row.total;
            }
            char *p = line.data();
            p = to_chars(p, p + 24, merged.count()).ptr;
            *p++ = ' ';
            p = to_chars(p, p + 24, merged.sum()).ptr;
            for (size_t i = 0; i < kept; ++i) {
                *p++ = ' ';
                p = to_chars(p, p + 24, ids[i]).ptr;
            }
            *p++ = '\n';
            out.append(line.data(), p - line.data());
        }
    }
};

class Engine {
    FlatDictionary dictionary;
    // Five sorted 12-bit positions fit below the count tag; bit 60 marks a query-local bitmap.
    static constexpr unsigned small_shift = 61, position_bits = 12, small_limit = 5;
    static unsigned small_count(uint64_t state) { return static_cast<unsigned>(state >> small_shift); }
    static uint32_t small_position(uint64_t state, unsigned index) {
        return static_cast<uint32_t>(state >> (index * position_bits)) & 4095u;
    }
    static uint64_t small_union(uint64_t a, uint64_t b) {
        unsigned i = 0, j = 0, size = 0, na = small_count(a), nb = small_count(b);
        uint64_t result = 0;
        while (i < na || j < nb) {
            uint32_t x = i < na ? small_position(a, i) : numeric_limits<uint32_t>::max();
            uint32_t y = j < nb ? small_position(b, j) : numeric_limits<uint32_t>::max();
            uint32_t position = min(x, y);
            if (size == small_limit) return 0;
            result |= uint64_t(position) << (size++ * position_bits);
            if (x == position) ++i;
            if (y == position) ++j;
        }
        return result | (uint64_t(size) << small_shift);
    }
    static bool small_contains(uint64_t state, uint32_t position) {
        // Unused lanes are ones, so zero-lane detection tests only stored positions.
        constexpr uint64_t lanes = 0x1001001001001ULL;
        uint64_t valid = (uint64_t(1) << (small_count(state) * position_bits)) - 1;
        uint64_t difference = (state ^ (uint64_t(position) * lanes)) | ~valid;
        return ((difference - lanes) & ~difference & (lanes << 11)) != 0;
    }
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
    };
    vector<BooleanQuery> boolean_queries;
    vector<Results> partial;

    // A short corpus prefix ranks phrase terms by rarity; ties prefer longer words. Only speed depends on it.
    static constexpr size_t sample_bytes = 512u << 10;
    // Boolean terms seen at least hot_min times in the sample get permanent rows, at most hot_cap of them.
    static constexpr size_t hot_min = 4, hot_cap = 3000;
    size_t hot_columns = 0;
    void compile_queries(const MappedFile &input) {
        // Sampling affects anchor choice, never whether a match is accepted.
        const size_t term_count = dictionary.size();
        vector<uint32_t> frequency(term_count);
        const char *file_end = input.data + input.size, *stop = input.data + min<size_t>(input.size, sample_bytes);
        LineTokens line;
        for (const char *p = input.data; p < stop;) {
            const char *line_end = static_cast<const char *>(memchr(p, '\n', file_end - p));
            if (!line_end) line_end = file_end;
            const char *tab = static_cast<const char *>(memchr(p, '\t', line_end - p));
            if (tab) {
                line.load(dictionary, tab + 1, line_end, file_end);
                for (size_t i = 0; i < line.count; ++i)
                    if (uint64_t meta = dictionary.resolve(line.probes[i])) ++frequency[FlatDictionary::term_id(meta)];
            }
            p = line_end < file_end ? line_end + 1 : file_end;
        }
        vector<int32_t> anchor_head(term_count, -1);
        query_count = queries.size();
        size_t bool_count = static_cast<size_t>(count_if(queries.begin(), queries.end(), [](const Query &q) { return !q.phrase; }));
        boolean_queries.reserve(bool_count);
        phrases.reserve(query_count - bool_count);
        vector<uint32_t> boolean_column(term_count, numeric_limits<uint32_t>::max());
        {   // Frequent Boolean terms take the first columns and permanent bitmap rows.
            vector<uint32_t> hot;
            vector<uint8_t> seen(term_count);
            for (const Query &q : queries) if (!q.phrase) for (uint32_t term : q.words())
                if (!seen[term]++ && frequency[term] >= hot_min) hot.push_back(term);
            if (hot.size() > hot_cap) {
                nth_element(hot.begin(), hot.begin() + hot_cap, hot.end(), [&](uint32_t a, uint32_t b) { return frequency[a] > frequency[b]; });
                hot.resize(hot_cap);
            }
            for (uint32_t term : hot) boolean_column[term] = static_cast<uint32_t>(boolean_count++);
            hot_columns = boolean_count;
        }
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
                boolean_queries.push_back(plan);
                continue;
            }
            size_t best = 0;
            for (size_t i = 1; i < q.term_count; ++i)
                if (frequency[q.terms[i]] < frequency[q.terms[best]] ||
                    (frequency[q.terms[i]] == frequency[q.terms[best]] && dictionary.term_length(q.terms[i]) > dictionary.term_length(q.terms[best]))) best = i;
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

    void match_document(uint32_t doc, span<const FlatDictionary::Probe> probes,
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
    void evaluate_block(const vector<uint64_t> &matrix, const vector<uint64_t> &active,
                        size_t in_block, uint32_t block_base, Results &results) const {
        static constexpr size_t words = block_docs / 64;
        static constexpr uint64_t local_value = uint64_t(1) << 60;
        for (const BooleanQuery &q : boolean_queries) {
            uint64_t state = active[q.terms[0]];
            array<uint64_t, words> value;
            auto present = [&](uint64_t source, uint32_t position) -> bool {
                unsigned size = small_count(source);
                if (size) return small_contains(source, position);
                const uint64_t *data = source == local_value ? value.data() : matrix.data() + source * words;
                return (data[position / 64] >> (position % 64)) & 1;
            };
            auto add_small = [&](uint64_t source) {
                for (unsigned j = 0, size = small_count(source); j < size; ++j) {
                    uint32_t position = small_position(source, j);
                    value[position / 64] |= uint64_t(1) << (position % 64);
                }
            };
            auto materialize = [&] {
                if (state == local_value) return;
                if (small_count(state)) { value.fill(0); add_small(state); }
                else copy_n(matrix.data() + state * words, words, value.data());
                state = local_value;
            };
            auto filter = [&](uint64_t source, uint64_t other, bool keep_matches) {
                uint64_t result = 0;
                unsigned kept = 0;
                for (unsigned j = 0, size = small_count(source); j < size; ++j) {
                    uint32_t position = small_position(source, j);
                    if (present(other, position) == keep_matches)
                        result |= uint64_t(position) << (kept++ * position_bits);
                }
                return result | (uint64_t(kept) << small_shift);
            };
            for (size_t i = 0; i < size_t(q.count - 1); ++i) {
                char op = q.ops[i];
                if (!state && op != 'O') continue;
                uint64_t other = active[q.terms[i+1]];
                if (op == 'O') {
                    if (!other || state == other) continue;
                    if (!state) { state = other; continue; }
                    unsigned left_size = small_count(state), right_size = small_count(other);
                    if (left_size && right_size) {
                        uint64_t merged = small_union(state, other);
                        if (merged) state = merged;
                        else { materialize(); add_small(other); }
                    } else if (right_size) { materialize(); add_small(other); }
                    else if (left_size) {
                        uint64_t previous = state;
                        copy_n(matrix.data() + other * words, words, value.data());
                        add_small(previous);
                        state = local_value;
                    } else {
                        materialize();
                        const uint64_t *rhs = matrix.data() + other * words;
                        for (size_t w = 0; w < words; ++w) value[w] |= rhs[w];
                    }
                } else {
                    if (!other) { if (op == 'A') state = 0; continue; }
                    if (small_count(state)) state = filter(state, other, op == 'A');
                    else if (small_count(other)) {
                        if (op == 'A') state = filter(other, state, true);
                        else {
                            materialize();
                            for (unsigned j = 0, size = small_count(other); j < size; ++j) {
                                uint32_t position = small_position(other, j);
                                value[position / 64] &= ~(uint64_t(1) << (position % 64));
                            }
                        }
                    } else {
                        materialize();
                        const uint64_t *rhs = matrix.data() + other * words;
                        if (op == 'A') for (size_t w = 0; w < words; ++w) value[w] &= rhs[w];
                        else for (size_t w = 0; w < words; ++w) value[w] &= ~rhs[w];
                    }
                }
            }
            if (unsigned size = small_count(state)) {
                for (unsigned j = 0; j < size; ++j) results.add(q.output, block_base + small_position(state, j));
            } else if (state) {
                const uint64_t *data = state == local_value ? value.data() : matrix.data() + state * words;
                results.template add_dense<words>(q.output, data, in_block, block_base);
            }
        }
    }

    template<size_t block_docs>
    void scan_corpus(const MappedFile &input, size_t workers) {
        static constexpr size_t words = block_docs / 64;
        static_assert(block_docs <= (1u << position_bits));
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
                // Row 0 is empty, rows 1..hot_columns are permanent hot rows, the next row absorbs non-hot writes.
                const size_t base_rows = hot_columns + 2;
                vector<uint64_t> matrix(words * base_rows);
                matrix.reserve(words * (boolean_count + 2));
                vector<uint32_t> cold(1024);
                vector<uint64_t> active(boolean_count);
                uint32_t block_base = 0;
                size_t in_block = 0;
                auto flush = [&] {
                    for (size_t column = 0; column < hot_columns; ++column) active[column] = column + 1;
                    evaluate_block<block_docs>(matrix, active, in_block, block_base, c.results);
                    matrix.resize(base_rows * words);
                    fill(matrix.begin() + words, matrix.end(), 0);
                    fill(active.begin(), active.end(), 0);
                    in_block = 0;
                };
                vector<uint32_t> last(phrases.size(), numeric_limits<uint32_t>::max());
                vector<pair<uint32_t, int32_t>> anchors;
                anchors.reserve(128);
                LineTokens line;
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
                    line.load(dictionary, tab + 1, line_end, end);
                    const size_t token_count = line.count;
                    const auto *probes = line.probes.data();
                    constexpr size_t kAhead = 8;
                    for (size_t i = 0; i < kAhead && i < token_count; ++i) dictionary.prefetch(probes[i]);
                    const uint64_t document_bit = uint64_t(1) << (in_block % 64);
                    const size_t document_word = in_block / 64;
                    if (anchors.size() < token_count + 1) anchors.resize(token_count + 1);
                    if (cold.size() < token_count) cold.resize(token_count);
                    uint64_t *hot_rows = matrix.data();
                    size_t cold_count = 0;
                    for (size_t index = 0; index < token_count; ++index) {
                        if (index + kAhead < token_count) dictionary.prefetch(probes[index + kAhead]);
                        const uint64_t entry = dictionary.resolve(probes[index]);
                        const int32_t head = static_cast<int32_t>(entry >> 40) - 1;
                        anchors[anchor_count] = {static_cast<uint32_t>(index), head};
                        anchor_count += head >= 0;
                        const uint32_t column = static_cast<uint32_t>(entry >> 20) & 0xfffffu;
                        const bool hot = column - 1 < hot_columns;
                        hot_rows[(hot ? column : hot_columns + 1) * words + document_word] |= document_bit;
                        cold[cold_count] = column;
                        cold_count += column > hot_columns;
                    }
                    for (size_t k = 0; k < cold_count; ++k) {
                        const uint32_t column = cold[k] - 1;
                        uint64_t &row = active[column];
                        unsigned size = small_count(row);
                        if (!row) row = (uint64_t(1) << small_shift) | in_block;
                        else if (size) {
                            if (small_position(row, size - 1) != in_block) {
                                if (size < small_limit) {
                                    row = (row & ((uint64_t(1) << small_shift) - 1)) |
                                        (uint64_t(in_block) << (size * position_bits)) |
                                        (uint64_t(size + 1) << small_shift);
                                } else {
                                    uint64_t previous = row;
                                    row = matrix.size() / words;
                                    matrix.resize(matrix.size() + words, 0);
                                    for (unsigned j = 0; j < small_limit; ++j) {
                                        uint32_t position = small_position(previous, j);
                                        matrix[row * words + position / 64] |= uint64_t(1) << (position % 64);
                                    }
                                    matrix[row * words + document_word] |= document_bit;
                                }
                            }
                        } else matrix[row * words + document_word] |= document_bit;
                    }
                    match_document(doc, {probes, token_count}, {anchors.data(), anchor_count}, c.results, last);
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
        for (Chunk &c : chunks) {
            if (c.docs && c.first_doc != expected_doc) throw runtime_error("nonconsecutive document chunks");
            expected_doc += static_cast<uint32_t>(c.docs);
            partial.push_back(move(c.results));
        }
    }

    // Each thread merges and formats one contiguous range of queries.
    void write_answers(ostream &out, size_t threads) {
        vector<const Results *> parts;
        for (const Results &part : partial) parts.push_back(&part);
        vector<string> text(threads);
        auto format = [&](size_t i) {
            size_t first = query_count * i / threads, last = query_count * (i + 1) / threads;
            text[i].reserve((last - first) * 32);
            Results::write_range(parts, first, last, text[i]);
        };
        vector<thread> pool;
        for (size_t i = 1; i < threads; ++i) pool.emplace_back(format, i);
        format(0);
        for (thread &t : pool) t.join();
        for (const string &chunk : text) out.write(chunk.data(), chunk.size());
    }

    void parse_queries(const MappedFile &input) {
        const char *p = input.data, *end = p + input.size;
        dictionary.reserve(input.size / 24);
        queries.reserve(input.size / 20);
        LineTokens line;
        while (p < end) {
            const char *line_end = static_cast<const char *>(memchr(p, '\n', end - p));
            if (!line_end) line_end = end;
            line.load(dictionary, p, line_end, end);
            Query q;
            auto is = [&](size_t i, string_view text) { return line.probes[i].word == text; };
            if (line.count && is(0, "BOOL")) {
                // Terms and operators alternate; A, O and N identify AND, OR and NOT.
                for (size_t i = 1; i < line.count; ++i) {
                    if (i % 2) q.add_term(dictionary.intern(line.probes[i]));
                    else q.add_operator(line.probes[i].word[0]);
                }
            } else if (line.count && is(0, "PHRASE")) {
                q.phrase = true;
                for (size_t i = 1; i < line.count; ++i) q.add_term(dictionary.intern(line.probes[i]));
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
        const size_t workers = docs_file.size >= (8u << 20) ? 12 : 1;
        scan_corpus<2048>(docs_file, workers);
        filesystem::create_directories(out_dir);
        ofstream output(out_dir + "/results.txt", ios::binary);
        if (!output) throw runtime_error("cannot open output");
        write_answers(output, workers);
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
