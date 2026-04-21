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
 *   ursa/ursa-rs/target/release/ursa-rs /tmp/linked.s --zero-mem \
 *       --rom /path/to/Image@1024 \
 *       --rom /path/to/dtb.bin@8387904
 *
 * Why 1024 and 8387904:
 *   ram_words lives at MtG byte address 1024 (the start of globals;
 *   confirm via `print(prog.globals["ram_words"])` in ursa's assembler
 *   module, or by reading `.comm ram_words` layout in the linked .s).
 *   The kernel image goes at ram_words byte 0, i.e. MtG address 1024.
 *   The DTB goes at ram_words byte (RAM_SIZE - sizeof(state) - DTB_SIZE)
 *   = 8388608 - 192 - 1536 = 8386880, i.e. MtG address 1024 + 8386880
 *   = 8387904. Miscounting this by even a single cell lands the kernel
 *   on "memory@8000..." strings inside the DTB body and makes
 *   fdt_check_header fail silently — been there.
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
/* Default UART/SYSCON range; mini-rv32ima's default DTB places CLINT
   at 0x11000000 which is already inside the default 0x10000000..
   0x12000000 range, so we don't need to widen it. */
#define MINIRV32_OTHERCSR_WRITE(...) ;
#define MINIRV32_OTHERCSR_READ(...) ;
#define MINIRV32_POSTEXEC(...) ;

#include "/mnt/work/mini-rv32ima/mini-rv32ima/mini-rv32ima.h"

#ifdef LINUX_BOOT_MMIO_TRACE
static uint32_t mmio_store_count;
static uint32_t mmio_load_count;
#endif

#if defined(LINUX_BOOT_PC_HIST) || defined(LINUX_BOOT_TRAP_TRACE) || defined(LINUX_BOOT_STEP_TRACE)
static void hex32(uint32_t v) {
    unsigned k;
    for (k = 0; k < 8; k++) {
        unsigned nib = (v >> ((7 - k) * 4)) & 0xF;
        __mtg_output(nib < 10 ? ('0' + nib) : ('A' + (nib - 10)));
    }
}
#endif

#ifdef LINUX_BOOT_PC_HIST
/* Hash-based PC histogram: key=PC, cnt=hits. Linear-probe, drop on
   overflow. 8192 slots covers ~1k distinct hot PCs comfortably. */
#define PC_HIST_SIZE 8192u
#define PC_HIST_MASK (PC_HIST_SIZE - 1u)
static uint32_t pc_hist_key[PC_HIST_SIZE];
static uint32_t pc_hist_cnt[PC_HIST_SIZE];
static uint32_t pc_hist_dropped;

static void pc_hist_add(uint32_t pc) {
    uint32_t h = (pc * 2654435761u) & PC_HIST_MASK;
    uint32_t probe;
    for (probe = 0; probe < PC_HIST_SIZE; probe++) {
        uint32_t slot = (h + probe) & PC_HIST_MASK;
        uint32_t k = pc_hist_key[slot];
        if (k == pc) { pc_hist_cnt[slot]++; return; }
        if (k == 0 && pc_hist_cnt[slot] == 0) {
            pc_hist_key[slot] = pc;
            pc_hist_cnt[slot] = 1;
            return;
        }
    }
    pc_hist_dropped++;
}

static void pc_hist_dump(void) {
    uint32_t i;
    __mtg_output('\n');
    __mtg_output('H'); __mtg_output('I'); __mtg_output('S'); __mtg_output('T');
    __mtg_output(':'); __mtg_output('\n');
    for (i = 0; i < PC_HIST_SIZE; i++) {
        uint32_t c = pc_hist_cnt[i];
        if (c == 0) continue;
        hex32(pc_hist_key[i]);
        __mtg_output('=');
        hex32(c);
        __mtg_output('\n');
    }
    __mtg_output('D'); __mtg_output('R'); __mtg_output('O'); __mtg_output('P');
    __mtg_output('=');
    hex32(pc_hist_dropped);
    __mtg_output('\n');
}
#endif

static uint32_t HandleControlStore(uint32_t addy, uint32_t val) {
#ifdef LINUX_BOOT_MMIO_TRACE
    /* Emit one '>' per store to any MMIO address. Lets us see whether
       the kernel reaches device code at all. */
    mmio_store_count++;
    if ((mmio_store_count & 0xFF) == 0) __mtg_output('>');
#endif
    if (addy == 0x10000000) {
        __mtg_output(val);
        return 0;
    }
    if (addy == 0x11100000) {
        done_flag = 1;
        return val;
    }
    if (addy == 0x11004000) { g_core->timermatchl = val; return 0; }
    if (addy == 0x11004004) { g_core->timermatchh = val; return 0; }
    return 0;
}

