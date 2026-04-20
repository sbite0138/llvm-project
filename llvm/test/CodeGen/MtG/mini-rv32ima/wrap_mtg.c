/* MtG 32-bit wrap regression test (Epic 3-4).
 *
 * Unlike the other harnesses in this directory, this file does NOT run
 * the mini-rv32ima emulator. It runs directly on MtG/ursa and checks
 * the backend's 32-bit wrap contract for ADD / SUB / MUL / NEG / SHL /
 * signed compare. Every wrapping op in MtG is lowered to an MtG
 * arithmetic op (which is arbitrary precision) followed by a REM-by-2^32
 * in post-RA expansion; if that REM is ever dropped or the 2^32 constant
 * leaks into a spillable vreg, this test catches it.
 *
 * Each test that passes emits one character; mismatch emits '!' and
 * halts (empty infinite loop — ursa returns after reaching max steps
 * or output stops). Successful run prints "ABCDEFGHIJKL\n".
 *
 * Manual invocation:
 *   build/bin/clang --target=mtg -O1 -nostdlib -ffreestanding -S \
 *       llvm/test/CodeGen/MtG/mini-rv32ima/wrap_mtg.c -o /tmp/t.s \
 *       -I build/lib/clang/23/include
 *   python3 ursa/tools/mtg-link.py /tmp/t.s -o /tmp/linked.s
 *   python3 ursa/src/main.py /tmp/linked.s   # expect "ABCDEFGHIJKL\n"
 */

#include <mtg.h>

typedef unsigned int   uint32_t;
typedef int            int32_t;

static void put_char(int c) {
    __mtg_output(c);
}

/* No SYSCON halt here — we aren't in the mini-rv32ima harness. ursa
   naturally halts when _start returns (the Return instruction pops an
   empty return stack). On the first CHECK failure we set `failed` so
   subsequent CHECKs are skipped; _start still returns cleanly so the
   accumulated output gets flushed. */
static int failed;

static void check_or_fail(int cond, int ch) {
    if (failed) return;
    if (cond) put_char(ch);
    else { put_char('!'); failed = 1; }
}

#define CHECK(cond, ch) check_or_fail(cond, ch)

/* noinline + volatile-loaded inputs force MtG to emit the real op with
   real register traffic; without this the constant folder would
   collapse everything to the expected value at compile time. */
__attribute__((noinline))
static uint32_t uadd(uint32_t a, uint32_t b) { return a + b; }
__attribute__((noinline))
static uint32_t usub(uint32_t a, uint32_t b) { return a - b; }
__attribute__((noinline))
static uint32_t umul(uint32_t a, uint32_t b) { return a * b; }
__attribute__((noinline))
static uint32_t uneg(uint32_t a)             { return -a; }
__attribute__((noinline))
static uint32_t ushl(uint32_t a, uint32_t n) { return a << n; }
__attribute__((noinline))
static int      islt(int32_t a, int32_t b)   { return a < b ? 1 : 0; }

void _start(void) {
    /* ursa treats uninitialized BSS reads as errors, so zero the
       first-failure sentinel before any CHECK runs. */
    failed = 0;

    /* A: ADD overflow by 1. (uint32)(-1) + 1 must wrap to 0. */
    {
        volatile uint32_t a = 0xFFFFFFFFu, b = 1u;
        CHECK(uadd(a, b) == 0u, 'A');
    }
    /* B: ADD overflow at 2^31 + 2^31. */
    {
        volatile uint32_t a = 0x80000000u, b = 0x80000000u;
        CHECK(uadd(a, b) == 0u, 'B');
    }
    /* C: SUB underflow: 0 - 1 = 2^32 - 1. */
    {
        volatile uint32_t a = 0u, b = 1u;
        CHECK(usub(a, b) == 0xFFFFFFFFu, 'C');
    }
    /* D: SUB at INT_MIN boundary: 2^31 - 1 = 2^31 - 1 (no wrap). */
    {
        volatile uint32_t a = 0x80000000u, b = 1u;
        CHECK(usub(a, b) == 0x7FFFFFFFu, 'D');
    }
    /* E: MUL truncation: 2^16 * 2^16 = 2^32 → wraps to 0. */
    {
        volatile uint32_t a = 0x10000u, b = 0x10000u;
        CHECK(umul(a, b) == 0u, 'E');
    }
    /* F: MUL of (-1) * (-1) in unsigned: (2^32 - 1)^2 mod 2^32 = 1. */
    {
        volatile uint32_t a = 0xFFFFFFFFu, b = 0xFFFFFFFFu;
        CHECK(umul(a, b) == 1u, 'F');
    }
    /* G: NEG of INT_MIN: -(-2^31) overflows signed, but as uint32
          the two's-complement result is INT_MIN again. */
    {
        volatile uint32_t a = 0x80000000u;
        CHECK(uneg(a) == 0x80000000u, 'G');
    }
    /* H: NEG of 1 → 0xFFFFFFFF. */
    {
        volatile uint32_t a = 1u;
        CHECK(uneg(a) == 0xFFFFFFFFu, 'H');
    }
    /* I: SHL: 0x80000000 << 1 must wrap to 0. */
    {
        volatile uint32_t a = 0x80000000u, n = 1u;
        CHECK(ushl(a, n) == 0u, 'I');
    }
    /* J: SHL: 1 << 31 = 0x80000000. */
    {
        volatile uint32_t a = 1u, n = 31u;
        CHECK(ushl(a, n) == 0x80000000u, 'J');
    }
    /* K: ADD where the second operand already is (uint32)(-1). Exercise
          the "result straddles 2^32" path with a different operand
          shape than test A (important because ADD_MACRO uses a
          different vreg layout when the first addend is a constant). */
    {
        volatile uint32_t a = 0u, b = 0xFFFFFFFFu;
        CHECK(uadd(a, b) == 0xFFFFFFFFu, 'K');
    }
    /* L: Signed SLT at the INT_MIN/0 boundary. The MtG signed compare
          uses bit31 peel (Epic 1c); this is the most sensitive case. */
    {
        volatile int32_t a = (int32_t)0x80000000, b = 0;
        CHECK(islt(a, b) == 1, 'L');
    }

    if (!failed)
        put_char('\n');
    /* fall through — ursa halts on Return with empty return stack */
}
