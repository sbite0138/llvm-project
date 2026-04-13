//===-- MtGExpandBranchPseudo.cpp - Expand BR_PSEUDO / BRCOND_PSEUDO ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MtG ISA has separate forward/backward jump opcodes (JumpFwd, JumpBwd,
// JumpFwdNF, JumpBwdNF, JumpFwdF, JumpBwdF). Direction must therefore be
// baked into the opcode, and that decision depends on the final block layout.
//
// This pass runs in addPreEmitPass, after MachineBlockPlacement has finalized
// block order, so we can correctly pick forward vs. backward opcodes when
// expanding BR_PSEUDO and BRCOND_PSEUDO.
//
//===----------------------------------------------------------------------===//

#include "MtG.h"
#include "MtGInstrInfo.h"
#include "MtGSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "mtg-expand-branch-pseudo"

namespace {
class MtGExpandBranchPseudo : public MachineFunctionPass {
public:
  static char ID;
  MtGExpandBranchPseudo() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "MtG Expand Branch Pseudo";
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  bool expand(MachineBasicBlock &MBB, MachineInstr &MI,
              const TargetInstrInfo &TII,
              const DenseMap<const MachineBasicBlock *, unsigned> &LayoutPos);
};
} // end anonymous namespace

char MtGExpandBranchPseudo::ID = 0;

bool MtGExpandBranchPseudo::expand(
    MachineBasicBlock &MBB, MachineInstr &MI, const TargetInstrInfo &TII,
    const DenseMap<const MachineBasicBlock *, unsigned> &LayoutPos) {
  unsigned Opc = MI.getOpcode();
  DebugLoc DL = MI.getDebugLoc();

  auto isForwardTo = [&](const MachineBasicBlock *TargetMBB) -> bool {
    return LayoutPos.lookup(TargetMBB) > LayoutPos.lookup(&MBB);
  };

  if (Opc == MtG::BR_PSEUDO) {
    MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
    unsigned JumpOpc = isForwardTo(TargetMBB) ? MtG::JUMPFWD : MtG::JUMPBWD;

    BuildMI(MBB, MI, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
    BuildMI(MBB, MI, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
    BuildMI(MBB, MI, DL, TII.get(JumpOpc)).addMBB(TargetMBB);
    MI.eraseFromParent();
    return true;
  }

  if (Opc == MtG::BRCOND_PSEUDO) {
    Register CondReg = MI.getOperand(0).getReg();
    int64_t Polarity = MI.getOperand(1).getImm();
    MachineBasicBlock *TargetMBB = MI.getOperand(2).getMBB();
    bool IsForward = isForwardTo(TargetMBB);

    // Polarity 0: branch if CondReg != 0 → "jump if !FLAG" after FIsZero.
    // Polarity 1: branch if CondReg == 0 → "jump if FLAG" after FIsZero.
    unsigned JumpOpc;
    if (Polarity == 0)
      JumpOpc = IsForward ? MtG::JUMPFWDNF : MtG::JUMPBWDNF;
    else
      JumpOpc = IsForward ? MtG::JUMPFWDF : MtG::JUMPBWDF;

    BuildMI(MBB, MI, DL, TII.get(MtG::FISZERO)).addReg(CondReg);
    BuildMI(MBB, MI, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
    BuildMI(MBB, MI, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
    BuildMI(MBB, MI, DL, TII.get(JumpOpc)).addMBB(TargetMBB);
    MI.eraseFromParent();
    return true;
  }

  return false;
}

bool MtGExpandBranchPseudo::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  // Record layout position of each block at pass-entry time.
  DenseMap<const MachineBasicBlock *, unsigned> LayoutPos;
  unsigned Pos = 0;
  for (const MachineBasicBlock &MBB : MF)
    LayoutPos[&MBB] = Pos++;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      if (MI.getOpcode() == MtG::BR_PSEUDO ||
          MI.getOpcode() == MtG::BRCOND_PSEUDO) {
        Changed |= expand(MBB, MI, TII, LayoutPos);
      }
    }
  }
  return Changed;
}

FunctionPass *llvm::createMtGExpandBranchPseudoPass() {
  return new MtGExpandBranchPseudo();
}
