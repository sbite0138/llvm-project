//===-- MtGBranchSelector.cpp - Patch Jump/Return displacement NumBuilds --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Every MtG Jump* instruction is emitted by MtGExpandBranchPseudo (and by
// the ret-sequence expander) as:
//
//   NumBuild 0, 0   ; high base-144 digit of Z (placeholder)
//   NumBuild 0, 0   ; low  base-144 digit of Z (placeholder)
//   Jump{Fwd,Bwd}{,F,NF} target
//
// MtG hardware evaluates branches as PC += 3*r0 (forward) or PC -= 3*r0
// (backward), where r0 has been built up by the two preceding NumBuilds:
//   r0 = (12*Y1 + Z1) * 144 + (12*Y2 + Z2)
//
// The ret expansion uses the same NumBuild-pair / placeholder scheme for
// Return's Z' field (Z=0 ⇒ "use r0", so we can encode an arbitrary instr
// count). The Z' value for a Return is the distance in instructions from
// the function entry to that Return.
//
// This pass runs last (right before the asm printer), assigns each real
// instruction a linear position, and patches both kinds of placeholders.
// Inter-function call displacements are NOT handled here — they require
// module-wide layout knowledge and live in MtGCallSelector.
//
//===----------------------------------------------------------------------===//

#include "MtG.h"
#include "MtGInstrInfo.h"
#include "MtGSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

#define DEBUG_TYPE "mtg-branch-select"

STATISTIC(NumPatched, "Number of branches patched with PC-relative offsets");

namespace {
class MtGBSel : public MachineFunctionPass {
public:
  static char ID;
  MtGBSel() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override { return "MtG Branch Selector"; }
};
char MtGBSel::ID = 0;
} // namespace

// Return true if MI occupies a slot in the emitted instruction stream.
// Meta instructions (DBG_VALUE, CFI, labels, KILL, IMPLICIT_DEF, etc.) do not.
static bool isCountedInstruction(const MachineInstr &MI) {
  return !MI.isMetaInstruction();
}

// Classify a Jump* opcode. Returns false if MI is not a jump we patch.
static bool isJump(unsigned Opc, bool &IsForward) {
  switch (Opc) {
  case MtG::JUMPFWD:
  case MtG::JUMPFWDNF:
  case MtG::JUMPFWDF:
    IsForward = true;
    return true;
  case MtG::JUMPBWD:
  case MtG::JUMPBWDNF:
  case MtG::JUMPBWDF:
    IsForward = false;
    return true;
  default:
    return false;
  }
}

// Patch the two NumBuild placeholders immediately preceding MI so that r0
// evaluates to Value when MI executes. Returns true on success.
static bool patchPrecedingNumBuildPair(MachineInstr &MI,
                                       MachineBasicBlock &MBB, int64_t Value) {
  if (Value < 0) {
    errs() << "MtGBranchSelector: negative value " << Value
           << " for displacement placeholder: " << MI;
    return false;
  }
  if (Value >= 144 * 144) {
    // Exceeded the 2-digit NumBuild encoding range. Supporting longer
    // displacements would require emitting more NumBuild digits, which
    // in turn changes instruction counts and needs iteration to converge.
    // Out of scope for now.
    report_fatal_error("MtG branch/return displacement exceeds 2-digit "
                       "NumBuild encoding; wider offsets are not yet "
                       "supported");
  }

  unsigned HighDigit = Value / 144;
  unsigned LowDigit = Value % 144;

  auto StepBackToCounted = [](MachineBasicBlock::iterator It,
                              MachineBasicBlock::iterator Begin)
      -> MachineBasicBlock::iterator {
    while (It != Begin) {
      --It;
      if (isCountedInstruction(*It))
        return It;
    }
    return Begin;
  };

  auto MBBBegin = MBB.begin();
  auto It = MI.getIterator();
  if (It == MBBBegin)
    return false;
  auto NB2It = StepBackToCounted(It, MBBBegin);
  if (NB2It == MBBBegin || NB2It->getOpcode() != MtG::NUMBUILD)
    return false;
  auto NB1It = StepBackToCounted(NB2It, MBBBegin);
  if (NB1It == NB2It || NB1It->getOpcode() != MtG::NUMBUILD)
    return false;

  NB1It->getOperand(0).setImm(HighDigit / 12);
  NB1It->getOperand(1).setImm(HighDigit % 12);
  NB2It->getOperand(0).setImm(LowDigit / 12);
  NB2It->getOperand(1).setImm(LowDigit % 12);
  return true;
}

bool MtGBSel::runOnMachineFunction(MachineFunction &MF) {
  // Assign each counted instruction a linear position within the function,
  // and record each block's starting position.
  DenseMap<const MachineInstr *, unsigned> InstrPos;
  DenseMap<const MachineBasicBlock *, unsigned> BlockStartPos;
  unsigned Pos = 0;
  for (const MachineBasicBlock &MBB : MF) {
    BlockStartPos[&MBB] = Pos;
    for (const MachineInstr &MI : MBB) {
      if (!isCountedInstruction(MI))
        continue;
      InstrPos[&MI] = Pos;
      ++Pos;
    }
  }

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      bool IsForward;
      if (isJump(Opc, IsForward)) {
        assert(MI.getNumOperands() >= 1 && MI.getOperand(0).isMBB() &&
               "Jump operand must be an MBB");
        const MachineBasicBlock *Target = MI.getOperand(0).getMBB();

        unsigned JumpPos = InstrPos.lookup(&MI);
        unsigned TargetPos = BlockStartPos.lookup(Target);

        // PC after the jump instruction points at JumpPos + 1.
        //   Forward:  PC + 3Z = TargetPos  →  Z = TargetPos - (JumpPos + 1)
        //   Backward: PC - 3Z = TargetPos  →  Z = (JumpPos + 1) - TargetPos
        int64_t Z;
        if (IsForward)
          Z = (int64_t)TargetPos - (int64_t)(JumpPos + 1);
        else
          Z = (int64_t)(JumpPos + 1) - (int64_t)TargetPos;

        if (patchPrecedingNumBuildPair(MI, MBB, Z)) {
          ++NumPatched;
          Changed = true;
        }
      } else if (Opc == MtG::RETURN) {
        // Return Z' in the ret-pseudo expansion always uses Z'=0 (i.e. read
        // from r0), so the preceding 2 NumBuilds encode the instruction
        // count from the function entry (position 0) to this Return's
        // position. That's exactly InstrPos[MI].
        unsigned RetPos = InstrPos.lookup(&MI);
        if (patchPrecedingNumBuildPair(MI, MBB, (int64_t)RetPos)) {
          ++NumPatched;
          Changed = true;
        }
      }
    }
  }
  return Changed;
}

FunctionPass *llvm::createMtGBranchSelectionPass() { return new MtGBSel(); }
