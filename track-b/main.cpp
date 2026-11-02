#include <fcntl.h>
#include <malloc.h>
#include <immintrin.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
using namespace std;
using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
typedef unsigned char B32 __attribute__((vector_size(32)));
typedef u64 Q4 __attribute__((vector_size(32)));
static B32 load32(const void *p) {
  B32 v;
  memcpy(&v, p, 32);
  return v;
}

// Maps a file read-only, followed by zero bytes so SIMD loads may overrun it.
static const char *map_file(const string &path, size_t &size) {
  int fd = open(path.c_str(), O_RDONLY);
  struct stat st;
  if (fd < 0 || fstat(fd, &st))
    throw runtime_error("cannot open " + path);
  size = st.st_size;
  int anon = MAP_PRIVATE | MAP_ANONYMOUS, fixed = MAP_PRIVATE | MAP_FIXED;
  char *p = (char *)mmap(0, size + 4096, PROT_READ, anon, -1, 0);
  if (p == MAP_FAILED ||
      (size && mmap(p, size, PROT_READ, fixed, fd, 0) == MAP_FAILED))
    throw runtime_error("cannot map " + path);
  close(fd);
  return p;
}

// Runs f(0), ..., f(n - 1) on n threads and rethrows the first failure.
template <class F> static void parallel(u32 n, F &&f) {
  vector<exception_ptr> errors(n);
  vector<thread> pool;
  for (u32 i = 0; i < n; i++)
    pool.emplace_back([&, i] {
      try {
        f(i);
      } catch (...) {
        errors[i] = current_exception();
      }
    });
  for (thread &t : pool)
    t.join();
  for (exception_ptr &e : errors)
    if (e)
      rethrow_exception(e);
}

// The first line of [b, b + size) that starts at or after offset off.
static const char *line_at(const char *b, size_t size, size_t off) {
  if (!off || off >= size)
    return b + min(off, size);
  auto *p = (const char *)memchr(b + off - 1, '\n', size - off + 1);
  return p ? p + 1 : b + size;
}

// Bit i is set when byte i of the 64 bytes at p is an ASCII letter or digit.
static u64 alnum64(const char *p) {
  auto half = [](B32 b) {
    B32 is = (B32)((b | 32) - 'a' < 26) | (B32)(b - '0' < 10);
    return u64(u32(_mm256_movemask_epi8(__m256i(is))));
  };
  return half(load32(p)) | half(load32(p + 32)) << 32;
}

// Writes the offsets where the tokens of [b, b + n) start and end, alternately,
// to out (n + 72 entries) and returns their number.
static u32 edges(const char *b, size_t n, u32 *out) {
  u32 k = 0;
  u64 carry = 0;
  for (size_t off = 0; off < n; off += 64) {
    u64 m = alnum64(b + off) & (n - off < 64 ? (u64(1) << (n - off)) - 1 : ~0);
    u64 t = m ^ (m << 1 | carry);
    u32 c = popcount(t);
    carry = m >> 63;
    for (u32 i = 0; i < c; i += 4) // Unrolled stores past c are overwritten.
      for (u32 j = 0; j < 4; j++, t &= t - 1)
        out[k + i + j] = off + countr_zero(t);
    k += c;
  }
  out[k] = n;
  return k + carry;
}

static size_t token_length(const char *p) {
  size_t n = 0;
  while (!~alnum64(p + n))
    n += 64;
  return n + countr_one(alnum64(p + n));
}
// Eight lowercase token bytes from offset i, zero past the token's n bytes.
static u64 chunk(const char *p, size_t i, size_t n) {
  u64 x;
  memcpy(&x, p + i, 8);
  return (x | 0x2020202020202020) & ~u64(0) >> (n - i < 8 ? 64 - 8 * (n - i) : 0);
}
// Tokens of up to eight bytes are their own key and 9-12 letters pack 5-bit
// codes; other keys are CRC fingerprints (top bits 10) checked against text.
static u64 key_of(const char *p, size_t n) {
  u64 a = chunk(p, 0, n), b, h = n;
  if (n <= 8)
    return a;
  memcpy(&b, p + n - 8, 8);
  b |= 0x2020202020202020;
  if (n <= 12 && (a & b & 0x4040404040404040) == 0x4040404040404040)
    return 3ull << 62 | _pext_u64(a, 0x1f1f1f1f1f1f1f1f) |
           _pext_u64(b >> (16 - n) * 8, 0x1f1f1f1f1f1f1f1f) << 40;
  for (size_t i = 0; i < n; i += 8)
    h = _mm_crc32_u64(h, chunk(p, i, n));
  return 2ull << 62 | h;
}
static u32 hash_of(u64 key) { return _mm_crc32_u64(0, key); }

