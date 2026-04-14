//===--- MtG.cpp - Implement MtG target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MtG.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

const char *const MtGTargetInfo::GCCRegNames[] = {
    "r0", "r1", "r2", "r3", "r4",  "r5",
    "r6", "r7", "r8", "r9", "r10", "r11",
};

ArrayRef<const char *> MtGTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

void MtGTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("MtG");
  Builder.defineMacro("__MtG__");
  Builder.defineMacro("__mtg__");
}
