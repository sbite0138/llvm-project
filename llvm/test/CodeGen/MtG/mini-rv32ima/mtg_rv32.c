/* MtG freestanding harness for mini-rv32ima. */
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

/* ----- RAM (4 KB, word-addressable backing store) ----- */
#define RAM_SIZE 4096
static uint32_t ram_words[RAM_SIZE / 4];

/* Custom memory bus: pack/unpack bytes within 32-bit words. */
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

/* ----- MMIO handling ----- */
static int done_flag;

static uint32_t HandleControlStore(uint32_t addy, uint32_t val) {
    if (addy == 0x10000000) {  /* UART */
        __mtg_output(val);
        return 0;
    }
    if (addy == 0x11100000) {  /* SYSCON poweroff/restart */
        done_flag = 1;
        return val;
    }
    return 0;
}

static uint32_t HandleControlLoad(uint32_t addy) {
    return 0;
}

/* 32-bit-only MULH: avoid 64-bit multiply which needs __muldi3. */
static uint32_t mulhu32(uint32_t a, uint32_t b) {
    uint32_t ah = a >> 16, al = a & 0xFFFF;
    uint32_t bh = b >> 16, bl = b & 0xFFFF;
    uint32_t mid1 = ah * bl;
    uint32_t mid2 = al * bh;
    uint32_t low = al * bl;
    uint32_t carry = ((low >> 16) + (mid1 & 0xFFFF) + (mid2 & 0xFFFF)) >> 16;
    return ah * bh + (mid1 >> 16) + (mid2 >> 16) + carry;
}

/* ----- Mini-rv32ima config ----- */
#define MINI_RV32_RAM_SIZE RAM_SIZE
#define MINIRV32_RAM_IMAGE_OFFSET 0  /* Avoid uint32_t overflow in address math;
                                        MtG uses arbitrary-precision integers so
                                        0x90000000 + 0x80000000 = 0x110000000,
                                        breaking MMIO range checks. */
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_DECORATE static

/* Use 32-bit MULH to avoid 64-bit libcalls. */
#define CUSTOM_MULH \
    case 1: { /* MULH (signed * signed) */ \
        uint32_t a = rs1, b = rs2; \
        int neg = 0; \
        if ((int32_t)a < 0) { a = -a; neg ^= 1; } \
        if ((int32_t)b < 0) { b = -b; neg ^= 1; } \
        uint32_t hi = mulhu32(a, b); \
        uint32_t lo = a * b; \
        if (neg) { hi = ~hi + (lo == 0); } \
        rval = hi; break; \
    } \
    case 2: { /* MULHSU (signed * unsigned) */ \
        uint32_t a = rs1, b = rs2; \
        int neg = 0; \
        if ((int32_t)a < 0) { a = -a; neg = 1; } \
        uint32_t hi = mulhu32(a, b); \
        uint32_t lo = a * b; \
        if (neg) { hi = ~hi + (lo == 0); } \
        rval = hi; break; \
    } \
    case 3: rval = mulhu32(rs1, rs2); break; /* MULHU */

#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addy, val) \
    if (HandleControlStore(addy, val)) return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addy, rval) \
    rval = HandleControlLoad(addy);
#define MINIRV32_OTHERCSR_WRITE(...) ;
#define MINIRV32_OTHERCSR_READ(...) ;
#define MINIRV32_POSTEXEC(...) ;

#include "/mnt/work/mini-rv32ima/mini-rv32ima/mini-rv32ima.h"

/* RV32I test ROM: 13 tests (A-M), outputs '!' and poweroff on failure.
   Tests: ADDI, ADD, SUB, ANDI, ORI, XORI, SLLI, SRLI, SW/LW,
          BEQ, BNE, JAL, SLT.
   Expected output on success: "ABCDEFGHIJKLM" */
static const uint32_t test_rom[] = {
    0x100002B7,0x02A00513,0x02A00393,0x00750863,0x02100313,
    0x0062A023,0x111002B7,0x0072A023,0x100002B7,0x04100313,
    0x0062A023,0x01E00513,0x00C00593,0x00B50533,0x02A00393,
    0x00750863,0x02100313,0x0062A023,0x111002B7,0x0072A023,
    0x100002B7,0x04200313,0x0062A023,0x03200513,0x00800593,
    0x40B50533,0x02A00393,0x00750863,0x02100313,0x0062A023,
    0x111002B7,0x0072A023,0x100002B7,0x04300313,0x0062A023,
    0x03F00513,0x02A57513,0x02A00393,0x00750863,0x02100313,
    0x0062A023,0x111002B7,0x0072A023,0x100002B7,0x04400313,
    0x0062A023,0x02200513,0x00856513,0x02A00393,0x00750863,
    0x02100313,0x0062A023,0x111002B7,0x0072A023,0x100002B7,
    0x04500313,0x0062A023,0x0FF00513,0x0D554513,0x02A00393,
    0x00750863,0x02100313,0x0062A023,0x111002B7,0x0072A023,
    0x100002B7,0x04600313,0x0062A023,0x01500513,0x00151513,
    0x02A00393,0x00750863,0x02100313,0x0062A023,0x111002B7,
    0x0072A023,0x100002B7,0x04700313,0x0062A023,0x05400513,
    0x00155513,0x02A00393,0x00750863,0x02100313,0x0062A023,
    0x111002B7,0x0072A023,0x100002B7,0x04800313,0x0062A023,
    0x32000113,0x02A00513,0x00A12023,0x00000513,0x00012503,
    0x02A00393,0x00750863,0x02100313,0x0062A023,0x111002B7,
    0x0072A023,0x100002B7,0x04900313,0x0062A023,0x00500513,
    0x00500593,0x00B50463,0x06300513,0x00500393,0x00750863,
    0x02100313,0x0062A023,0x111002B7,0x0072A023,0x100002B7,
    0x04A00313,0x0062A023,0x00500513,0x00300593,0x00B51463,
    0x06300513,0x00500393,0x00750863,0x02100313,0x0062A023,
    0x111002B7,0x0072A023,0x100002B7,0x04B00313,0x0062A023,
    0x0080056F,0x06300513,0x02A00513,0x02A00393,0x00750863,
    0x02100313,0x0062A023,0x111002B7,0x0072A023,0x100002B7,
    0x04C00313,0x0062A023,0xFFB00513,0x00300593,0x00B52533,
    0x00100393,0x00750863,0x02100313,0x0062A023,0x111002B7,
    0x0072A023,0x100002B7,0x04D00313,0x0062A023,0x111002B7,
    0x55500313,0x0062A023
};

void _start(void) {
    /* Clear RAM */
    int i;
    for (i = 0; i < RAM_SIZE / 4; i++)
        ram_words[i] = 0;

    /* Load ROM at offset 0 (address 0x80000000) */
    for (i = 0; i < (int)(sizeof(test_rom) / sizeof(test_rom[0])); i++)
        ram_words[i] = test_rom[i];

    /* Set up CPU state at end of RAM */
    struct MiniRV32IMAState *core =
        (struct MiniRV32IMAState *)((uint8_t *)ram_words + RAM_SIZE
                                    - sizeof(struct MiniRV32IMAState));
    core->pc = MINIRV32_RAM_IMAGE_OFFSET;
    core->extraflags = 3;  /* Machine mode */

    /* Run emulator: 1 instruction per step, up to 1000 steps */
    done_flag = 0;
    for (i = 0; i < 1000 && !done_flag; i++) {
        int32_t ret = MiniRV32IMAStep(core, (uint8_t *)ram_words, 0, 1, 1);
        if (ret == 0x5555) {
            done_flag = 1;
        }
    }
}