// Query terms in four-way buckets. Metadata holds the term ID in bits 0-19, the
// Boolean column in bits 20-39 and the phrase head plus one above.
struct Dictionary {
  struct alignas(32) Bucket {
    u64 key[4];
  };
  vector<Bucket> buckets = vector<Bucket>(1024);
  vector<array<u64, 4>> metadata = vector<array<u64, 4>>(1024);
  vector<const char *> words;
  u32 mask() const { return buckets.size() - 1; }
  static u32 match(const Bucket &b, u64 key) {
    __m256i keys = _mm256_load_si256((const __m256i *)b.key);
    __m256i eq = _mm256_cmpeq_epi64(keys, _mm256_set1_epi64x(key));
    return _mm256_movemask_pd(_mm256_castsi256_pd(eq));
  }
  void prefetch(u64 key) const {
    _mm_prefetch((const char *)&buckets[hash_of(key) & mask()], _MM_HINT_T0);
  }
  // The token's metadata, or zero when it is no query term.
  u64 find(const char *p, u64 key, u32 bucket) const {
    const Bucket &b = buckets[bucket];
    u32 m = match(b, key);
    if (__builtin_expect((key >> 62 == 2) | (!m & (b.key[3] != 0)), 0))
      return find_slow(p, key);
    if (!m)
      return 0;
    return metadata[bucket][countr_zero(m)];
  }
  u64 find_slow(const char *p, u64 key) const {
    for (u32 i = hash_of(key);; i++) {
      const Bucket &b = buckets[i & mask()];
      const auto &meta = metadata[i & mask()];
      for (u32 m = match(b, key); m; m &= m - 1)
        if (key >> 62 != 2 || same(meta[countr_zero(m)], p))
          return meta[countr_zero(m)];
      if (!b.key[3])
        return 0;
    }
  }
  bool same(u64 meta, const char *p) const {
    const char *w = words[meta & 0xfffff];
    size_t n = token_length(p), i = 0;
    while (i < n && chunk(w, i, n) == chunk(p, i, n))
      i += 8;
    return i >= n && token_length(w) == n;
  }
  // Thread-safe: a slot is claimed by compare-and-swap, then its ID published.
  u32 intern(const char *p, u64 key, u32 &next) {
    for (u32 i = hash_of(key), j = 0;; j = (j + 1) % 4, i += !j) {
      Bucket &b = buckets[i & mask()];
      atomic_ref<u64> slot(b.key[j]), meta(metadata[i & mask()][j]);
      u64 seen = slot.load(memory_order_acquire), id;
      if (!seen && slot.compare_exchange_strong(seen, key)) {
        words[id = next++] = p;
        meta.store(id, memory_order_release);
        return id;
      }
      while (seen == key && !(id = meta.load(memory_order_acquire)))
        ;
      if (seen == key && (key >> 62 != 2 || same(id, p)))
        return id;
    }
  }
};

// Plans keep their output line. Boolean terms become columns once planned;
// phrase terms stay term IDs.
struct Bool {
  u32 query, n, term[6];
  char op[5];
  u16 shape; // Term count and operators; AND, NOT, OR differ in low bits.
};
struct Phrase {
  u32 query, n, anchor, term[8];
  int next;
};
// The ten smallest matching IDs of one query, merged under a spin lock.
struct First {
  atomic<u64> total{0}; // Packed count << 35 | ID sum, as in Worker::totals.
  atomic_flag busy;
  u32 n = 0, id[10];
};

