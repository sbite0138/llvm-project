/* MtG ✕ mini-rv32ima — Zicsr / RV32A / privileged-instruction tests.
 *
 * Companion to rv32m_rv32.c. Boots a guest that exercises CSRRW/CSRRS/
 * CSRRC/CSRRWI on mscratch (A-C), an ECALL → mtvec handler → MRET cycle
 * (D), every AMO operation (E-I), LR/SC success and failure (J-K), and
 * a WFI woken by a CLINT timer match (L). Each test that passes emits
 * one character; mismatch emits '!' and halts via SYSCON. Successful
 * run output:
 *
 *   ABCDEFGHIJKL\n
 *
 * The harness extends the usual MMIO surface with a tiny CLINT shim:
 *     0x02004000  mtimecmpl
 *     0x02004004  mtimecmph
 *     0x0200BFF8  mtimel  (read-only mirror of core->timerl)
 *     0x0200BFFC  mtimeh  (read-only mirror of core->timerh)
 * Stores into mtimecmp* go directly into core->timermatch* so
 * mini-rv32ima's existing timer-match path will fire MTIP and clear
 * WFI on a subsequent step. No MtG ISA changes; this is host-side
 * MMIO emulation only.
 *
 * STATUS:
 *   -O0 PASS  (full "ABCDEFGHIJKL" — all 12 RV32A/Zicsr/priv tests)
 *   -O1 FAIL  (Jump out of bounds in MtG-compiled mini-rv32ima.h CSR
 *              switch; minimum repro is the very first `csrw mtvec`
 *              after main_test starts. mtg_rv32.c / rv32m_rv32.c don't
 *              hit the CSR path so they pass at -O1; this harness is
 *              the first guest that does. Tracked as a follow-up Epic
 *              for the MtG -O1 codegen.)
 *
 * Manual invocation (use -O0 until the -O1 codegen issue is resolved):
 *   build/bin/clang --target=mtg -O0 -nostdlib -ffreestanding -S \
 *       llvm/test/CodeGen/MtG/mini-rv32ima/rv32priv_rv32.c -o /tmp/t.s \
 *       -I build/lib/clang/23/include
 *   python3 ursa/tools/mtg-link.py /tmp/t.s -o /tmp/linked.s
 *   python3 ursa/src/main.py /tmp/linked.s   # expect "ABCDEFGHIJKL\n"
 *
 * Regenerating the embedded binary (kept out of git; do by hand when
 * tiny_rv32priv.c below changes):
 *
 *   /usr/bin/clang --target=riscv32 -march=rv32ima_zicsr -mabi=ilp32 \
 *       -nostdlib -ffreestanding -fno-pic -Os \
 *       -Wl,-Ttext=0x80000000 -Wl,--no-relax -fuse-ld=lld \
 *       /tmp/tiny_rv32priv.c -o /tmp/tiny_rv32priv.elf
 *   /usr/bin/llvm-objcopy -O binary /tmp/tiny_rv32priv.elf /tmp/tiny_rv32priv.bin
 *   od -An -tx4 -v /tmp/tiny_rv32priv.bin
 *
 * The guest source (tiny_rv32priv.c) lives outside the tree; reproduce
 * it from the project's working notes when regenerating.
 */

#include <mtg.h>

typedef unsigned int   uint32_t;
typedef int            int32_t;
typedef unsigned short uint16_t;
typedef short          int16_t;
typedef unsigned char  uint8_t;
typedef signed char    int8_t;
typedef long long      int64_t;
typedef unsigned long long uint64_t;

#define NULL ((void*)0)
#define INT32_MIN (-2147483647 - 1)

/* 8 KB: tiny_rom (~1.2 KB) + guest stack/bss + MiniRV32IMAState (~196 B).
   The guest sets sp at 0x80001E00 (offset 7680) so anything below that
   is fair game for stack growth. */
#define RAM_SIZE 8192
static uint32_t ram_words[RAM_SIZE / 4];

#define MINIRV32_CUSTOM_MEMORY_BUS

