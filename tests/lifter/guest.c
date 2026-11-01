// Test program for the lifter: compiled once for AArch64 (lifted) and once for the host (reference).
// Every test takes three integers and returns one, so the harness can call both versions the same way.
// Freestanding: no libc, so the AArch64 build needs nothing but this file.
#include <stdint.h>

#ifdef NATIVE
#define T(name) nat_##name
#else
#define T(name) name
#endif

typedef uint64_t u64;
typedef int64_t i64;

static u64 bits_of(double d) { union { double d; u64 u; } x = { .d = d }; return x.u; }
static double dbl_of(u64 u) { union { double d; u64 u; } x = { .u = u }; return x.d; }
// Results C leaves undefined differ between AArch64 and x86 by design (the lifter follows AArch64), so the tests
// only compare well-defined values: one canonical NaN, and float-to-integer conversions clamped into range.
static u64 canon(double d) { return d != d ? 0x7ff8000000000000ull : bits_of(d); }
static i64 to_i64(double d) { return d != d ? 0 : d >= 9.2e18 ? INT64_MAX : d <= -9.2e18 ? INT64_MIN : (i64)d; }

u64 T(t_arith)(u64 a, u64 b, u64 c) { return a * 3 + (b ^ c) - (a >> 7) + (u64)((i64)b >> 3); }
u64 T(t_divide)(u64 a, u64 b, u64 c) {
    int sdiv_ok = c && !((i64)a == INT64_MIN && (i64)c == -1);   // that one quotient overflows (traps on x86)
    return (b ? a / b : 0) + (sdiv_ok ? (u64)((i64)a / (i64)c) : 0) + (b ? a % b : 0);
}
u64 T(t_narrow)(u64 a, u64 b, u64 c) { uint32_t x = (uint32_t)a, y = (uint32_t)b; return (u64)(x * y + (x >> (c & 31))) + (int16_t)a; }
u64 T(t_bits)(u64 a, u64 b, u64 c) {
    return (u64)__builtin_popcountll(a) + (a ? (u64)__builtin_clzll(a) : 64) + (b ? (u64)__builtin_ctzll(b) : 64)
         + __builtin_bswap64(c) + ((a << 13) | (a >> 51));
}
u64 T(t_compare)(u64 a, u64 b, u64 c) {
    u64 r = 0;
    if ((i64)a < (i64)b) r |= 1;
    if (a < b) r |= 2;
    if (a == c) r |= 4;
    r += (i64)a > 0 ? a : b;
    return r + ((a & 0xF0) ? c : ~c);
}
u64 T(t_loop)(u64 a, u64 b, u64 c) {
    u64 s = 0;
    for (u64 i = 0; i < (a & 1023); i++) s += i * b + (s >> 3) + c;
    return s;
}