static uint32_t HandleControlLoad(uint32_t addy) {
#ifdef LINUX_BOOT_MMIO_TRACE
    mmio_load_count++;
    if ((mmio_load_count & 0xFF) == 0) __mtg_output('<');
#endif
    if (addy == 0x10000005) return 0x60;
    if (addy == 0x1100BFF8) return g_core ? g_core->timerl : 0;
    if (addy == 0x1100BFFC) return g_core ? g_core->timerh : 0;
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

    /* Patch default64mbdtb's memory-size sentinel to match our actual
       RAM (= dtb byte offset). Without this, the kernel trusts the
       DTB's 64 MB claim and oops-es the first time memblock hands out
       a page past our 8 MB. Matches what mini-rv32ima.c does natively.
       The sentinel at DTB byte 0x13c is 0x03 0xFF 0xC0 0x00 (BE for
       ~64 MB - 16 KB), appearing as the LE u32 0x00C0FF03. */
    uint8_t *dtb_bytes =
        (uint8_t *)ram_words + (dtb_guest_addr - MINIRV32_RAM_IMAGE_OFFSET);
    uint32_t *dtb_sentinel = (uint32_t *)(dtb_bytes + 0x13c);
    if (*dtb_sentinel == 0x00c0ff03u) {
        uint32_t valid_ram =
            dtb_guest_addr - MINIRV32_RAM_IMAGE_OFFSET; /* bytes of usable RAM */
        *dtb_sentinel =
            ((valid_ram >> 24) & 0xFFu) |
            (((valid_ram >> 16) & 0xFFu) << 8) |
            (((valid_ram >> 8) & 0xFFu) << 16) |
            ((valid_ram & 0xFFu) << 24);
    }

    g_core = core;

    /* Run until the kernel halts via SYSCON, or until a conservative
       upper-bound on steps.

       Compile with -DLINUX_BOOT_TRACE to emit `[pc=XXXXXXXX]` every N
       RV32 instructions; useful to tell whether an apparently-silent
       boot is stuck (e.g. in an infinite trap loop) or merely slow
       (e.g. BSS zero-init of ~150 KB dominates for the first several
       minutes under the current Python simulator). */
    done_flag = 0;
#ifndef LINUX_BOOT_MAX_RV32_STEPS
#define LINUX_BOOT_MAX_RV32_STEPS 4000000000u
#endif
#ifdef LINUX_BOOT_TRAP_TRACE
    uint32_t prev_pc = 0xFFFFFFFFu;
    uint32_t trap_log_count = 0;
#endif
    for (i = 0; i < LINUX_BOOT_MAX_RV32_STEPS && !done_flag; i++) {
        int32_t ret = MiniRV32IMAStep(core, (uint8_t *)ram_words, 0, 1, 1);
        if (ret == 0x5555) {
            done_flag = 1;
        }
#ifdef LINUX_BOOT_STEP_TRACE
        /* Emit `P <pc>` for every step up to LINUX_BOOT_STEP_TRACE (a
           compile-time limit). Handy for diffing the guest PC stream
           against a native mini-rv32ima run and finding the first
           divergence — the technique that caught the 2026-04-21 DTB
           off-by-256 bug. */
        if (i < (uint32_t)LINUX_BOOT_STEP_TRACE) {
            __mtg_output('P');
            __mtg_output(' ');
            hex32(core->pc);
            __mtg_output('\n');
        }
#endif
#ifdef LINUX_BOOT_TRAP_TRACE
        /* Each time core->pc transitions into mtvec (0x80001cbc for the
           rv32nommu 6.1.14 kernel we test against), log mcause/mepc/
           mtval plus a few caller-saved regs. Useful for spotting the
           first exception the kernel takes. Bounded to avoid swamping
           output on fault loops. If you re-target a different kernel
           image, update the mtvec literal below. */
        if (core->pc == 0x80001cbcu && prev_pc != 0x80001cbcu
                && trap_log_count < 16u) {
            __mtg_output('T');
            __mtg_output(':');
            hex32(core->mcause);
            __mtg_output(':');
            hex32(core->mepc);
            __mtg_output(':');
            hex32(core->mtval);
            __mtg_output('\n');
            trap_log_count++;
        }
        prev_pc = core->pc;
#endif
#ifdef LINUX_BOOT_PC_HIST
        pc_hist_add(core->pc);
#endif
#ifdef LINUX_BOOT_TRACE
        if ((i & 0x7Fu) == 0u) {
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
#ifdef LINUX_BOOT_PC_HIST
    pc_hist_dump();
#endif
}
