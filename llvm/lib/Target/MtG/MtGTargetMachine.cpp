//===-- MtGTargetMachine.cpp - Define TargetMachine for MtG ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Top-level implementation for the MtG target.
//
//===----------------------------------------------------------------------===//

#include "MtGTargetMachine.h"
#include "MtG.h"
#include "MtGMachineFunctionInfo.h"
#include "TargetInfo/MtGTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include <optional>
using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMtGTarget() {
  // Register the target.
  RegisterTargetMachine<MtGTargetMachine> X(getTheMtGTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeMtGDAGToDAGISelLegacyPass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static std::string computeDataLayout(const Triple &TT, StringRef CPU,
                                     const TargetOptions &Options) {
  return "e-m:e-p:16:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16";
}

MtGTargetMachine::MtGTargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT, CPU, Options), TT, CPU, FS,
                        Options, getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

MtGTargetMachine::~MtGTargetMachine() = default;

namespace {
/// MtG Code Generator Pass Configuration Options.
class MtGPassConfig : public TargetPassConfig {
public:
  MtGPassConfig(MtGTargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  MtGTargetMachine &getMtGTargetMachine() const {
    return getTM<MtGTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *MtGTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new MtGPassConfig(*this, PM);
}

MachineFunctionInfo *MtGTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return MtGMachineFunctionInfo::create<MtGMachineFunctionInfo>(Allocator,
                                                                      F, STI);
}

void MtGPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addIRPasses();
}

bool MtGPassConfig::addInstSelector() {
  // Install an instruction selector.
  addPass(createMtGISelDag(getMtGTargetMachine(), getOptLevel()));
  return false;
}

void MtGPassConfig::addPreEmitPass() {
  // Must run branch selection immediately preceding the asm printer.
  addPass(createMtGBranchSelectionPass());
}