static uint32_t mem_load4(uint32_t ofs) {
    return ram_words[ofs >> 2];
}
static uint16_t mem_load2(uint32_t ofs) {
    uint32_t w = ram_words[ofs >> 2];
    uint32_t shift = (ofs & 2) * 8;
    return (uint16_t)((w >> shift) & 0xFFFF);
}
static uint8_t mem_load1(uint32_t ofs) {
    uint32_t w = ram_words[ofs >> 2];
    uint32_t shift = (ofs & 3) * 8;
    return (uint8_t)((w >> shift) & 0xFF);
}
static int16_t mem_load2s(uint32_t ofs) {
    uint16_t v = mem_load2(ofs);
    return (v & 0x8000) ? (int16_t)(v | 0xFFFF0000) : (int16_t)v;
}
static int8_t mem_load1s(uint32_t ofs) {
    uint8_t v = mem_load1(ofs);
    return (v & 0x80) ? (int8_t)(v | 0xFFFFFF00) : (int8_t)v;
}
static void mem_store4(uint32_t ofs, uint32_t val) {
    ram_words[ofs >> 2] = val;
}
static void mem_store2(uint32_t ofs, uint32_t val) {
    uint32_t idx = ofs >> 2;
    uint32_t shift = (ofs & 2) * 8;
    uint32_t mask = 0xFFFF << shift;
    ram_words[idx] = (ram_words[idx] & ~mask) | ((val & 0xFFFF) << shift);
}
static void mem_store1(uint32_t ofs, uint32_t val) {
    uint32_t idx = ofs >> 2;
    uint32_t shift = (ofs & 3) * 8;
    uint32_t mask = 0xFF << shift;
    ram_words[idx] = (ram_words[idx] & ~mask) | ((val & 0xFF) << shift);
}

#define MINIRV32_STORE4(ofs, val) mem_store4(ofs, val)
#define MINIRV32_STORE2(ofs, val) mem_store2(ofs, val)
#define MINIRV32_STORE1(ofs, val) mem_store1(ofs, val)
#define MINIRV32_LOAD4(ofs)       mem_load4(ofs)
#define MINIRV32_LOAD2(ofs)       mem_load2(ofs)
#define MINIRV32_LOAD1(ofs)       mem_load1(ofs)
#define MINIRV32_LOAD2_SIGNED(ofs) mem_load2s(ofs)
#define MINIRV32_LOAD1_SIGNED(ofs) mem_load1s(ofs)

/* Forward declaration so HandleControlStore/Load can see the running core. */
struct MiniRV32IMAState;
static struct MiniRV32IMAState *g_core;

static int done_flag;

static uint32_t HandleControlStore(uint32_t addy, uint32_t val);
static uint32_t HandleControlLoad(uint32_t addy);

/* 32-bit-only MULH helpers. mtg_rv32.c proved these work; we keep the
   identical CUSTOM_MULH wiring so the guest sees a stable RV32M. */
static uint32_t mulhu32(uint32_t a, uint32_t b) {
    uint32_t ah = a >> 16, al = a & 0xFFFF;
    uint32_t bh = b >> 16, bl = b & 0xFFFF;
    uint32_t mid1 = ah * bl;
    uint32_t mid2 = al * bh;
    uint32_t low = al * bl;
    uint32_t carry = ((low >> 16) + (mid1 & 0xFFFF) + (mid2 & 0xFFFF)) >> 16;
    return ah * bh + (mid1 >> 16) + (mid2 >> 16) + carry;
}

#define MINI_RV32_RAM_SIZE RAM_SIZE
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_DECORATE static

#define CUSTOM_MULH \
    case 1: { /* MULH  (signed × signed) */ \
        uint32_t a = rs1, b = rs2; \
        uint32_t sa = (a >> 31) & 1, sb = (b >> 31) & 1; \
        uint32_t aa = sa ? (uint32_t)(-(int32_t)a) : a; \
        uint32_t bb = sb ? (uint32_t)(-(int32_t)b) : b; \
        uint32_t hi = mulhu32(aa, bb); \
        uint32_t lo = aa * bb; \
        if (sa ^ sb) { \
            hi = ~hi; lo = (uint32_t)(-(int32_t)lo); \
            if (lo == 0) hi += 1; \
        } \
        rval = hi; \
        break; \
    } \
    case 2: { /* MULHSU (signed × unsigned) */ \
        uint32_t a = rs1, b = rs2; \
        uint32_t sa = (a >> 31) & 1; \
        uint32_t aa = sa ? (uint32_t)(-(int32_t)a) : a; \
        uint32_t hi = mulhu32(aa, b); \
        uint32_t lo = aa * b; \
        if (sa) { \
            hi = ~hi; lo = (uint32_t)(-(int32_t)lo); \
            if (lo == 0) hi += 1; \
        } \
        rval = hi; \
        break; \
    } \
    case 3: { /* MULHU */ \
        rval = mulhu32(rs1, rs2); \
        break; \
    }