// A block's term sets are rows of 2,048 bits or, up to five positions, 12-bit
// fields under a 3-bit count.
constexpr u32 kDocs = 2048, kWords = kDocs / 64, kSmall = 5;
constexpr u64 kLocal = u64(1) << 60, kOne = u64(1) << 61;
static u32 count_of(u64 s) { return s >> 61; }
static u32 at(u64 s, u32 i) { return s >> (12 * i) & 4095; }
static bool small_has(u64 s, u32 p) {
  for (u32 i = 0; i < count_of(s); i++)
    if (at(s, i) == p)
      return true;
  return false;
}

struct Worker {
  vector<u64> rows, state, totals, keys, pending;
  vector<u32> edge, longs, cold, last, touched;
  vector<pair<u32, int>> anchors;
  u64 value[kWords];
};

struct Engine {
  Dictionary dict;
  vector<Bool> bools;
  vector<Phrase> phrases;
  u32 queries = 0, hot = 0, cols = 1;
  thread planner;
  atomic<bool> planned{false};
  vector<u32> guards, guarded; // guarded[guards[c]...] lists column c's queries
  unique_ptr<First[]> first;
  unique_ptr<atomic<u32>[]> bound; // The tenth smallest ID once a list is full.

  // Threads parse line-aligned parts of the query file, then intern their
  // terms concurrently into a table sized for the term count.
  void parse(const char *q, size_t size, u32 workers) {
    struct Part {
      vector<Bool> bools;
      vector<Phrase> phrases;
      vector<const char *> at;
      vector<u64> keys;
      u32 lines = 0;
    };
    vector<Part> parts(workers);
    parallel(workers, [&](u32 t) {
      Part &w = parts[t];
      vector<u32> e(512);
      const char *p = line_at(q, size, size * t / workers),
                 *end = line_at(q, size, size * (t + 1) / workers), *eol;
      w.at.reserve((end - p) / 2), w.keys.reserve((end - p) / 2);
      w.bools.reserve((end - p) / 7), w.phrases.reserve((end - p) / 7);
      for (; p < end; p = eol + 1, w.lines++) {
        eol = (const char *)memchr(p, '\n', end - p);
        eol = eol ? eol : end;
        if (e.size() < size_t(eol - p) + 72)
          e.resize(eol - p + 72);
        u32 n = edges(p, eol - p, e.data()) / 2, is_bool = n > 1 && *p == 'B';
        if (n < 2 || n > (is_bool ? 12 : 9) || (is_bool && n % 2))
          throw runtime_error("invalid query");
        auto term = [&](u32 i) {
          w.at.push_back(p + e[2 * i]);
          w.keys.push_back(key_of(w.at.back(), e[2 * i + 1] - e[2 * i]));
          return w.at.size() - 1;
        };
        if (is_bool) {
          Bool &b = w.bools.emplace_back(Bool{w.lines, n / 2});
          for (u32 i = 1; i < n; i++)
            i % 2 ? b.term[i / 2] = term(i) : b.op[i / 2 - 1] = p[e[2 * i]];
          b.shape = b.n;
          for (char op : b.op)
            b.shape = b.shape << 2 | (op & 3);
        } else {
          Phrase &f = w.phrases.emplace_back(Phrase{w.lines, n - 1});
          for (u32 i = 1; i < n; i++)
            f.term[i - 1] = term(i);
        }
      }
    });
    // Each part numbers its new terms from its own first ID, and its plans
    // start at its own offsets.
    vector<u32> first_id(workers + 1, 1), nb(workers + 1), np(workers + 1),
        nq(workers + 1);
    for (u32 t = 0; t < workers; t++) {
      first_id[t + 1] = first_id[t] + parts[t].at.size();
      nb[t + 1] = nb[t] + parts[t].bools.size();
      np[t + 1] = np[t] + parts[t].phrases.size();
      nq[t + 1] = nq[t] + parts[t].lines;
    }
    if (first_id[workers] >= 1 << 20)
      throw runtime_error("too many query terms");
    dict.buckets.resize(bit_ceil(first_id[workers] / 4 + 1024));
    dict.metadata.resize(dict.buckets.size());
    dict.words.resize(first_id[workers]);
    bools.resize(nb[workers]), phrases.resize(np[workers]);
    queries = nq[workers];
    parallel(workers, [&](u32 t) {
      Part &w = parts[t];
      for (size_t i = 0; i < w.at.size(); i++) {
        if (i + 16 < w.at.size())
          dict.prefetch(w.keys[i + 16]);
        w.keys[i] = dict.intern(w.at[i], w.keys[i], first_id[t]);
      }
      auto place = [&](auto &all, auto &part, u32 at) {
        for (auto &x : part) {
          for (u32 i = 0; i < x.n; i++)
            x.term[i] = w.keys[x.term[i]];
          x.query += nq[t], all[at++] = x;
        }
      };
      place(bools, w.bools, nb[t]), place(phrases, w.phrases, np[t]);
    });
    first.reset(new First[queries]), bound.reset(new atomic<u32>[queries]);
    for (u32 q = 0; q < queries; q++)
      bound[q].store(~0u, memory_order_relaxed);
  }

