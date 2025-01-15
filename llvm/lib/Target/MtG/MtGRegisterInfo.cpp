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

using namespace llvm;

#define DEBUG_TYPE "mtg-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "MtGGenRegisterInfo.inc"

// FIXME: Provide proper call frame setup / destroy opcodes.
MtGRegisterInfo::MtGRegisterInfo() : MtGGenRegisterInfo(MtG::R11) {}

const MCPhysReg *
MtGRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const MtGFrameLowering *TFI = getFrameLowering(*MF);
  const Function *F = &MF->getFunction();
  static const MCPhysReg CalleeSavedRegs[] = {MtG::R4, MtG::R5, MtG::R6,
                                              MtG::R7, MtG::R8};
  return CalleeSavedRegs;
}

BitVector MtGRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  // Reserved.set(MtG::R0);

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
  if (MI.getOpcode() == MtG::ADD_OR_SUB_PSEUDO) {

    assert(i == 1);
    unsigned FrameReg = MtG::R11;
    const auto SrcReg = MI.getOperand(0).getReg();
    MBB.insert(II,
               BuildMI(MF, DL, TII->get(MtG::MOV_GG), SrcReg).addUse(FrameReg));

    int FrameIndex = MI.getOperand(i).getIndex();

    uint64_t stackSize = MF.getFrameInfo().getStackSize();
    int64_t spOffset = MF.getFrameInfo().getObjectOffset(FrameIndex);

    int64_t Offset;
    Offset = spOffset + (int64_t)stackSize;
    Offset += MI.getOperand(i + 1).getImm();
    Offset /= 4;
    if (!MI.isDebugValue() && !isInt<12>(Offset)) {
      assert("(!MI.isDebugValue() && !isInt<16>(Offset))");
    }
    MI.getOperand(i + 0).ChangeToRegister(SrcReg, false);
    MI.getOperand(i + 1).ChangeToImmediate(Offset);

    return true;
  } else if (MI.getOpcode() == MtG::CALC_FI_PSEUDO) {
    assert(i == 1);
    int FrameIndex = MI.getOperand(i).getIndex();
    auto TmpReg = MI.getOperand(i + 1).getReg();
    uint64_t stackSize = MF.getFrameInfo().getStackSize();
    int64_t spOffset = MF.getFrameInfo().getObjectOffset(FrameIndex);

    int64_t Offset;
    Offset = spOffset + (int64_t)stackSize;
    Offset /= 4;

    if (!MI.isDebugValue() && !isInt<12>(Offset)) {
      assert("(!MI.isDebugValue() && !isInt<12>(Offset))");
    }
    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_PSEUDO), MtG::R0)
                       .addImm(std::abs(Offset)));

    MBB.insert(II,
               BuildMI(MF, DL, TII->get(MtG::MOV_GG), TmpReg).addUse(MtG::R0));

    MBB.insert(
        II, BuildMI(MF, DL, TII->get(MtG::MOV_GG), MtG::R0).addUse(MtG::R11));

    const auto Opc = (Offset >= 0) ? MtG::ADD : MtG::SUB;
    MBB.insert(II, BuildMI(MF, DL, TII->get(Opc), MtG::R0)
                       .addUse(MtG::R0)
                       .addUse(TmpReg, RegState::Kill));

    MBB.erase(II);
    return true;
  }
  return false;
}

Register MtGRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // const MSP430FrameLowering *TFI = getFrameLowering(MF);
  return MtG::R11;
}