__attribute__((noinline)) static u64 fib(u64 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
u64 T(t_recursion)(u64 a, u64 b, u64 c) { (void)b; (void)c; return fib(a % 24); }

static volatile u64 sink;
// Dense cases with bodies that differ in shape: compiled to a jump table and a `br`, which the lifter must resolve.
u64 T(t_switch)(u64 a, u64 b, u64 c) {
    u64 r;
    switch ((a ^ (a >> 5)) % 16) {
    case 0: r = b + 1; sink = r; break;
    case 1: r = b * c + 7; sink = r ^ 1; break;
    case 2: r = b - c; sink = r + 2; break;
    case 3: r = b ^ 0x55; sink = r * 3; break;
    case 4: r = c << 3; sink = r - 4; break;
    case 5: r = b / (c | 1); sink = r | 5; break;
    case 6: r = 6000 + c; sink = r & 6; break;
    case 7: r = b & c; sink = ~r; break;
    case 8: r = b | c; sink = r >> 8; break;
    case 9: r = ~b; sink = r << 9; break;
    case 10: r = c >> 2; sink = r + 10; break;
    case 11: r = b * b; sink = r - 11; break;
    case 12: r = c * 13 + b; sink = r ^ 12; break;
    case 13: r = (b << 13) | (c >> 51); sink = r + 13; break;
    case 14: r = b % (c | 3); sink = r * 14; break;
    default: r = 42 + (b >> 1); sink = r; break;
    }
    return r + sink;
}

__attribute__((noinline)) static u64 op_add(u64 x, u64 y) { return x + y; }
__attribute__((noinline)) static u64 op_sub(u64 x, u64 y) { return x - y; }
__attribute__((noinline)) static u64 op_mul(u64 x, u64 y) { return x * y; }
__attribute__((noinline)) static u64 op_xor(u64 x, u64 y) { return x ^ y; }
// A table of function pointers in .data: needs R_AARCH64_RELATIVE relocations and indirect calls (BLR).
static u64 (*volatile const ops[4])(u64, u64) = { op_add, op_sub, op_mul, op_xor };
u64 T(t_fnptr)(u64 a, u64 b, u64 c) { return ops[a & 3](b, c) + ops[(a >> 2) & 3](c, b); }

u64 T(t_double)(u64 a, u64 b, u64 c) {
    double x = (double)(i64)a / 7.0, y = (double)(b & 0xFFFFF) + 0.25;
    double r = x * y - (double)c / 3.0;
    if (r < 0) r = -r;
    return canon(r + (r > 1e6 ? 1.0 : r * r)) ^ (u64)to_i64(x + 0.5);
}
u64 T(t_float)(u64 a, u64 b, u64 c) {
    float x = (float)(uint32_t)a * 0.5f, y = (float)(int32_t)b;
    return (u64)to_i64((double)(x * y + (float)c)) + (u64)(x > y);
}
u64 T(t_fpbits)(u64 a, u64 b, u64 c) {
    // Operations on bit patterns: special values (NaN, infinities, denormals, signed zeros) come from the inputs.
    double x = dbl_of(a), y = dbl_of(b);
    double s = x + y, m = x * y, d = y != 0 ? x / y : 0, q = (double)(i64)c * 0.125;
    return canon(s) ^ (canon(m) << 1) ^ (canon(d) >> 1) ^ canon(q - x) ^ (u64)(x < y) ^ ((u64)(x == y) << 1);
}

static int32_t va[64], vb[64];
static float fa[64];
u64 T(t_vector)(u64 a, u64 b, u64 c) {
    for (int i = 0; i < 64; i++) { va[i] = (int32_t)(a * i + c); vb[i] = (int32_t)(b ^ (u64)i); fa[i] = (float)(i * (int)(c & 7)); }
    int64_t dot = 0;
    for (int i = 0; i < 64; i++) dot += (int64_t)va[i] * vb[i];
    int32_t mx = va[0];
    for (int i = 1; i < 64; i++) mx = va[i] > mx ? va[i] : mx;
    float fs = 0;
    for (int i = 0; i < 64; i++) fs += fa[i] * 0.5f;
    return (u64)dot + (u64)(int64_t)mx + (u64)to_i64(fs);
}

static u64 counter;
u64 T(t_atomic)(u64 a, u64 b, u64 c) {
    counter = c;
    for (u64 i = 0; i < (a & 63); i++) __atomic_fetch_add(&counter, b, __ATOMIC_SEQ_CST);
    u64 expected = counter;
    __atomic_compare_exchange_n(&counter, &expected, expected ^ 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return __atomic_exchange_n(&counter, a, __ATOMIC_ACQ_REL) + counter;
}

u64 T(t_sort)(u64 a, u64 b, u64 c) {
    uint16_t v[24];
    u64 s = a ^ (b << 1) ^ c;
    for (int i = 0; i < 24; i++) { s = s * 6364136223846793005ull + 1442695040888963407ull; v[i] = (uint16_t)(s >> 48); }
    for (int i = 1; i < 24; i++) { uint16_t k = v[i]; int j = i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
    u64 h = 0;
    for (int i = 0; i < 24; i++) h = h * 31 + v[i];
    return h;
}

u64 T(t_mulhi)(u64 a, u64 b, u64 c) {
    __uint128_t p = (__uint128_t)a * b;
    __int128 q = (__int128)(i64)a * (i64)c;
    return (u64)(p >> 64) ^ (u64)p ^ (u64)(q >> 64) ^ (u64)((p >> 64) > (__uint128_t)c);
}
