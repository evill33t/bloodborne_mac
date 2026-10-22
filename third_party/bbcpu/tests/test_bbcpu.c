/* tests/test_bbcpu.c: differential test of the bbcpu interpreter (x86-64 hosts). Functions in
 * the __TEXT,__bbtest section are "guest code": each runs natively and through bbcpu with random
 * inputs, and the results must match. Calls they make to libc (outside the section) go through
 * the host call bridge; qsort calling back into a guest comparator exercises nested calls. */
#include "../src/cpu/bbcpu.h"
#include <immintrin.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

uint64_t bb_image_base; /* platform.c in the game: fault reports */

void *runtime_low_map(size_t size, int prot) {
    void *p = mmap(NULL, size, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

#define GUEST __attribute__((section("__TEXT,__bbtest,regular,pure_instructions"), noinline, used))

GUEST uint64_t t_arith(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t r = a * 0x9E3779B97F4A7C15ull + (b >> (c & 63)) - (a << (b & 31));
    r ^= (r >> 17) | (a << 7);
    r += (uint64_t)((int64_t)b / ((int64_t)(c | 1)));
    r += b % (a | 1);
    r += (uint64_t)__builtin_popcountll(a) + (uint64_t)__builtin_ctzll(b | 1) + (uint64_t)__builtin_clzll(c | 1);
    r += (uint32_t)a * (uint32_t)b;
    r += (uint64_t)(int16_t)(a >> 3) * (uint64_t)(int8_t)b;
    r += a > b ? a - b : b - a;
    r += (int64_t)a < (int64_t)c ? 7 : 13;
    unsigned __int128 wide = (unsigned __int128)a * b;
    r += (uint64_t)(wide >> 64);
    r += __builtin_bswap64(c) + __builtin_bswap32((uint32_t)a);
    r += (a >> 61) | (a << 3); /* rotate */
    return r;
}

GUEST uint64_t t_flags(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t r = a, carry = 0, out;
    __asm__ volatile("add %[b], %[r]\n\tadc %[c], %[r]\n\tsbb %[a], %[r]\n\tsetc %b[carry]\n\t"
                     : [r] "+r"(r), [carry] "+q"(carry)
                     : [a] "r"(a), [b] "r"(b), [c] "r"(c)
                     : "cc");
    out = r + carry;
    uint64_t x = a, y = b;
    __asm__ volatile("shld %%cl, %[y], %[x]\n\trcl $3, %[x]\n\trcr $5, %[y]\n\tbtc %[c], %[x]\n\t"
                     "pushfq\n\tpop %[c2]\n\t"
                     : [x] "+r"(x), [y] "+r"(y), [c2] "=r"(carry)
                     : [c] "r"(c & 63), "c"(c)
                     : "cc");
    out += x ^ (y << 1) ^ (carry & 0x1); /* only CF is defined after btc */
    uint32_t lo = (uint32_t)a;
    __asm__ volatile("neg %[v]\n\tsbb %[v], %[v]\n\t" : [v] "+r"(lo) : : "cc");
    out += lo;
    int64_t sa = (int64_t)a;
    out += (uint64_t)(sa >> (b & 63)) + (uint64_t)((int32_t)sa >> 5);
    return out;
}

GUEST uint64_t t_vector(uint64_t a, uint64_t b, uint64_t c) {
    float fa = (float)(a & 0xffff) / 7.0f, fb = (float)(b & 0xffff) / 3.0f, fc = (float)(c & 0xff) - 100.0f;
    __m128 x = _mm_set_ps(fa, fb, fc, fa - fb);
    __m128 y = _mm_set_ps(fc, fa, fb * 2.0f, 0.5f);
    __m128 z = _mm_add_ps(_mm_mul_ps(x, y), _mm_shuffle_ps(x, y, 0x1b));
    z = _mm_max_ps(z, _mm_min_ps(x, y));
    z = _mm_div_ps(z, _mm_add_ps(_mm_set1_ps(1.5f), _mm_and_ps(x, _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff)))));
    z = _mm_sqrt_ps(_mm_and_ps(z, _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff))));
    __m128 cmp = _mm_cmplt_ps(x, y);
    z = _mm_blendv_ps(z, x, cmp);
    __m128 dp = _mm_dp_ps(x, y, 0xf1);
    __m128i i = _mm_cvttps_epi32(_mm_mul_ps(z, _mm_set1_ps(1000.0f)));
    i = _mm_add_epi32(i, _mm_shuffle_epi32(i, 0x4e));
    i = _mm_mullo_epi32(i, _mm_set_epi32((int)a, (int)b, (int)c, 3));
    i = _mm_xor_si128(i, _mm_srli_epi64(i, 13));
    i = _mm_unpacklo_epi16(i, _mm_set1_epi16((short)b));
    __m128i bytes = _mm_shuffle_epi8(i, _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, (char)0x80, 15));
    __m256 w = _mm256_set_ps(fa, fb, fc, 1.0f, 2.0f, fa * fb, -fc, 0.25f);
    w = _mm256_mul_ps(w, _mm256_permute_ps(w, 0x93));
    w = _mm256_add_ps(w, _mm256_permute2f128_ps(w, w, 0x01));
    __m128 lo = _mm256_castps256_ps128(w), hi = _mm256_extractf128_ps(w, 1);
    float out[4];
    _mm_storeu_ps(out, _mm_hadd_ps(lo, hi));
    uint64_t r = (uint64_t)_mm_extract_epi64(bytes, 0) ^ (uint64_t)_mm_extract_epi64(i, 1);
    r += (uint64_t)_mm_movemask_ps(cmp) + (uint64_t)_mm_movemask_epi8(bytes);
    r += (uint64_t)(int64_t)(_mm_cvtss_f32(dp) * 16.0f);
    r += (uint64_t)(int64_t)((out[0] + out[1] * 3.0f + out[2] * 5.0f + out[3] * 7.0f) * 8.0f);
    double d = (double)a / (double)(b | 1) + sqrt((double)(c & 0xfffff));
    r += (uint64_t)(int64_t)(d * 1024.0) + (uint64_t)lrint((double)fc * 1.75);
    return r;
}