#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addy, val) \
    if (HandleControlStore(addy, val)) return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addy, rval) \
    rval = HandleControlLoad(addy);
/* mini-rv32ima's default MMIO range only covers UART/SYSCON; extend it
   to include the CLINT region we shim above so guest accesses to
   mtimecmp/mtime go through HandleControlStore/Load instead of trapping
   as access faults. */
#define MINIRV32_MMIO_RANGE(n) \
    ((((n) >= 0x10000000) && ((n) < 0x12000000)) || \
     (((n) >= 0x02000000) && ((n) < 0x02100000)))
#define MINIRV32_OTHERCSR_WRITE(...) ;
#define MINIRV32_OTHERCSR_READ(...) ;
#define MINIRV32_POSTEXEC(...) ;

#include "/mnt/work/mini-rv32ima/mini-rv32ima/mini-rv32ima.h"

/* MMIO surface — defined after the header so we can poke g_core fields. */
static uint32_t HandleControlStore(uint32_t addy, uint32_t val) {
    if (addy == 0x10000000) {
        __mtg_output(val);
        return 0;
    }
    if (addy == 0x11100000) {
        done_flag = 1;
        return val;
    }
    /* CLINT timer-compare. mtime is read-only (mirrors the simulated
       timer); writes here drive mini-rv32ima's existing timer-match
       path so a guest WFI can be woken without any ISA tweak. */
    if (addy == 0x02004000) {
        g_core->timermatchl = val;
        return 0;
    }
    if (addy == 0x02004004) {
        g_core->timermatchh = val;
        return 0;
    }
    return 0;
}

static uint32_t HandleControlLoad(uint32_t addy) {
    if (addy == 0x0200BFF8)
        return g_core ? g_core->timerl : 0;
    if (addy == 0x0200BFFC)
        return g_core ? g_core->timerh : 0;
    return 0;
}

/* Flat binary of tiny_rv32priv.c (~1.2 KB), loaded at RAM offset 0
   which maps to 0x80000000 in guest address space. */
