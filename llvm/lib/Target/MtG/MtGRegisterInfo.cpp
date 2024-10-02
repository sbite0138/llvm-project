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
MtGRegisterInfo::MtGRegisterInfo()
  : MtGGenRegisterInfo(MtG::PC) {}

const MCPhysReg*
MtGRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const MtGFrameLowering *TFI = getFrameLowering(*MF);
  const Function* F = &MF->getFunction();
  static const MCPhysReg CalleeSavedRegs[] = {
    MtG::R4, MtG::R5, MtG::R6, MtG::R7,
    MtG::R8, MtG::R9, MtG::R10,
    0
  };
  static const MCPhysReg CalleeSavedRegsFP[] = {
    MtG::R5, MtG::R6, MtG::R7,
    MtG::R8, MtG::R9, MtG::R10,
    0
  };
  static const MCPhysReg CalleeSavedRegsIntr[] = {
    MtG::R4,  MtG::R5,  MtG::R6,  MtG::R7,
    MtG::R8,  MtG::R9,  MtG::R10, MtG::R11,
    MtG::R12, MtG::R13, MtG::R14, MtG::R15,
    0
  };
  static const MCPhysReg CalleeSavedRegsIntrFP[] = {
    MtG::R5,  MtG::R6,  MtG::R7,
    MtG::R8,  MtG::R9,  MtG::R10, MtG::R11,
    MtG::R12, MtG::R13, MtG::R14, MtG::R15,
    0
  };

  if (TFI->hasFP(*MF))
    return (F->getCallingConv() == CallingConv::MtG_INTR ?
            CalleeSavedRegsIntrFP : CalleeSavedRegsFP);
  else
    return (F->getCallingConv() == CallingConv::MtG_INTR ?
            CalleeSavedRegsIntr : CalleeSavedRegs);

}

BitVector MtGRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const MtGFrameLowering *TFI = getFrameLowering(MF);

  // Mark 4 special registers with subregisters as reserved.
  Reserved.set(MtG::PCB);
  Reserved.set(MtG::SPB);
  Reserved.set(MtG::SRB);
  Reserved.set(MtG::CGB);
  Reserved.set(MtG::PC);
  Reserved.set(MtG::SP);
  Reserved.set(MtG::SR);
  Reserved.set(MtG::CG);

  // Mark frame pointer as reserved if needed.
  if (TFI->hasFP(MF)) {
    Reserved.set(MtG::R4B);
    Reserved.set(MtG::R4);
  }

  return Reserved;
}

const TargetRegisterClass *
MtGRegisterInfo::getPointerRegClass(const MachineFunction &MF, unsigned Kind)
                                                                         const {
  return &MtG::GR16RegClass;
}

bool
MtGRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                        int SPAdj, unsigned FIOperandNum,
                                        RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const MtGFrameLowering *TFI = getFrameLowering(MF);
  DebugLoc dl = MI.getDebugLoc();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  unsigned BasePtr = (TFI->hasFP(MF) ? MtG::R4 : MtG::SP);
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);

  // Skip the saved PC
  Offset += 2;

  if (!TFI->hasFP(MF))
    Offset += MF.getFrameInfo().getStackSize();
  else
    Offset += 2; // Skip the saved FP

  // Fold imm into offset
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  if (MI.getOpcode() == MtG::ADDframe) {
    // This is actually "load effective address" of the stack slot
    // instruction. We have only two-address instructions, thus we need to
    // expand it into mov + add
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

    MI.setDesc(TII.get(MtG::MOV16rr));
    MI.getOperand(FIOperandNum).ChangeToRegister(BasePtr, false);

    // Remove the now unused Offset operand.
    MI.removeOperand(FIOperandNum + 1);

    if (Offset == 0)
      return false;

    // We need to materialize the offset via add instruction.
    Register DstReg = MI.getOperand(0).getReg();
    if (Offset < 0)
      BuildMI(MBB, std::next(II), dl, TII.get(MtG::SUB16ri), DstReg)
        .addReg(DstReg).addImm(-Offset);
    else
      BuildMI(MBB, std::next(II), dl, TII.get(MtG::ADD16ri), DstReg)
        .addReg(DstReg).addImm(Offset);

    return false;
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(BasePtr, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register MtGRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const MtGFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? MtG::R4 : MtG::SP;
}
