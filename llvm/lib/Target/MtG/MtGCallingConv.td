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

]>;

//===----------------------------------------------------------------------===//
// MtG Argument Calling Conventions
//===----------------------------------------------------------------------===//
def CC_MtG_AssignStack : CallingConv<[
  // Pass by value if the byval attribute is given
  CCIfByVal<CCPassByVal<2, 2>>,

  // Promote i8 arguments to i16.
  CCIfType<[i8], CCPromoteToType<i16>>,

  // Integer values get stored in stack slots that are 2 bytes in
  // size and 2-byte aligned.
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;

