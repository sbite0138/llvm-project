/* MtG ✕ mini-rv32ima end-to-end smoke test.
 *
 * Goes one layer beyond the single-instruction test vectors in
 * mtg_rv32.c: boots a real C program that was cross-compiled with
 * system clang --target=riscv32 -march=rv32ima_zicsr -mabi=ilp32,
 * objcopied to a flat binary, and encoded here as a uint32_t word
 * array. The guest program writes "RV!\n" to the UART MMIO at
 * 0x10000000 and signals completion via the SYSCON at 0x11100000.
 *
 * Manual invocation:
 *   build/bin/clang --target=mtg -O1 -nostdlib -ffreestanding -S \
 *       llvm/test/CodeGen/MtG/mini-rv32ima/smoke_rv32.c -o /tmp/t.s \
 *       -I build/lib/clang/23/include
 *   python3 ursa/tools/mtg-link.py /tmp/t.s -o /tmp/linked.s
 *   python3 ursa/src/main.py /tmp/linked.s     # expect "RV!\n"
 *
 * Regenerating the embedded binary (kept out of git; do by hand when
 * tiny.c below changes):
 *
 *   cat > /tmp/tiny.c <<'EOF'
 *   #define UART 0x10000000u
 *   #define HALT 0x11100000u
 *   static void putc_uart(char c) {
 *       *(volatile unsigned char *)UART = (unsigned char)c;
 *   }
 *   void _start(void) {
 *       putc_uart('R'); putc_uart('V'); putc_uart('!'); putc_uart('\n');
 *       *(volatile unsigned int *)HALT = 0x5555;
 *       for (;;) { }
 *   }
 *   EOF
 *   /usr/bin/clang --target=riscv32 -march=rv32ima_zicsr -mabi=ilp32 \
 *       -nostdlib -ffreestanding -fno-pic -Os \
 *       -Wl,-Ttext=0x80000000 -Wl,--no-relax -fuse-ld=lld \
 *       /tmp/tiny.c -o /tmp/tiny.elf
 *   /usr/bin/llvm-objcopy -O binary /tmp/tiny.elf /tmp/tiny.bin
 *   od -An -tx4 -v /tmp/tiny.bin
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

#define RAM_SIZE 4096
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

static int done_flag;

static uint32_t HandleControlStore(uint32_t addy, uint32_t val) {
    if (addy == 0x10000000) {
        __mtg_output(val);
        return 0;
    }
    if (addy == 0x11100000) {
        done_flag = 1;
        return val;
    }
    return 0;
}

static uint32_t HandleControlLoad(uint32_t addy) {
    return 0;
}

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
#define MINIRV32_OTHERCSR_WRITE(...) ;
#define MINIRV32_OTHERCSR_READ(...) ;
#define MINIRV32_POSTEXEC(...) ;

#include "/mnt/work/mini-rv32ima/mini-rv32ima/mini-rv32ima.h"

/* Flat binary of tiny.c (14 words = 56 bytes), loaded at RAM offset 0
   which maps to 0x80000000 in guest address space. */
static const uint32_t tiny_rom[] = {
    0x10000537, 0x05200593, 0x00b50023, 0x05600593, 0x00b50023,
    0x02100593, 0x00b50023, 0x00a00593, 0x00b50023, 0x00005537,
    0x55550513, 0x111005b7, 0x00a5a023, 0x0000006f,
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

    /* Enough headroom to reach the HALT write even on a cold icache. */
    done_flag = 0;
    for (i = 0; i < 200 && !done_flag; i++) {
        int32_t ret = MiniRV32IMAStep(core, (uint8_t *)ram_words, 0, 1, 1);
        if (ret == 0x5555) {
            done_flag = 1;
        }
    }
}