  // A corpus prefix ranks the terms. It decides hot rows, phrase anchors and
  // Boolean guards, which change the work done but never the answers.
  void plan(const char *docs, size_t size) {
    size_t terms = dict.words.size();
    vector<u32> freq(terms + 1), seen(terms), order, column(terms), e(512);
    vector<int> head(terms, -1);
    const char *stop = line_at(docs, size, 512 << 10);
    for (const char *p = docs, *eol; p < stop; p = eol + 1) {
      eol = (const char *)memchr(p, '\n', stop - p);
      eol = eol ? eol : stop;
      if (e.size() < size_t(eol - p) + 72)
        e.resize(eol - p + 72);
      // Edges 0 and 1 delimit the document ID.
      for (u32 i = 2, n = edges(p, eol - p, e.data()); i < n; i += 2)
        if (u64 key = key_of(p + e[i], e[i + 1] - e[i]); true)
          freq[dict.find(p + e[i], key, hash_of(key) & dict.mask()) & 0xfffff]++;
    }
    freq[0] = 0;
    for (Bool &b : bools)
      for (u32 i = 0; i < b.n; i++)
        if (freq[b.term[i]] >= 4 && !seen[b.term[i]]++)
          order.push_back(b.term[i]);
    sort(order.begin(), order.end(),
         [&](u32 a, u32 b) { return freq[a] > freq[b]; });
    for (u32 i = 0; i < order.size() && i < 3000; i++)
      column[order[i]] = ++hot;
    vector<u32> weight(terms + 1);
    cols = hot + 1;
    for (Bool &b : bools)
      for (u32 i = 0; i < b.n; i++) {
        u32 &c = column[b.term[i]];
        weight[c = c ? c : cols++] = freq[b.term[i]] + 1;
        b.term[i] = c;
      }
    // Sorting by operator shape makes the evaluator's branches repeat.
    // Evaluation order and guards are first needed at a block's end, so they are
    // planned while the scan starts.
    planner = thread([this, weight = std::move(weight)] {
      sort(bools.begin(), bools.end(),
           [](const Bool &a, const Bool &b) { return a.shape < b.shape; });
      plan_guards(weight);
      planned.store(true, memory_order_release);
    });
    for (u32 k = 0; k < phrases.size(); k++) {
      Phrase &f = phrases[k];
      for (u32 i = 1; i < f.n; i++)
        if (freq[f.term[i]] < freq[f.term[f.anchor]])
          f.anchor = i;
      f.next = exchange(head[f.term[f.anchor]], k);
    }
    for (auto &row : dict.metadata)
      for (u64 &m : row)
        if (m)
          m |= u64(column[m]) << 20 | u64(head[m] + 1) << 40;
  }

