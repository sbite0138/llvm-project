//===-- MtGBranchSelector.cpp - Emit long conditional branches ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that scans a machine function to determine which
// conditional branches need more than 10 bits of displacement to reach their
// target basic block.  It does this in two passes; a calculation of basic block
// positions pass, and a branch pseudo op to machine branch opcode pass.  This
// pass should be run last, just before the assembly printer.
//
//===----------------------------------------------------------------------===//

#include "MtG.h"
#include "MtGInstrInfo.h"
#include "MtGSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "mtg-branch-select"

static cl::opt<bool>
    BranchSelectEnabled("mtg-branch-select", cl::Hidden, cl::init(true),
                        cl::desc("Expand out of range branches"));

STATISTIC(NumSplit, "Number of machine basic blocks split");
STATISTIC(NumExpanded, "Number of branches expanded to long format");

namespace {
class MtGBSel : public MachineFunctionPass {

  typedef SmallVector<int, 16> OffsetVector;

  MachineFunction *MF;
  const MtGInstrInfo *TII;

  unsigned measureFunction(OffsetVector &BlockOffsets,
                           MachineBasicBlock *FromBB = nullptr);
  bool expandBranches(OffsetVector &BlockOffsets);

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

static bool isInRage(int DistanceInBytes) {
  // According to CC430 Family User's Guide, Section 4.5.1.3, branch
  // instructions have the signed 10-bit word offset field, so first we need to
  // convert the distance from bytes to words, then check if it fits in 10-bit
  // signed integer.
  const int WordSize = 2;

  assert((DistanceInBytes % WordSize == 0) &&
         "Branch offset should be word aligned!");

  int Words = DistanceInBytes / WordSize;
  return isInt<10>(Words);
}

/// Measure each basic block, fill the BlockOffsets, and return the size of
/// the function, starting with BB
unsigned MtGBSel::measureFunction(OffsetVector &BlockOffsets,
                                  MachineBasicBlock *FromBB) {
  // Give the blocks of the function a dense, in-order, numbering.
  MF->RenumberBlocks(FromBB);

  MachineFunction::iterator Begin;
  if (FromBB == nullptr) {
    Begin = MF->begin();
  } else {
    Begin = FromBB->getIterator();
  }

  BlockOffsets.resize(MF->getNumBlockIDs());

  unsigned TotalSize = BlockOffsets[Begin->getNumber()];
  for (auto &MBB : make_range(Begin, MF->end())) {
    BlockOffsets[MBB.getNumber()] = TotalSize;
    for (MachineInstr &MI : MBB) {
      TotalSize += TII->getInstSizeInBytes(MI);
    }
  }
  return TotalSize;
}

/// Do expand branches and split the basic blocks if necessary.
/// Returns true if made any change.
bool MtGBSel::expandBranches(OffsetVector &BlockOffsets) {
  // TODO: Implement branch expansion when needed
  return false;
}

bool MtGBSel::runOnMachineFunction(MachineFunction &mf) {
  // TODO: Implement branch selection when needed
  return false;
}

/// Returns an instance of the Branch Selection Pass
FunctionPass *llvm::createMtGBranchSelectionPass() { return new MtGBSel(); }
