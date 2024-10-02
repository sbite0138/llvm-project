//===-- MtGTargetMachine.h - Define TargetMachine for MtG -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MtG specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_LIB_TARGET_MtG_MtGTARGETMACHINE_H
#define LLVM_LIB_TARGET_MtG_MtGTARGETMACHINE_H

#include "MtGSubtarget.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

namespace llvm {
class StringRef;

/// MtGTargetMachine
///
class MtGTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  MtGSubtarget Subtarget;

public:
  MtGTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                      StringRef FS, const TargetOptions &Options,
                      std::optional<Reloc::Model> RM,
                      std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                      bool JIT);
  ~MtGTargetMachine() override;

  const MtGSubtarget *getSubtargetImpl(const Function &F) const override {
    return &Subtarget;
  }
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;
}; // MtGTargetMachine.

} // end namespace llvm

#endif
