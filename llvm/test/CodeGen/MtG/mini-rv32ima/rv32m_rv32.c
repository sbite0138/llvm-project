/* MtG ✕ mini-rv32ima — RV32M edge-case verification.
 *
 * Companion to smoke_rv32.c. Where smoke runs a trivial UART-only guest,
 * this harness boots a guest that exercises CUSTOM_MULH (MULH/MULHSU/
 * MULHU) and the RV32M division corners (INT_MIN/-1, x/0, x%0,
 * INT_MIN%-1). Each test that passes emits one character; on mismatch
 * the guest emits '!' and halts via SYSCON. Successful run output:
 *
 *   ABCDEFGHIJKLM\n
 *
 * Manual invocation:
 *   build/bin/clang --target=mtg -O1 -nostdlib -ffreestanding -S \
 *       llvm/test/CodeGen/MtG/mini-rv32ima/rv32m_rv32.c -o /tmp/t.s \
 *       -I build/lib/clang/23/include
 *   python3 ursa/tools/mtg-link.py /tmp/t.s -o /tmp/linked.s
 *   python3 ursa/src/main.py /tmp/linked.s   # expect "ABCDEFGHIJKLM\n"
 *
 * Regenerating the embedded binary (kept out of git; do by hand when
 * tiny_rv32m.c below changes):
 *
 *   /usr/bin/clang --target=riscv32 -march=rv32ima_zicsr -mabi=ilp32 \
 *       -nostdlib -ffreestanding -fno-pic -Os \
 *       -Wl,-Ttext=0x80000000 -Wl,--no-relax -fuse-ld=lld \
 *       /tmp/tiny_rv32m.c -o /tmp/tiny_rv32m.elf
 *   /usr/bin/llvm-objcopy -O binary /tmp/tiny_rv32m.elf /tmp/tiny_rv32m.bin
 *   od -An -tx4 -v /tmp/tiny_rv32m.bin
 *
 * The guest source (tiny_rv32m.c) lives outside the tree; reproduce it
 * from the comment below if regenerating.
 *
 *   #define UART 0x10000000u
 *   #define HALT 0x11100000u
 *   static void put_char(unsigned char c) {
 *       *(volatile unsigned char *)UART = c;
 *   }
 *   static void __attribute__((noreturn)) halt(void) {
 *       *(volatile unsigned int *)HALT = 0x5555u;
 *       for (;;) { }
 *   }
 *   #define MUL(a,b)    ({unsigned _r;__asm__("mul    %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define MULH(a,b)   ({unsigned _r;__asm__("mulh   %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define MULHSU(a,b) ({unsigned _r;__asm__("mulhsu %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define MULHU(a,b)  ({unsigned _r;__asm__("mulhu  %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define DIV(a,b)    ({int      _r;__asm__("div    %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define DIVU(a,b)   ({unsigned _r;__asm__("divu   %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define REM(a,b)    ({int      _r;__asm__("rem    %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define REMU(a,b)   ({unsigned _r;__asm__("remu   %0,%1,%2":"=r"(_r):"r"(a),"r"(b));_r;})
 *   #define CHECK(c,ch) do{if(c)put_char(ch);else{put_char('!');halt();}}while(0)
 *   void _start(void) {
 *       CHECK(MUL(6,7) == 42u, 'A');
 *       CHECK(MUL(0x10000u,0x10000u) == 0u, 'B');
 *       CHECK(MULH(0x80000000u,0x80000000u) == 0x40000000u, 'C');
 *       CHECK(MULH(0xFFFFFFFFu,1u) == 0xFFFFFFFFu, 'D');
 *       CHECK(MULH(0xFFFFFFFFu,0xFFFFFFFFu) == 0u, 'E');
 *       CHECK(MULHU(0xFFFFFFFFu,0xFFFFFFFFu) == 0xFFFFFFFEu, 'F');
 *       CHECK(MULHSU(0x80000000u,1u) == 0xFFFFFFFFu, 'G');
 *       CHECK(DIV((int)0x80000000,-1) == (int)0x80000000, 'H');
 *       CHECK(DIV(7,0) == -1, 'I');
 *       CHECK(DIVU(7u,0u) == 0xFFFFFFFFu, 'J');
 *       CHECK(REM(7,0) == 7, 'K');
 *       CHECK(REM((int)0x80000000,-1) == 0, 'L');
 *       CHECK(REMU(7u,0u) == 7u, 'M');
 *       put_char('\n');
 *       halt();
 *   }
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

/* RAM big enough for tiny_rom (704 B) + MiniRV32IMAState (~196 B). */
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

