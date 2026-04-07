//===-- MtGMCAsmInfo.h - MtG asm properties --------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MtGMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MtG_MCTARGETDESC_MtGMCASMINFO_H
#define LLVM_LIB_TARGET_MtG_MCTARGETDESC_MtGMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class MtGMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit MtGMCAsmInfo(const Triple &TT);
};

} // namespace llvm

#endif
