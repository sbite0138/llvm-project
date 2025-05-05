//===-- MtGFrameLowering.cpp - MtG Frame Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MtG implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "MtGFrameLowering.h"
#include "MCTargetDesc/MtGMCTargetDesc.h"
#include "MtGInstrInfo.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

MtGFrameLowering::MtGFrameLowering(const MtGSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(4), 0,
                          Align(4)),
      STI(STI), TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()) {}

void MtGFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MtGInstrInfo &TII =
      *static_cast<const MtGInstrInfo *>(STI.getInstrInfo());
  MFI.dump(MF);
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  unsigned SP = MtG::R8;
  uint64_t StackSize = MFI.getStackSize();
  if (StackSize == 0 && !MFI.adjustsStack())
    return;
  dbgs() << "[emitPrologue] StackSize: " << StackSize << "\n";
  const MCRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  TII.adjustStackPtr(SP, -StackSize, MBB, MBBI);
  unsigned CFIIndex =
      MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, -StackSize));
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex);

  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  if (CSI.size()) {
    for (unsigned i = 0; i < CSI.size(); ++i)
      ++MBBI;
    for (std::vector<CalleeSavedInfo>::const_iterator I = CSI.begin(),
                                                      E = CSI.end();
         I != E; ++I) {
      int64_t Offset = MFI.getObjectOffset(I->getFrameIdx());
      unsigned Reg = I->getReg();
      {
        // Reg is in CPURegs.
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, TRI->getDwarfRegNum(Reg, 1), Offset));
        BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
      }
    }
  }
}

void MtGFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  const MtGInstrInfo &TII =
      *static_cast<const MtGInstrInfo *>(STI.getInstrInfo());

  DebugLoc dl = MBBI->getDebugLoc();
  unsigned SP = MtG::R8;
  uint64_t StackSize = MFI.getStackSize();
  if (!StackSize)
    return;

  TII.adjustStackPtr(SP, StackSize, MBB, MBBI);
}

MachineBasicBlock::iterator MtGFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  unsigned SP = MtG::R8;
  if (!hasReservedCallFrame(MF)) {
    int64_t Amount = I->getOperand(0).getImm();
    if (I->getOpcode() == MtG::ADJCALLSTACKDOWN)
      Amount = -Amount;
    STI.getInstrInfo()->adjustStackPtr(SP, Amount, MBB, I);
  }
  return MBB.erase(I);
}