GUEST uint64_t t_string(uint64_t a, uint64_t b, uint64_t c) {
    char buf[256], out[256];
    for (int k = 0; k < 256; ++k) buf[k] = (char)(a >> (k & 63)) ^ (char)k;
    size_t n = (size_t)(b & 255);
    void *d = out, *s = buf;
    size_t cnt = n;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(cnt) : : "memory");
    memset(out + n, (int)(c & 0xff), 256 - n);
    uint64_t r = 0;
    for (int k = 0; k < 256; k += 8) { uint64_t v; memcpy(&v, out + k, 8); r = r * 31 + v; }
    char text[64];
    snprintf(text, sizeof(text), "%llu-%x", (unsigned long long)(a % 100000), (unsigned)(b & 0xfff));
    r += strlen(text) * 977 + (uint64_t)strtoul(text, NULL, 10);
    return r;
}

GUEST static uint64_t fib(uint64_t n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
GUEST uint64_t t_control(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t r = 0;
    for (uint64_t k = 0; k < (a & 31) + 10; ++k) {
        switch ((k + b) & 7) { /* a jump table */
        case 0: r += k * 3; break;
        case 1: r ^= k << 4; break;
        case 2: r -= c; break;
        case 3: r = r * 5 + 1; break;
        case 4: r += fib(k & 15); break;
        case 5: r ^= r >> 11; break;
        case 6: r += (uint64_t)(int64_t)(int32_t)k; break;
        default: r -= 77; break;
        }
    }
    return r;
}

GUEST static int compare_u32(const void *x, const void *y) {
    const uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return a < b ? -1 : a > b;
}
GUEST uint64_t t_callback(uint64_t a, uint64_t b, uint64_t c) {
    uint32_t v[64];
    for (int k = 0; k < 64; ++k) v[k] = (uint32_t)(a * (uint64_t)(k + 1) ^ (b >> (k & 31)) ^ c);
    qsort(v, 64, sizeof(v[0]), compare_u32); /* host calling the guest comparator */
    uint64_t r = 0;
    for (int k = 0; k < 64; ++k) r = r * 1315423911u + v[k];
    return r;
}

GUEST uint64_t t_x87(uint64_t a, uint64_t b, uint64_t c) {
    long double x = (long double)(a & 0xfffff) / 3.0L, y = (long double)(b & 0xffff) + 0.5L;
    long double z = x * y - (long double)(c & 0xff);
    double out = (double)z;
    return (uint64_t)(int64_t)(out * 64.0);
}

typedef uint64_t (*Test)(uint64_t, uint64_t, uint64_t);

static uint64_t rng = 0x123456789abcdef1ull;
static uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(void) {
    unsigned long size = 0;
    const uint8_t *section = getsectiondata(&_mh_execute_header, "__TEXT", "__bbtest", &size);
    if (!section) { puts("no __bbtest section"); return 1; }
    bbcpu_add_guest_code((uintptr_t)section, size);
    const struct { const char *name; Test fn; } tests[] = {
        {"arith", t_arith}, {"flags", t_flags}, {"vector", t_vector}, {"string", t_string},
        {"control", t_control}, {"callback", t_callback}, {"x87", t_x87},
    };
    int failures = 0;
    for (size_t t = 0; t < sizeof(tests) / sizeof(*tests); ++t) {
        int bad = 0;
        for (int round = 0; round < 2000 && bad < 3; ++round) {
            uint64_t args[3] = {next(), next(), next()};
            if (round % 4 == 1) args[1] &= 0xff;
            if (round % 4 == 2) args[0] = 0;
            const uint64_t want = tests[t].fn(args[0], args[1], args[2]);
            const uint64_t got = bbcpu_call((uintptr_t)tests[t].fn, 3, args);
            /* x87 values are doubles in bbcpu, not 80-bit: the last bits may differ. */
            const int close = tests[t].fn == t_x87 && (want - got + 2) <= 4;
            if (want != got && !close) {
                printf("FAIL %s(%#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 "): native %#" PRIx64 ", bbcpu %#" PRIx64 "\n",
                       tests[t].name, args[0], args[1], args[2], want, got);
                ++bad;
            }
        }
        printf("%-8s %s\n", tests[t].name, bad ? "FAILED" : "ok");
        failures += bad != 0;
    }
    bbcpu_report();
    return failures != 0;
}
