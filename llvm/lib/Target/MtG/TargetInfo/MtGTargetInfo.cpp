//===-- MtGTargetInfo.cpp - MtG Target Implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/MtGTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheMtGTarget() {
  static Target TheMtGTarget;
  return TheMtGTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMtGTargetInfo() {
  RegisterTarget<Triple::mtg> X(getTheMtGTarget(), "mtg",
                                   "MtG [experimental]", "MtG");
}
