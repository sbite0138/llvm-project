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
  // little endian, 32-bit pointers, 32-bit alignment for i32/i64/f64
  // (everything wider is emulated via 32-bit ops, so there's no benefit
  // to larger ABI alignment). Stack alignment 32 bits.
  return "e-p:32:32-i32:32:32-i64:32:32-f64:32:32-n32-S32";
}

MtGTargetMachine::MtGTargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &Options,
                                   std::optional<Reloc::Model> RM,
                                   std::optional<CodeModel::Model> CM,
                                   CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, computeDataLayout(TT, CPU, Options), TT, CPU, FS,
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
  return MtGMachineFunctionInfo::create<MtGMachineFunctionInfo>(Allocator, F,
                                                                STI);
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
  // Expand BR_PSEUDO / BRCOND_PSEUDO (and prepend NumBuild placeholders to
  // CALL_PSEUDO) after block placement so we pick forward/backward Jump
  // opcodes based on the final layout.
  addPass(createMtGExpandBranchPseudoPass());
  // Patch the NumBuild placeholders preceding each intra-function Jump and
  // each Return with their displacement (Jump: MBB-to-MBB; Return: entry-to-
  // this-return instruction count).
  //
  // Note: inter-function call displacements are NOT patched here. Same as
  // RISC-V deferring PC-relative resolution to its MC/linker layer, MtG
  // defers call-distance resolution to the ursa assembler (which already
  // does the equivalent of this for Jumps in its fixup_jumps pass). See
  // ursa/src/assembler.py — it will flip CallFwd ↔ CallBwdR based on the
  // sign of the computed displacement and patch the 2 leading NumBuilds.
  addPass(createMtGBranchSelectionPass());
}
