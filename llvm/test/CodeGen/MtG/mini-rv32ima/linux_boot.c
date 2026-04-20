/* MtG ✕ mini-rv32ima — first Linux boot attempt (Epic 5-4).
 *
 * Unlike the other mini-rv32ima harnesses in this directory, this one
 * embeds no test ROM: the guest kernel image and the DTB are preloaded
 * into guest memory by ursa's --rom flag. We only build the minimal
 * surrounding machinery — MMIO for UART/SYSCON/CLINT, the custom
 * memory bus, the CUSTOM_MULH shim — and the emulator step loop.
 *
 * RAM_SIZE is large enough for a small rv32nommu Linux (8 MB of
 * simulated DRAM — mini-rv32ima's default demos use 64 MB but we
 * start smaller to keep ursa memory-dict overhead manageable).
 *
 * Memory layout in guest space (= byte offsets from 0x80000000):
 *   0                         kernel image
 *   ...
 *   RAM_SIZE - sizeof(state) - DTB_SIZE   DTB
 *   RAM_SIZE - sizeof(state)              MiniRV32IMAState
 *
 * Manual invocation (kernel + DTB supplied by --rom):
 *   build/bin/clang --target=mtg -O1 -nostdlib -ffreestanding -S \
 *       llvm/test/CodeGen/MtG/mini-rv32ima/linux_boot.c -o /tmp/t.s \
 *       -I build/lib/clang/23/include
 *   python3 ursa/tools/mtg-link.py /tmp/t.s -o /tmp/linked.s
 *   python3 ursa/src/main.py /tmp/linked.s --zero-mem \
 *       --rom /path/to/Image@<ram_words_addr> \
 *       --rom /path/to/dtb.bin@<ram_words_addr + DTB_OFFSET>
 *
 *   (ram_words_addr is the MtG address of the ram_words[] array; look
 *   it up in the linked .s or print `prog.globals["ram_words"]` from
 *   ursa's assembler module.)
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

/* 8 MB simulated DRAM. At 1 cell/byte this is 8 M cells in ursa's
   memory dict — the zero-init read path (--zero-mem) keeps us from
   having to prefill it. Scale up later if the kernel needs more. */
#define RAM_SIZE (8 * 1024 * 1024)
#define DTB_SIZE 1536  /* matches mini-rv32ima's default64mbdtc.h */
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

struct MiniRV32IMAState;
static struct MiniRV32IMAState *g_core;
static int done_flag;

static uint32_t HandleControlStore(uint32_t addy, uint32_t val);
static uint32_t HandleControlLoad(uint32_t addy);

/* Reuse the 32-bit MULH shim the other harnesses use. */
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
    case 1: { \
        uint32_t a = rs1, b = rs2; \
        uint32_t sa = (a >> 31) & 1, sb = (b >> 31) & 1; \
        uint32_t aa = sa ? (uint32_t)(-(int32_t)a) : a; \
        uint32_t bb = sb ? (uint32_t)(-(int32_t)b) : b; \
        uint32_t hi = mulhu32(aa, bb); \
        uint32_t lo = aa * bb; \
        if (sa ^ sb) { hi = ~hi; lo = (uint32_t)(-(int32_t)lo); if (lo == 0) hi += 1; } \
        rval = hi; break; \
    } \
    case 2: { \
        uint32_t a = rs1, b = rs2; \
        uint32_t sa = (a >> 31) & 1; \
        uint32_t aa = sa ? (uint32_t)(-(int32_t)a) : a; \
        uint32_t hi = mulhu32(aa, b); \
        uint32_t lo = aa * b; \
        if (sa) { hi = ~hi; lo = (uint32_t)(-(int32_t)lo); if (lo == 0) hi += 1; } \
        rval = hi; break; \
    } \
    case 3: { rval = mulhu32(rs1, rs2); break; }

