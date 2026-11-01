#include "eng.h"
#include <chrono>
static double ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// Branch-free key for tokens up to 12 bytes; longer or digit-bearing 9-12 tokens are flagged.
static inline u64 fast_key(const char *p, u32 n) {
  u64 a, b; memcpy(&a, p, 8); memcpy(&b, p + n - 8 + 8 * (n < 8), 8);
  a |= 0x2020202020202020; b |= 0x2020202020202020;
  u64 shortk = a & (~u64(0) >> (64 - 8 * (n < 8 ? n : 8)));
  u64 packed = 3ull << 62 | _pext_u64(a, 0x1f1f1f1f1f1f1f1f) | _pext_u64(b >> ((16 - n) * 8 & 63), 0x1f1f1f1f1f1f1f1f) << 40;
  bool letters = (a & b & 0x4040404040404040) == 0x4040404040404040;
  return n <= 8 ? shortk : (n <= 12 && letters) ? packed : 0;
}
int main(int c, char **v) {
  size_t n; const char *d = map_file(string(v[1]) + "/docs.txt", n); int mode = atoi(v[2]);
  volatile char sink; for (size_t i = 0; i < n; i += 4096) sink = d[i];
  vector<u32> e(1 << 20); vector<u64> keys(1 << 19); vector<u32> hs(1 << 19), longs(1 << 19);
  u64 acc = 0, cnt = 0; double t = ms();
  for (const char *p = d, *end; p < d + n; p = end + 1) {
    end = (const char *)memchr(p, '\n', d + n - p); const char *text = (const char *)memchr(p, '\t', end - p) + 1; u32 len = end - text;
    if (mode == 0) { // current passes
      u32 count = edges(text, len, e.data()) / 2, nl = 0;
      for (u32 k = 0; k < count; k++) { u32 l = e[2*k+1]-e[2*k]; u64 a; memcpy(&a, text + e[2*k], 8); keys[k] = (a | 0x2020202020202020) & ~u64(0) >> (64 - 8 * min(l, 8u)); longs[nl] = k; nl += l > 8; }
      for (u32 i = 0; i < nl; i++) { u32 k = longs[i]; keys[k] = key_of(text + e[2*k], e[2*k+1]-e[2*k]); }
      for (u32 k = 0; k < count; k++) hs[k] = hash_of(keys[k]);
      cnt += count; acc += hs[count ? count - 1 : 0];
    } else { // fused: transitions straight to keys and hashes
      u32 k = 0, start = 0; u64 carry = 0;
      for (size_t off = 0; off < len; off += 64) {
        u64 m = alnum64(text + off) & (len - off < 64 ? (u64(1) << (len - off)) - 1 : ~0ull);
        u64 tr = m ^ (m << 1 | carry); carry = m >> 63;
        if (tr & 1 && !(m & 1)) { u32 s = start, l = off - s; u64 kk = fast_key(text + s, l); if (__builtin_expect(!kk, 0)) kk = key_of(text + s, l); keys[k] = kk; hs[k++] = hash_of(kk); tr &= tr - 1; }
        while (tr) {
          u32 s = off + countr_zero(tr); tr &= tr - 1;
          if (!tr) { start = s; break; }
          u32 l = off + countr_zero(tr) - s; tr &= tr - 1;
          u64 kk = fast_key(text + s, l); if (__builtin_expect(!kk, 0)) kk = key_of(text + s, l);
          keys[k] = kk; hs[k++] = hash_of(kk);
        }
      }
      if (carry) { u32 s = start, l = len - s; u64 kk = fast_key(text + s, l); if (!kk) kk = key_of(text + s, l); keys[k] = kk; hs[k++] = hash_of(kk); }
      cnt += k; acc += hs[k ? k - 1 : 0];
    }
  }
  fprintf(stderr, "mode %d %.1f ms tokens %lu (%lu)\n", mode, ms() - t, cnt, acc);
}
