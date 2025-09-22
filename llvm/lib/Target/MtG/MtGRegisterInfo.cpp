//===-- MtGRegisterInfo.cpp - MtG Register Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MtG implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "MtGRegisterInfo.h"
#include "MCTargetDesc/MtGMCTargetDesc.h"
#include "MtG.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mtg-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "MtGGenRegisterInfo.inc"

// FIXME: Provide proper call frame setup / destroy opcodes.
MtGRegisterInfo::MtGRegisterInfo() : MtGGenRegisterInfo(0) {}

const MCPhysReg *
MtGRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const MtGFrameLowering *TFI = getFrameLowering(*MF);
  const Function *F = &MF->getFunction();
  static const MCPhysReg CalleeSavedRegs[] = {
      MtG::R1, MtG::R2, MtG::R3, MtG::R4,  MtG::R5, MtG::R6,
      MtG::R7, MtG::R8, MtG::R9, MtG::R10, MtG::R11};
  return CalleeSavedRegs;
}

BitVector MtGRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(MtG::FLAG);
  Reserved.set(MtG::R0);
  Reserved.set(MtG::R1);
  Reserved.set(MtG::R2);
  Reserved.set(MtG::R3);
  Reserved.set(MtG::R4);
  Reserved.set(MtG::R5);
  Reserved.set(MtG::R6);

  return Reserved;
}

bool MtGRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc DL = MI.getDebugLoc();
  MachineBasicBlock &MBB = *MI.getParent();
  auto *TII = MF.getSubtarget().getInstrInfo();
  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  if (MI.getOpcode() == MtG::ADD_MACRO_FI) {
    const auto DstReg = MI.getOperand(0).getReg();
    const auto FrameIndex = MI.getOperand(1).getIndex();
    const auto Offset = MI.getOperand(2).getImm();
    const int64_t totalOffset =
        MFI.getObjectOffset(FrameIndex) + MFI.getStackSize() + SPAdj + Offset;

    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MOVE), DstReg)
                       .addUse(getFrameRegister(MF)));

    MBB.insert(
        II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO)).addImm(totalOffset));

    if (!MI.isDebugValue() && !isInt<16>(Offset)) {
      assert("(!MI.isDebugValue() && !isInt<16>(Offset))");
    }
    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::ADD_MACRO), DstReg)
                       .addReg(DstReg)
                       .addReg(MtG::R0));
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::STOREBYTEWISE_FI_MACRO ||
             MI.getOpcode() == MtG::LOADBYTEWISE_FI_MACRO) {
    const auto OpReg = MI.getOperand(0).getReg();
    const auto FrameIndex = MI.getOperand(1).getIndex();
    const int64_t totalOffset =
        MFI.getObjectOffset(FrameIndex) + MFI.getStackSize() + SPAdj;

    const auto MRI = &MF.getRegInfo();
    const auto tmpReg = MtG::R3; // MRI->createVirtualRegister(&MtG::GRRegClass);

    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MOVE), tmpReg)
                       .addUse(getFrameRegister(MF)));
    MBB.insert(
        II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO)).addImm(totalOffset));

    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::ADD_MACRO), tmpReg)
                       .addReg(tmpReg)
                       .addReg(MtG::R0));

    if (MI.getOpcode() == MtG::STOREBYTEWISE_FI_MACRO) {
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::STOREBYTEWISE_MACRO))
                         .addUse(tmpReg)
                         .addUse(OpReg));
    } else {
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::LOADBYTEWISE_MACRO), OpReg)
                         .addUse(tmpReg));
    }
    MI.eraseFromParent();

    return true;
  }
  assert(false && "Unknown FrameIndex elimination!");
  return false;
}

Register MtGRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return MtG::R1;
}
