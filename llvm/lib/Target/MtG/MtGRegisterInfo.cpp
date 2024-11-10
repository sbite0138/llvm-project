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
#include "MtG.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"
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
  static const MCPhysReg CalleeSavedRegs[] = {
      MtG::R4, MtG::R5, MtG::R6, MtG::R7, MtG::R8, MtG::R9, MtG::R10, MtG::R11};
  return CalleeSavedRegs;
}

BitVector MtGRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(MtG::R0);

  return Reserved;
}

bool MtGRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  return false;
}

Register MtGRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // const MSP430FrameLowering *TFI = getFrameLowering(MF);
  return MtG::R11;
}