static const uint32_t tiny_rom[] = {
    0x80002137, 0xE0010113, 0x0240006F, 0x342022F3, 0x0002D663,
    0x08000313, 0x30433073, 0x341022F3, 0x00428293, 0x34129073,
    0x30200073, 0x00000297, 0xFE028293, 0x30529073, 0xDEADC537,
    0xEEF50513, 0x34051073, 0x340025F3, 0x1EA59063, 0x10000537,
    0x04100593, 0x00B50023, 0x0FF00293, 0x34029073, 0x000102B7,
    0xF0028293, 0x3402A073, 0x34002673, 0x000105B7, 0xFFF58693,
    0x1CD61863, 0x0F000293, 0x3402B073, 0x34002573, 0xF0F58593,
    0x1CB51C63, 0x10000537, 0x04200593, 0x00B50023, 0x340FD073,
    0x340025F3, 0x01F00613, 0x1CC59E63, 0x04300593, 0x00B50023,
    0x123402B7, 0x34029073, 0x00000073, 0x123452B7, 0x67828293,
    0x34029073, 0x34002573, 0x123455B7, 0x67858593, 0x1CB51463,
    0x10000537, 0x04400593, 0x00B50023, 0x800015B7, 0x11111637,
    0x11160693, 0x4CD5AA23, 0x4D458713, 0x22222637, 0x22260613,
    0x08C7272F, 0x1AD71C63, 0x4D45A583, 0x1AC59863, 0x04500593,
    0x00B50023, 0x800015B7, 0x06400613, 0x4CC5AA23, 0x4D458693,
    0x03200713, 0x00E6A6AF, 0x1AC69463, 0x4D45A583, 0x09600613,
    0x18C59E63, 0x04600593, 0x00B50023, 0x800015B7, 0x0FF00613,
    0x4CC5AA23, 0x4D458693, 0x00F00713, 0x60E6A7AF, 0x4D45A783,
    0x18E79863, 0x0F000513, 0x40A6A52F, 0x4D45A503, 0x18C51E63,
    0x80001537, 0x4D450593, 0x0AA00613, 0x20C5A62F, 0x4D452603,
    0x05500693, 0x1AD61063, 0x10000637, 0x04700693, 0x00D60023,
    0xFFB00713, 0x4CE52A23, 0x00300693, 0x80D5A5AF, 0x4D452503,
    0x18E51E63, 0x80001537, 0x4D450593, 0xA0D5A62F, 0x4D452603,
    0x1AD61263, 0x100006B7, 0x04800613, 0x00C68023, 0xFFF00613,
    0x4CC52A23, 0x0FF00713, 0xC0E5A5AF, 0x4D452503, 0x1AE51063,
    0x800016B7, 0x4D468593, 0xE0C5A52F, 0x4D46A503, 0x1AC50463,
    0x10000537, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x10000537, 0x02100593,
    0x00B50023, 0x00005537, 0x55550513, 0x111005B7, 0x00A5A023,
    0x0000006F, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x10000537, 0x02100593,
    0x00B50023, 0x00005537, 0x55550513, 0x111005B7, 0x00A5A023,
    0x0000006F, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x10000537, 0x02100593,
    0x00B50023, 0x00005537, 0x55550513, 0x111005B7, 0x00A5A023,
    0x0000006F, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x02100593, 0x00B50023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x02100593, 0x00B50023, 0x00005537, 0x55550513, 0x111005B7,
    0x00A5A023, 0x0000006F, 0x10000537, 0x02100593, 0x00B50023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x10000537, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x02100513, 0x00A60023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x10000537, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x02100513, 0x00A68023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x10000537, 0x04900613, 0x00C50023, 0xAAAAB637, 0xAAA60713,
    0x4CE6AA23, 0xBBBBC637, 0xBBB60693, 0x1005A7AF, 0x18D5A62F,
    0x0CE79463, 0x0C061263, 0x800015B7, 0x4D45A603, 0xBBBBC6B7,
    0xBBB68693, 0x0AD61863, 0x04A00613, 0x00C50023, 0xCCCCD637,
    0xCCC60613, 0x4CC5AA23, 0x4D458693, 0x800015B7, 0x4C05AC23,
    0x4D858713, 0xDDDDE5B7, 0xDDD58793, 0x1006A82F, 0x100728AF,
    0x18F6A5AF, 0x08C81863, 0x08058663, 0x800015B7, 0x4D45A583,
    0xCCCCD637, 0xCCC60613, 0x06C59C63, 0x04B00593, 0x00B50023,
    0x00800293, 0x3002A073, 0x08000293, 0x3042A073, 0x0200C5B7,
    0xFF85A583, 0x02004637, 0x00062223, 0x00458593, 0x00B62023,
    0x10500073, 0x04C00593, 0x00B50023, 0x00A00593, 0x00B50023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x02100593, 0x00B50023, 0x00005537, 0x55550513, 0x111005B7,
    0x00A5A023, 0x0000006F, 0x02100593, 0x00B50023, 0x00005537,
    0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
};

void _start(void) {
    int i;
    for (i = 0; i < RAM_SIZE / 4; i++)
        ram_words[i] = 0;

    for (i = 0; i < (int)(sizeof(tiny_rom) / sizeof(tiny_rom[0])); i++)
        ram_words[i] = tiny_rom[i];

    struct MiniRV32IMAState *core =
        (struct MiniRV32IMAState *)((uint8_t *)ram_words + RAM_SIZE
                                    - sizeof(struct MiniRV32IMAState));
    core->pc = MINIRV32_RAM_IMAGE_OFFSET;
    core->extraflags = 3;  /* Machine mode */
    g_core = core;          /* expose to MMIO callbacks */

    /* WFI may park the CPU for a handful of steps, and the per-test
       work is non-trivial; budget generously. */
    done_flag = 0;
    for (i = 0; i < 20000 && !done_flag; i++) {
        int32_t ret = MiniRV32IMAStep(core, (uint8_t *)ram_words, 0, 1, 1);
        if (ret == 0x5555) {
            done_flag = 1;
        }
    }
}