/* 32-bit-only MULH helpers (avoid 64-bit libcalls). */
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

/* Flat binary of tiny_rv32m.c (176 words = 704 bytes), loaded at RAM
   offset 0 which maps to 0x80000000 in guest address space. */
static const uint32_t tiny_rom[] = {
    0x00600513, 0x00700593, 0x02B50533, 0x02A00593, 0x02B51C63,
    0x10000537, 0x04100593, 0x00010637, 0x02C60633, 0x00B50023,
    0x04060063, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x10000537, 0x02100593,
    0x00B50023, 0x00005537, 0x55550513, 0x111005B7, 0x00A5A023,
    0x0000006F, 0x04200593, 0x80000637, 0x02C61633, 0x400006B7,
    0x00B50023, 0x02D61C63, 0x04300593, 0xFFF00613, 0x00100693,
    0x02D616B3, 0x00B50023, 0x02C68E63, 0x02100593, 0x00B50023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x02100593, 0x00B50023, 0x00005537, 0x55550513, 0x111005B7,
    0x00A5A023, 0x0000006F, 0x04400593, 0xFFF00613, 0x02C61633,
    0x00B50023, 0x02060063, 0x02100593, 0x00B50023, 0x00005537,
    0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F, 0x04500613,
    0xFFF00593, 0x02B5B6B3, 0xFFE00713, 0x00C50023, 0x02E69C63,
    0x04600693, 0x80000637, 0x00100713, 0x02E62733, 0x00D50023,
    0x02B70E63, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x02100593, 0x00B50023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x04700693, 0xFFF00593, 0x02B64733, 0x00D50023, 0x02C71C63,
    0x00000613, 0x04800693, 0x00700713, 0x02C74633, 0x00D50023,
    0x02B60E63, 0x02100593, 0x00B50023, 0x00005537, 0x55550513,
    0x111005B7, 0x00A5A023, 0x0000006F, 0x02100593, 0x00B50023,
    0x00005537, 0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F,
    0x00000613, 0x04900693, 0x00700593, 0x02C5D633, 0xFFF00713,
    0x00D50023, 0x02E60063, 0x02100593, 0x00B50023, 0x00005537,
    0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F, 0x00000613,
    0x04A00693, 0x02C5E633, 0x00D50023, 0x02B61C63, 0x04B00593,
    0x80000637, 0xFFF00693, 0x02D66633, 0x00B50023, 0x02060E63,
    0x02100593, 0x00B50023, 0x00005537, 0x55550513, 0x111005B7,
    0x00A5A023, 0x0000006F, 0x02100593, 0x00B50023, 0x00005537,
    0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F, 0x00000593,
    0x04C00613, 0x00700693, 0x02B6F5B3, 0x00C50023, 0x02D59463,
    0x04D00593, 0x00B50023, 0x00A00593, 0x00B50023, 0x00005537,
    0x55550513, 0x111005B7, 0x00A5A023, 0x0000006F, 0x02100593,
    0x00B50023, 0x00005537, 0x55550513, 0x111005B7, 0x00A5A023,
    0x0000006F,
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

    /* 13 tests × ~10 instructions each + helpers; 4000 steps is generous. */
    done_flag = 0;
    for (i = 0; i < 4000 && !done_flag; i++) {
        int32_t ret = MiniRV32IMAStep(core, (uint8_t *)ram_words, 0, 1, 1);
        if (ret == 0x5555) {
            done_flag = 1;
        }
    }
}