  // An answer lies within S(t_j) and the OR terms after j, for t_1 and every
  // AND term t_j. The rarest such guard picks the blocks a query is run in.
  void plan_guards(const vector<u32> &weight) {
    auto guard = [&](const Bool &b, u32 *g) {
      u32 best = ~0u, n = 0;
      for (u32 j = 0; j < b.n; j++) {
        u32 cost = weight[b.term[j]], k = 1, terms[6] = {b.term[j]};
        for (u32 i = j + 1; i < b.n; i++)
          if (b.op[i - 1] == 'O')
            cost += weight[b.term[i]], terms[k++] = b.term[i];
        if ((!j || b.op[j - 1] == 'A') && cost < best)
          best = cost, n = k, copy(terms, terms + k, g);
      }
      return n;
    };
    guards.assign(cols + 1, 0);
    u32 g[6];
    for (const Bool &b : bools)
      for (u32 i = 0, n = guard(b, g); i < n; i++)
        guards[g[i]]++;
    for (u32 c = 1; c <= cols; c++)
      guards[c] += guards[c - 1];
    guarded.resize(guards[cols]);
    for (u32 q = bools.size(); q--;)
      for (u32 i = 0, n = guard(bools[q], g); i < n; i++)
        guarded[--guards[g[i]]] = q;
  }

