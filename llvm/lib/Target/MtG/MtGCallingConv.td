//==- MtGCallingConv.td - Calling Conventions for MtG -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for MtG architecture.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MtG Return Value Calling Convention
//===----------------------------------------------------------------------===//
def RetCC_MtG : CallingConv<[
  CCIfType<[i32], CCAssignToReg<[R1,R2]>>
]>;

//===----------------------------------------------------------------------===//
// MtG Argument Calling Conventions
//===----------------------------------------------------------------------===//
def CC_MtG : CallingConv<[
  CCIfType<[i32], CCAssignToReg<[R1,R2]>>,
  CCIfType<[i32], CCAssignToStack<4, 4>>,
]>;

