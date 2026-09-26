/*
 * lib32.c - the few 32-bit arithmetic helpers a freestanding -m68000
 * link may otherwise pull from libgcc.
 *
 * The 68000 can only multiply or divide 16x16 in hardware, so gcc
 * lowers 32-bit '*', '/' and '%' to __mulsi3 / __udivsi3 / __divsi3 /
 * __umodsi3 / __modsi3 calls.  We do not link libgcc (ROM size and
 * control), so provide them here: shift/add multiply and the same
 * shift-subtract division used elsewhere in the boot ROM.
 */

typedef unsigned int u32;
typedef int s32;

/* 32x32 -> 32, shift/add; sign recovered symmetrically. */
int __mulsi3(int a, int b)
{
    u32 u = 0;
    int neg = 0;

    if (a < 0) { a = -a; neg = !neg; }
    if (b < 0) { b = -b; neg = !neg; }
    while (b != 0) {
        if (b & 1)
            u += (u32)a;
        a += a;
        b >>= 1;
    }
    return neg ? -(int)u : (int)u;
}

static u32 udivmod(u32 n, u32 d, u32 *rem)
{
    u32 q = 0, s = d;
    int sh = 0;

    if (d == 0) {
        *rem = n;
        return 0;
    }
    while (s <= n && sh < 31) {
        s <<= 1;
        sh++;
    }
    for (;;) {
        q <<= 1;
        if (n >= s) {
            n -= s;
            q |= 1u;
        }
        if (sh == 0)
            break;
        s >>= 1;
        sh--;
    }
    *rem = n;
    return q;
}

u32 __udivsi3(u32 a, u32 b)
{
    u32 rem;
    return udivmod(a, b, &rem);
}

u32 __umodsi3(u32 a, u32 b)
{
    u32 rem;
    (void)udivmod(a, b, &rem);
    return rem;
}

s32 __divsi3(s32 a, s32 b)
{
    int neg = 0;
    u32 rem, q;

    if (a < 0) { a = -a; neg = !neg; }
    if (b < 0) { b = -b; neg = !neg; }
    q = udivmod((u32)a, (u32)b, &rem);
    return neg ? -(s32)q : (s32)q;
}

s32 __modsi3(s32 a, s32 b)
{
    int neg = 0;
    u32 rem;

    if (a < 0) { a = -a; neg = !neg; }
    if (b < 0) { b = -b; neg = !neg; }
    (void)udivmod((u32)a, (u32)b, &rem);
    return neg ? -(s32)rem : (s32)rem;
}