#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addy, val) \
    if (HandleControlStore(addy, val)) return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addy, rval) \
    rval = HandleControlLoad(addy);
/* Default UART/SYSCON range plus the CLINT region so timer accesses
   route through our shim (Epic 3-3 pattern). */
#define MINIRV32_MMIO_RANGE(n) \
    ((((n) >= 0x10000000) && ((n) < 0x12000000)) || \
     (((n) >= 0x02000000) && ((n) < 0x02100000)))
#define MINIRV32_OTHERCSR_WRITE(...) ;
#define MINIRV32_OTHERCSR_READ(...) ;
#define MINIRV32_POSTEXEC(...) ;

#include "/mnt/work/mini-rv32ima/mini-rv32ima/mini-rv32ima.h"

static uint32_t HandleControlStore(uint32_t addy, uint32_t val) {
    if (addy == 0x10000000) {
        __mtg_output(val);
        return 0;
    }
    if (addy == 0x11100000) {
        done_flag = 1;
        return val;
    }
    if (addy == 0x02004000) { g_core->timermatchl = val; return 0; }
    if (addy == 0x02004004) { g_core->timermatchh = val; return 0; }
    return 0;
}

static uint32_t HandleControlLoad(uint32_t addy) {
    if (addy == 0x0200BFF8) return g_core ? g_core->timerl : 0;
    if (addy == 0x0200BFFC) return g_core ? g_core->timerh : 0;
    return 0;
}

void _start(void) {
    /* No zeroing — ursa --zero-mem handles untouched reads. The kernel
       image and DTB are assumed preloaded via --rom. */
    struct MiniRV32IMAState *core =
        (struct MiniRV32IMAState *)((uint8_t *)ram_words + RAM_SIZE
                                    - sizeof(struct MiniRV32IMAState));

    /* DTB sits between the kernel image and the state struct. The
       guest's a1 register receives the DTB's guest-physical address
       (base 0x80000000 + offset-in-RAM). */
    uint32_t dtb_guest_addr =
        MINIRV32_RAM_IMAGE_OFFSET + RAM_SIZE
            - (uint32_t)sizeof(struct MiniRV32IMAState) - DTB_SIZE;

    /* Zero out only the state struct area (we reuse parts of it for
       scratch; leaving the area zeroed is cheap and unambiguous). */
    unsigned i;
    for (i = 0; i < sizeof(struct MiniRV32IMAState) / 4; i++)
        ((uint32_t *)core)[i] = 0;

    core->pc = MINIRV32_RAM_IMAGE_OFFSET;
    core->regs[10] = 0;                /* a0 = hartid */
    core->regs[11] = dtb_guest_addr;   /* a1 = DTB pointer */
    core->extraflags = 3;              /* M-mode */

    g_core = core;

    /* Run until the kernel halts via SYSCON, or until a conservative
       upper-bound on steps.

       Compile with -DLINUX_BOOT_TRACE to emit `[pc=XXXXXXXX]` every N
       RV32 instructions; useful to tell whether an apparently-silent
       boot is stuck (e.g. in an infinite trap loop) or merely slow
       (e.g. BSS zero-init of ~150 KB dominates for the first several
       minutes under the current Python simulator). */
    done_flag = 0;
    for (i = 0; i < 500000000u && !done_flag; i++) {
        int32_t ret = MiniRV32IMAStep(core, (uint8_t *)ram_words, 0, 1, 1);
        if (ret == 0x5555) {
            done_flag = 1;
        }
#ifdef LINUX_BOOT_TRACE
        if ((i & 0x1FFu) == 0u) {
            uint32_t pc = core->pc;
            unsigned k;
            __mtg_output('[');
            for (k = 0; k < 8; k++) {
                unsigned nib = (pc >> ((7 - k) * 4)) & 0xF;
                __mtg_output(nib < 10 ? ('0' + nib) : ('A' + (nib - 10)));
            }
            __mtg_output(']');
        }
#endif
    }
}
