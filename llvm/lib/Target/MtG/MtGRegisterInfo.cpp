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
  static const MCPhysReg CalleeSavedRegs[] = {
      MtG::R4, MtG::R5, MtG::R6, MtG::R7, MtG::R8, MtG::R9, MtG::R10, MtG::R11};
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
  auto *TII = MF.getSubtarget().getInstrInfo();

  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  LLVM_DEBUG(errs() << "\nFunction : " << MF.getFunction().getName() << "\n";
             errs() << "<--------->\n"
                    << MI);
  int FrameIndex = MI.getOperand(i).getIndex();

  uint64_t stackSize = MF.getFrameInfo().getStackSize();
  int64_t spOffset = MF.getFrameInfo().getObjectOffset(FrameIndex);
  LLVM_DEBUG(errs() << "FrameIndex : " << FrameIndex << "\n"
                    << "spOffset   : " << spOffset << "\n"
                    << "stackSize  : " << stackSize << "\n");
  unsigned FrameReg = MtG::R11;

  int64_t Offset;
  Offset = spOffset + (int64_t)stackSize;
  Offset += MI.getOperand(i + 1).getImm();
  LLVM_DEBUG(errs() << "Offset     : " << Offset << "\n"
                    << "<--------->\n");

  if (!MI.isDebugValue() && !isInt<12>(Offset)) {
    assert("(!MI.isDebugValue() && !isInt<16>(Offset))");
  }

  dbgs() << "[debug] MI: ";
  MI.dump();
  dbgs() << "i: " << i << "\n";

  MI.getOperand(i + 0).ChangeToRegister(FrameReg, false);
  if (Offset < 0) {
    // MI.setDesc(TII->get(MtG::SUB_PSEUDO));
    MI.getOperand(i + 1).ChangeToImmediate(Offset);

  } else {
    // MI.setDesc(TII->get(MtG::ADD_PSEUDO));
    MI.getOperand(i + 1).ChangeToImmediate(Offset);
  }
  // change opcode to ADD_PSEUDO

  dbgs() << "done\n";
  return true;
}

Register MtGRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // const MSP430FrameLowering *TFI = getFrameLowering(MF);
  return MtG::R11;
}