  void offer(u32 q, const u32 *id, u32 k) {
    First &f = first[q];
    if (id[0] > bound[q].load(memory_order_relaxed))
      return;
    while (f.busy.test_and_set(memory_order_acquire))
      ;
    u32 out[10], i = 0, j = 0, m = 0;
    while (m < 10 && (i < f.n || j < k))
      out[m++] = j == k || (i < f.n && f.id[i] < id[j]) ? f.id[i++] : id[j++];
    memcpy(f.id, out, m * 4);
    if ((f.n = m) == 10)
      bound[q].store(out[9], memory_order_relaxed);
    f.busy.clear(memory_order_release);
  }
  // Plan b's per-worker totals and the query's shared first IDs take a set.
  void add_small(Worker &w, u32 b, u64 s, u32 base) {
    u32 id[kSmall], k = count_of(s);
    u64 sum = 0;
    for (u32 i = 0; i < k; i++) { // Unions may leave positions unsorted.
      sum += id[i] = base + at(s, i);
      for (u32 j = i; j && id[j] < id[j - 1]; j--)
        swap(id[j], id[j - 1]);
    }
    w.totals[b] += (u64(k) << 35) + sum;
    offer(bools[b].query, id, k);
  }
  // Counts a block row and sums its positions with nibble tables. A byte j of
  // word w weighs 64w + 8j per set bit, plus each bit's offset in the byte.
  void add_bits(Worker &w, u32 b, const u64 *x, u32 base) {
    u32 q = bools[b].query;
    auto shuffle = [](B32 t, B32 i) { return (B32)_mm256_shuffle_epi8(__m256i(t), __m256i(i)); };
    auto sad = [](B32 v) { return (Q4)_mm256_sad_epu8(__m256i(v), __m256i{}); };
    const B32 pop = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
                     0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4},
              weight = {0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6,
                        0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6};
    const B32 lane = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                      11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                      22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    Q4 c = {}, s = {}, index = {};
    for (u32 i = 0; i < kWords / 4; i++) {
      B32 v = load32(x + 4 * i), lo = v & 15, hi = v >> 4, ph = shuffle(pop, hi);
      B32 p = shuffle(pop, lo) + ph;
      Q4 n = sad(p), at = (Q4)_mm256_mul_epu32(__m256i(n), __m256i(Q4{} + 256 * i));
      c += n;
      s += sad(shuffle(weight, lo) + shuffle(weight, hi) + (ph << 2)) + at;
      __m256i j = _mm256_maddubs_epi16(__m256i(p), __m256i(lane));
      index += (Q4)_mm256_madd_epi16(j, _mm256_set1_epi16(8));
    }
    index += index >> 32; // Pairs of 32-bit lane sums.
    u64 n = c[0] + c[1] + c[2] + c[3];
    if (!n)
      return;
    w.totals[b] += (n << 35) + n * base + s[0] + s[1] + s[2] + s[3] +
                   u32(index[0]) + u32(index[1]) + u32(index[2]) + u32(index[3]);
    u32 id[10], k = 0;
    for (u32 i = 0, more = base <= bound[q]; more && i < kWords && k < 10; i++)
      for (u64 b = x[i]; b && k < 10; b &= b - 1)
        id[k++] = base + 64 * i + countr_zero(b);
    if (k)
      offer(q, id, k);
  }

  // Applies the guarded Boolean queries, strictly left to right, to a block.
  void evaluate(Worker &w, u32 base) {
    const u64 *rows = w.rows.data();
    u64 *v = w.value, r;
    auto bits = [&](u64 s) { return s == kLocal ? v : rows + s * kWords; };
    auto has = [&](u64 s, u32 p) {
      return count_of(s) ? small_has(s, p) : bits(s)[p / 64] >> p % 64 & 1;
    };
    auto filter = [&](u64 s, u64 o, bool keep) { // Positions of s (not) in o.
      u64 out = 0, k = 0;
      for (u32 i = 0; i < count_of(s); i++)
        if (has(o, at(s, i)) == keep)
          out |= u64(at(s, i)) << (12 * k++);
      return k ? out | k << 61 : 0;
    };
    auto unite = [&](u64 o) { // Unions may leave positions unsorted.
      for (u32 j = 0; j < count_of(o); j++)
        if (!small_has(r, at(o, j)))
          r = (r + kOne) | u64(at(o, j)) << (12 * count_of(r));
    };
    auto apply = [&](char op, u64 o) { // Makes r a row in v, then applies o.
      if (r != kLocal && count_of(r))
        memset(v, 0, sizeof w.value);
      for (u32 i = 0; r != kLocal && i < count_of(r); i++)
        v[at(r, i) / 64] |= u64(1) << at(r, i) % 64;
      if (r != kLocal && !count_of(r))
        memcpy(v, rows + r * kWords, sizeof w.value);
      r = kLocal;
      for (u32 j = 0; j < count_of(o); j++) {
        u64 &x = v[at(o, j) / 64], bit = u64(1) << at(o, j) % 64;
        x = op == 'O' ? x | bit : x & ~bit;
      }
      for (u32 j = 0; !count_of(o) && j < kWords; j++) {
        u64 x = rows[o * kWords + j];
        v[j] = op == 'O' ? v[j] | x : op == 'A' ? v[j] & x : v[j] & ~x;
      }
    };
    for (u32 word = 0; word < w.pending.size(); word++) {
      u32 list[64], n = 0; // Plans and their first sets are prefetched.
      for (u64 todo = exchange(w.pending[word], 0); todo; todo &= todo - 1)
        list[n++] = word * 64 + countr_zero(todo);
      for (u32 q = 0; q < n; q++) {
        if (q + 8 < n)
          _mm_prefetch((const char *)&bools[list[q + 8]], _MM_HINT_T0);
        if (q + 4 < n)
          _mm_prefetch((const char *)&w.state[bools[list[q + 4]].term[0]], _MM_HINT_T0);
        const Bool &b = bools[list[q]];
        r = w.state[b.term[0]];
        for (u32 i = 1; i < b.n; i++) {
          u64 o = w.state[b.term[i]];
          char op = b.op[i - 1];
          if (op == 'O' ? !o || o == r : !r)
            continue;
          if (op != 'O' && !o)
            r = op == 'A' ? 0 : r;
          else if (op == 'O' && !r)
            r = o;
          else if (op != 'O' && count_of(r))
            r = filter(r, o, op == 'A');
          else if (op == 'A' && count_of(o))
            r = filter(o, r, true);
          else if (count_of(r) && count_of(o) && count_of(r) + count_of(o) <= kSmall)
            unite(o);
          else
            apply(op, o);
        }
        if (count_of(r))
          add_small(w, list[q], r, base);
        else if (r)
          add_bits(w, list[q], bits(r), base);
      }
    }
  }

  // Workers claim line-aligned byte ranges that shrink towards the end, and
  // gather up to 2,048 consecutive documents per block.
  void scan(const char *docs, size_t size, vector<Worker> &all) {
    atomic<size_t> next{0};
    parallel(all.size(), [&](u32 me) {
      Worker &w = all[me];
      w.rows.reserve((2 * hot + 1024) * kWords); // Spill rows grow in place.
      w.rows.assign((hot + 1) * kWords, 0);
      w.state.assign(cols, 0);
      w.totals.assign(bools.size(), 0);
      w.last.assign(phrases.size(), ~0u);
      w.pending.assign(bools.size() / 64 + 1, 0);
      for (size_t from = 0; (from = next.load()) < size;) {
        size_t step = clamp<size_t>((size - from) / (4 * all.size()), 256 << 10,
                                    6 << 20);
        if (!next.compare_exchange_weak(from, from + step))
          continue;
        u32 base = 0, n = 0;
        const char *p = line_at(docs, size, from), *end = docs + size;
        for (const char *stop = line_at(docs, size, from + step); p < stop;) {
          const char *t = (const char *)memchr(p, '\t', stop - p);
          u32 doc = 0;
          if (!t || from_chars(p, t, doc).ptr != t || doc >= (1 << 18))
            throw runtime_error("invalid document");
          base = n ? base : doc;
          if (doc != base + n)
            throw runtime_error("nonconsecutive document id");
          p = (const char *)memchr(t, '\n', end - t);
          p = p ? p + 1 : end;
          process(w, doc, n, t + 1, p - t - 1 - (p[-1] == '\n'));
          if (++n == kDocs || p >= stop)
            flush(w, base), n = 0;
        }
      }
      vector<u64>().swap(w.rows), vector<u64>().swap(w.state); // Before output.
    });
  }

  // Runs the block's guarded queries, then clears the block's term sets.
  void flush(Worker &w, u32 base) {
    while (!planned.load(memory_order_acquire))
      this_thread::yield();
    for (u32 i = 1; i <= hot; i++)
      if (any_of(&w.rows[i * kWords], &w.rows[i * kWords + kWords],
                 [](u64 x) { return x; }))
        w.state[i] = i, w.touched.push_back(i);
    for (u32 c : w.touched)
      for (u32 k = guards[c]; k < guards[c + 1]; k++)
        w.pending[guarded[k] / 64] |= u64(1) << guarded[k] % 64;
    evaluate(w, base);
    for (u32 c : w.touched)
      w.state[c] = 0;
    w.touched.clear();
    w.rows.resize((hot + 1) * kWords);
    fill(w.rows.begin(), w.rows.end(), 0);
  }

  // Marks the document's terms in block slot `slot` and matches its phrases.
  void process(Worker &w, u32 doc, u32 slot, const char *text, size_t n) {
    if (w.edge.size() < n + 72) {
      w.edge.resize(n + 72), w.anchors.resize(n / 2 + 1), w.keys.resize(n / 2 + 1);
      for (auto *v : {&w.longs, &w.cold})
        v->resize(n / 2 + 1);
    }
    u32 count = edges(text, n, w.edge.data()) / 2, cold = 0, anchors = 0,
        nlong = 0, word = slot / 64, *e = w.edge.data();
    u64 bit = u64(1) << slot % 64, *rows = w.rows.data(), *keys = w.keys.data();
    for (u32 k = 0; k < count; k++) { // Long tokens are keyed afterwards.
      u32 len = e[2 * k + 1] - e[2 * k];
      u64 a;
      memcpy(&a, text + e[2 * k], 8);
      keys[k] = (a | 0x2020202020202020) & ~u64(0) >> (64 - 8 * min(len, 8u));
      w.longs[nlong] = k;
      nlong += len > 8;
    }
    for (u32 i = 0, k; i < nlong; i++)
      k = w.longs[i], keys[k] = key_of(text + e[2 * k], e[2 * k + 1] - e[2 * k]);
    // Keep dictionary prefetches near their use instead of prefetching an
    // entire document's random buckets before the first lookup.
    constexpr u32 lookahead = 8;
    u32 bucket[lookahead];
    for (u32 k = 0; k < min(count, lookahead); k++) {
      bucket[k] = hash_of(keys[k]) & dict.mask();
      _mm_prefetch((const char *)&dict.buckets[bucket[k]], _MM_HINT_T0);
    }
    const u32 hot = this->hot;
    u32 *colds = w.cold.data();
    auto *anchor = w.anchors.data();
    for (u32 k = 0; k < count; k++) { // Keys become metadata words in place.
      u32 at = bucket[k % lookahead];
      if (k + lookahead < count) {
        u32 next = hash_of(keys[k + lookahead]) & dict.mask();
        bucket[k % lookahead] = next;
        _mm_prefetch((const char *)&dict.buckets[next], _MM_HINT_T0);
      }
      keys[k] = dict.find(text + e[2 * k], keys[k], at);
    }
    for (u32 k = 0; k < count; k++) {
      u64 m = keys[k];
      u32 col = m >> 20 & 0xfffff;
      rows[(col & -u32(col <= hot)) * kWords + word] |= bit; // Cold: row 0.
      colds[cold] = col;
      cold += col > hot;
      if (m >> 40)
        anchor[anchors++] = {k, int(m >> 40) - 1};
    }
    for (u32 k = 0; k < cold; k++) {
      u64 &r = w.state[w.cold[k]];
      u32 c = count_of(r);
      if (!r)
        r = kOne | slot, w.touched.push_back(w.cold[k]);
      else if (!c)
        w.rows[r * kWords + word] |= bit;
      else if (at(r, c - 1) != slot && c < kSmall)
        r = (r + kOne) | u64(slot) << (12 * c);
      else if (at(r, c - 1) != slot) {
        u64 row = w.rows.size() / kWords;
        w.rows.resize(w.rows.size() + kWords);
        for (u32 i = 0; i < kSmall; i++)
          w.rows[row * kWords + at(r, i) / 64] |= u64(1) << at(r, i) % 64;
        w.rows[row * kWords + word] |= bit;
        r = row;
      }
    }
    for (u32 a = 0; a < anchors; a++)
      for (int k = w.anchors[a].second; k >= 0; k = phrases[k].next) {
        const Phrase &f = phrases[k];
        u32 p = w.anchors[a].first, j = 0;
        if (w.last[k] == doc || p < f.anchor || p - f.anchor + f.n > count)
          continue;
        while (j < f.n && (keys[p - f.anchor + j] & 0xfffff) == f.term[j])
          j++;
        if (j == f.n)
          w.last[k] = doc, first[f.query].total += (u64(1) << 35) + doc,
          offer(f.query, &doc, 1);
      }
  }

  void write(const string &dir, vector<Worker> &all) {
    vector<string> text(all.size());
    parallel(all.size(), [&](u32 me) {
      for (size_t b = bools.size() * me / all.size();
           b < bools.size() * (me + 1) / all.size(); b++)
        for (const Worker &w : all)
          first[bools[b].query].total += w.totals[b];
    });
    for (Worker &w : all) // Frees per-worker totals before the text grows.
      vector<u64>().swap(w.totals);
    parallel(all.size(), [&](u32 me) {
      for (u32 q = queries * u64(me) / all.size();
           q < queries * u64(me + 1) / all.size(); q++) {
        u64 total = first[q].total;
        char line[256], *p = to_chars(line, line + 24, total >> 35).ptr;
        *p++ = ' ';
        p = to_chars(p, p + 24, total & ((u64(1) << 35) - 1)).ptr;
        for (u32 i = 0; i < first[q].n; i++)
          *p++ = ' ', p = to_chars(p, p + 24, first[q].id[i]).ptr;
        *p++ = '\n';
        text[me].append(line, p);
      }
    });
    mkdir(dir.c_str(), 0755);
    int fd = open((dir + "/results.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    for (const string &s : text)
      if (fd < 0 || ::write(fd, s.data(), s.size()) != ssize_t(s.size()))
        throw runtime_error("cannot write results");
    close(fd);
  }
};

int main(int argc, char **argv) {
  mallopt(M_MMAP_THRESHOLD, 64 << 10); // Freed large buffers leave the heap.
  string data = "/data", out = "/out";
  for (int i = 1; i + 1 < argc; i += 2)
    (strcmp(argv[i], "--data") ? out : data) = argv[i + 1];
  try {
    Engine engine;
    size_t qsize, dsize;
    cpu_set_t cpus;
    bool ok = !sched_getaffinity(0, sizeof cpus, &cpus);
    vector<Worker> workers(ok ? CPU_COUNT(&cpus) : 1);
    const char *queries = map_file(data + "/queries.txt", qsize);
    engine.parse(queries, qsize, workers.size());
    const char *docs = map_file(data + "/docs.txt", dsize);
    engine.plan(docs, dsize);
    engine.scan(docs, dsize, workers);
    engine.planner.join();
    engine.write(out, workers);
  } catch (const exception &e) {
    fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
