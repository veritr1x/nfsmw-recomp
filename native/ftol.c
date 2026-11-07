/* ftol.c - the CRT's _ftol2 (007c4b80), native.
 *
 * Every float-to-integer cast in the game calls it, several hundred thousand
 * times a frame in a race. The original rounds ST(0) with FISTP under the
 * current rounding mode, then steps the 64-bit result one toward zero when
 * that rounding went away from zero, deciding by the sign of the value and of
 * the remainder as floats. This does the same arithmetic through the same
 * x86.h helpers, so EDX:EAX, ECX, the status word and the register file end
 * exactly as the translation leaves them. The original's scratch below the
 * caller's ESP is not written; nothing reads it after the return.
 *
 * RECOMP_NATIVE=0 runs the translation instead. RECOMP_NATIVE_SELFTEST=1
 * compares the two on edge and random values the first time it is called. */
#include "funcs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void native_007c4b80(X86 *c);

static int g_ftol = -1; /* 1 native, 0 translated, -1 unread */
static void ftol_selftest(X86 *c);

static int ftol_on(X86 *c) {
    if (__builtin_expect(g_ftol >= 0, 1))
        return g_ftol;
    const char *e = getenv("RECOMP_NATIVE");
    int want = !(e && e[0] == '0');
    const char *t = getenv("RECOMP_NATIVE_SELFTEST");
    if (t && t[0] == '1')
        ftol_selftest(c);
    g_ftol = want;
    return g_ftol;
}

static uint32_t float_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* Value in ST(0); returns EDX:EAX and pops it. */
void native_007c4b80(X86 *c) {
    if (!ftol_on(c)) {
        fn_007c4b80(c);
        return;
    }
    const unsigned top = c->fpu_top & 7u, above = (top - 1u) & 7u;
    const double v = c->st[top];
    /* FLD ST0 copies the value with its exact-integer metadata, so FISTP of
     * the copy is FISTP of ST(0). FST float [ESP+18h] keeps its bits. */
    const uint32_t vf = float_bits(fto_float(c, v));
    const uint64_t r = (uint64_t)fist_i64(c);
    const double ri = (double)(int64_t)r; /* FILD qword */
    uint32_t eax = (uint32_t)r, edx = (uint32_t)(r >> 32), ecx = c->r[R_ECX];
    /* The integer FILD pushed sits above the value: popped, with its bits. */
    c->st[above] = ri;
    c->st_bits[above] = r;
    c->st_exact[above] = 0;
    if (eax == 0 && (edx & 0x7fffffffu) == 0) {
        /* Zero, or the integer indefinite: both popped unchanged. */
    } else {
        /* FSUBP leaves value - integer in the value's register; FSTP float. */
        const double diff = fx87(c, v - ri);
        c->st[top] = diff;
        c->st_bits[top] = 0;
        const uint32_t d = float_bits(fto_float(c, diff));
        /* The sign test reads the value's float bits, or the high dword
         * when the low one was zero. */
        const uint32_t sign = eax == 0 ? edx : vf;
        if ((int32_t)sign < 0) {
            /* XOR ECX,80000000h; ADD ECX,7FFFFFFFh carries for a positive
             * remainder: the value rounded down, so ADC adds one. */
            const uint64_t s = (uint64_t)(d ^ 0x80000000u) + 0x7fffffffu;
            ecx = (uint32_t)s;
            const uint64_t lo = (uint64_t)eax + (uint32_t)(s >> 32);
            eax = (uint32_t)lo;
            edx += (uint32_t)(lo >> 32);
        } else {
            /* ADD ECX,7FFFFFFFh carries for a negative remainder: the value
             * rounded up, so SBB takes one away. */
            const uint64_t s = (uint64_t)d + 0x7fffffffu;
            ecx = (uint32_t)s;
            const uint32_t borrow = (uint32_t)(s >> 32);
            const uint32_t b2 = eax < borrow;
            eax -= borrow;
            edx -= b2;
        }
    }
    c->st_exact[top] = 0;
    ftag_put(c, top, FTAG_EMPTY);
    ftag_put(c, above, FTAG_EMPTY);
    c->fpu_top = (top + 1u) & 7u;
    c->r[R_EAX] = eax;
    c->r[R_EDX] = edx;
    c->r[R_ECX] = ecx;
    c->eip = rd32(c->r[R_ESP]);
    c->r[R_ESP] += 4;
}

/* ---- self-test ------------------------------------------------------------ */

