//===--- MtG.h - Declare MtG target feature support ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MtG TargetInfo. The MtG backend is an experimental
// LLVM target for the Magic: The Gathering Turing Machine (cyh31 / FUN'24);
// the data layout (32-bit pointers, 32-bit int / long, little-endian) mirrors
// what the backend declares in MtGTargetMachine.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MTG_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MTG_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY MtGTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];

public:
  MtGTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    TLSSupported = false;

    // The MtG backend treats every value as i32. Promoting char/short to
    // 32 bits would require lots of mask/sign-extend juggling we don't
    // need to support up front, but they're fine as load/store widths in
    // the type system. Pointers and int / long all match the backend's
    // 32-bit world.
    // MtG only has 32-bit native ops, so wider types are emulated and we
    // pick 32-bit alignment everywhere to match the backend's data layout.
    BoolWidth = BoolAlign = 8;
    ShortWidth = 16; ShortAlign = 16;
    IntWidth = IntAlign = 32;
    LongWidth = LongAlign = 32;
    LongLongWidth = 64; LongLongAlign = 32;
    FloatWidth = FloatAlign = 32;
    DoubleWidth = 64; DoubleAlign = 32;
    LongDoubleWidth = 64; LongDoubleAlign = 32;
    PointerWidth = PointerAlign = 32;
    SuitableAlign = 32;

    SizeType = UnsignedInt;
    IntMaxType = SignedLongLong;
    IntPtrType = SignedInt;
    PtrDiffType = SignedInt;
    SigAtomicType = SignedInt;

    // Mirror MtGTargetMachine::computeDataLayout exactly so the IR clang
    // produces lines up with what llc -mtriple=mtg expects.
    resetDataLayout("e-p:32:32-i32:32:32-i64:32:32-f64:32:32-n32-S32");
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasFeature(StringRef Feature) const override {
    return Feature == "mtg";
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override {
    switch (*Name) {
    case 'r': // GR class
      info.setAllowsRegister();
      return true;
    }
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }
};

} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MTG_H
