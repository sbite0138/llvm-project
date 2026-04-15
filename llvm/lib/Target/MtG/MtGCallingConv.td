//==- MtGCallingConv.td - Calling Conventions for MtG -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// MtG uses a stack-only argument passing convention.
//
// Rationale: MtG has no cheap base+offset addressing and every memory access
// already expands to a byte-wise macro, so the relative cost of passing args
// through registers versus memory is small. Keeping the ABI trivial means
// CallingConv.td is one line per kind, and we never have to teach callers to
// save/restore an argument-register set.
//
// Rules:
//   * Every i32 argument is assigned a 4-byte stack slot, in order.
//   * Larger-than-i32 types (i64, etc.) are legalised into 32-bit pieces
//     upstream; each piece then follows the i32 rule. For arguments this
//     happens automatically at stack-slot assignment.
//   * The i32 return value goes in R8. i64 returns are split into a
//     (low, high) pair in (R8, R9).
//   * A call clobbers R0 (macro scratch), FLAG, and every allocatable
//     register — equivalent to "no callee-saved registers". Caller-saved
//     handling is expressed via CALL_PSEUDO's Defs list, not here.
//===----------------------------------------------------------------------===//

def RetCC_MtG : CallingConv<[
  CCIfType<[i32], CCAssignToReg<[R8, R9]>>
]>;

def CC_MtG : CallingConv<[
  CCIfType<[i32], CCAssignToStack<4, 4>>
]>;