static uint64_t rng_state = 0x243f6a8885a308d3ull;
static uint64_t rnd64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* The first field in which two runs differ, or NULL. */
static const char *state_diff(const X86 *a, const X86 *b) {
    static const char *const regs[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    for (int i = 0; i < 8; ++i)
        if (a->r[i] != b->r[i])
            return regs[i];
    if (a->eip != b->eip)
        return "eip";
    if (a->fpu_top != b->fpu_top || a->fpu_sw != b->fpu_sw || a->fpu_tag != b->fpu_tag)
        return "fpu words";
    if (memcmp(a->st, b->st, sizeof a->st))
        return "st";
    if (memcmp(a->st_bits, b->st_bits, sizeof a->st_bits))
        return "st_bits";
    if (memcmp(a->st_exact, b->st_exact, sizeof a->st_exact))
        return "st_exact";
    return NULL;
}

static int ftol_case(X86 *c, double value, int exact, uint64_t bits, uint32_t stack) {
    /* A real continuation of a call to 007c4b80 (in 00642e60), so the
     * translation's RET returns to its caller as in the game. */
    const uint32_t ret = 0x00642ed1u, len = 0x1000;
    X86 s = *c;
    s.r[R_ESP] = stack;
    s.r[R_ECX] = (uint32_t)rnd64();
    s.fpu_top = (uint32_t)(rnd64() & 7u);
    s.fpu_tag = 0xffffu;
    s.fpu_sw = 0;
    if (exact)
        fpush_int(&s, (int64_t)bits);
    else
        fpush(&s, value);
    wr32(stack, ret);
    uint8_t *snapshot = malloc(len);
    for (uint32_t i = 0; i < len; ++i)
        snapshot[i] = rd8(stack - len + 16 + i);
    X86 a = s, b = s;
    g_ftol = 0;
    fn_007c4b80(&a);
    for (uint32_t i = 0; i < len; ++i)
        wr8(stack - len + 16 + i, snapshot[i]);
    g_ftol = 1;
    native_007c4b80(&b);
    free(snapshot);
    const char *what = state_diff(&a, &b);
    if (!what)
        return 0;
    fprintf(stderr,
            "native self-test 007c4b80 %.17g%s differs in %s: eax %08x/%08x edx %08x/%08x ecx %08x/%08x "
            "sw %04x/%04x tag %04x/%04x top %u/%u\n",
            value, exact ? " (exact)" : "", what, a.r[R_EAX], b.r[R_EAX], a.r[R_EDX], b.r[R_EDX], a.r[R_ECX],
            b.r[R_ECX], a.fpu_sw, b.fpu_sw, a.fpu_tag, b.fpu_tag, a.fpu_top, b.fpu_top);
    return 1;
}

static void ftol_selftest(X86 *c) {
    static const double edges[] = {0.0,        -0.0,        0.5,         -0.5,        1.5,         -1.5,
                                   2.5,        -2.5,        0.49999997,  -0.49999997, 1e-30,       -1e-30,
                                   2147483647, -2147483648, 2147483648., 4294967296.,  -4294967296., 1e19,
                                   -1e19,      9.2233720368547758e18,  -9.2233720368547758e18, 123456789.75,
                                   -123456789.75};
    const uint32_t stack = c->r[R_ESP] - 0x20000;
    int bad = 0, runs = 0;
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; ++i, ++runs)
        bad += ftol_case(c, edges[i], 0, 0, stack);
    const double specials[] = {INFINITY, -INFINITY, NAN};
    for (size_t i = 0; i < 3; ++i, ++runs)
        bad += ftol_case(c, specials[i], 0, 0, stack);
    for (int trial = 0; trial < 4000; ++trial, ++runs) {
        const uint64_t u = rnd64();
        double v;
        switch (trial % 4) {
        case 0: /* small magnitudes with fractions, as game code casts */
            v = ((double)(int64_t)(u >> 20) / 4096.0) * ((u & 1) ? -1 : 1) / 65536.0;
            break;
        case 1: /* any finite double */
            memcpy(&v, &u, 8);
            if (!isfinite(v))
                v = 1.0;
            break;
        case 2: /* floats, as most callers hold */
            v = (double)(float)((double)(int32_t)u / 64.0);
            break;
        default: /* exact 64-bit integers from FILD */
            ++runs;
            bad += ftol_case(c, 0, 1, u, stack);
            v = (double)(int64_t)(u >> 11);
            break;
        }
        bad += ftol_case(c, v, 0, 0, stack);
    }
    fprintf(stderr, "native self-test 007c4b80: %d runs, %d differ\n", runs, bad);
}
