//===-- MtGInstrInfo.cpp - MtG Instruction Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MtG implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "MtGInstrInfo.h"
#include "MCTargetDesc/MtGMCTargetDesc.h"
#include "MtG.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGRegisterInfo.h"
#include "MtGTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdlib>

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "MtGGenInstrInfo.inc"

// Pin the vtable to this file.
void MtGInstrInfo::anchor() {}

// MtGInstrInfo::MtGInstrInfo(MtGSubtarget &STI) : MtGGenInstrInfo(), RI() {}
MtGInstrInfo::MtGInstrInfo(MtGSubtarget &STI)
    : MtGGenInstrInfo(MtG::ADJCALLSTACKDOWN, MtG::ADJCALLSTACKUP), RI() {}

void MtGInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {

  // check RC is GRRegClass
  // assert(RC == &MtG::GRRegClass && "Can only store GRRegClass to stack
  // slot");
  // auto TmpReg2 = MtG::R10;
  assert(SrcReg != MtG::FLAG && "Cannot store FLAG register to stack slot");
  BuildMI(MBB, MI, MI->getDebugLoc(), get(MtG::STOREBYTEWISE_FI_MACRO))
      .addUse(SrcReg)
      .addFrameIndex(FrameIdx);
}

void MtGInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register DestReg, int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI,
                                        Register VReg) const {

  BuildMI(MBB, MI, MI->getDebugLoc(), get(MtG::LOADBYTEWISE_FI_MACRO), DestReg)
      .addFrameIndex(FrameIdx);
}

void MtGInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL, MCRegister DestReg,
                               MCRegister SrcReg, bool KillSrc,
                               bool RenamableDest, bool RenamableSrc) const {
  BuildMI(MBB, I, DL, get(MtG::MOVEREG_MACRO), DestReg)
      .addUse(SrcReg, getKillRegState(KillSrc));
}

bool MtGInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  // // llvm_unreachable("debug");
  const TargetInstrInfo &TII = *MF.getSubtarget<MtGSubtarget>().getInstrInfo();
  if (MI.getOpcode() == MtG::ADD_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
        .addImm(1UL << 32);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
        .addUse(DstReg)
        .addUse(MtG::R0);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::ADD_IMM_MACRO) {
    auto DstReg = MI.getOperand(1).getReg();
    auto SrcImm = MI.getOperand(2).getImm();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
        .addImm(SrcImm);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(MtG::R0);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
        .addImm(1UL << 32);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
        .addUse(DstReg)
        .addUse(MtG::R0);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::SUB_MACRO) {

    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NEG_MACRO), SrcReg)
        .addUse(SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
        .addImm(1UL << 32);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
        .addUse(DstReg)
        .addUse(MtG::R0);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::MULHI_MACRO) {
    llvm_unreachable("MULHI_MACRO not implemented yet");
  } else if (MI.getOpcode() == MtG::MULLO_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
        .addImm(1UL << 32);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
        .addUse(DstReg)
        .addUse(MtG::R0);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::DIV_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    assert(DstReg != MtG::R0 && "DstReg cannot be R0");
    assert(DstReg != MtG::R6 && "DstReg cannot be R6");
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE), MtG::R0)
        .addUse(SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
        .addUse(DstReg);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::REM_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE), MtG::R0)
        .addUse(SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
        .addUse(DstReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE), DstReg)
        .addUse(MtG::R6);
    MI.eraseFromParent();
    return true;
  }

  // if (MI.getOpcode() == MtG::ADD_PSEUDO) {
  //   auto DstReg = MI.getOperand(0).getReg();
  //   auto SrcReg = MI.getOperand(1).getReg();
  //   auto SrcImm = MI.getOperand(2).getImm();
  //   assert(DstReg == SrcReg);

  //   expandPostRAPseudo(*BuildMI(MBB, MI, MI.getDebugLoc(),
  //                               TII.get(MtG::NUMBUILD_PSEUDO), MtG::R0)
  //                           .addImm(SrcImm));
  //   BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
  //       .addUse(SrcReg)
  //       .addUse(MtG::R0);
  //   MI.eraseFromParent();
  //   return true;
  // }

  // if (MI.getOpcode() == MtG::SUB_PSEUDO) {
  //   auto DstReg = MI.getOperand(0).getReg();
  //   auto SrcReg = MI.getOperand(1).getReg();
  //   auto SrcImm = MI.getOperand(2).getImm();
  //   expandPostRAPseudo(*BuildMI(MBB, MI, MI.getDebugLoc(),
  //                               TII.get(MtG::NUMBUILD_PSEUDO), MtG::R0)
  //                           .addImm(SrcImm));
  //   BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SUB), DstReg)
  //       .addUse(SrcReg)
  //       .addUse(MtG::R0);
  //   MI.eraseFromParent();
  //   return true;
  // }

  // if (MI.getOpcode() == MtG::NUMBUILD_PSEUDO) {
  //   auto Imm = MI.getOperand(1).getImm();
  //   assert(Imm >= 0);

  //   // assert(Imm > 0);
  //   std::vector<unsigned> Digits;
  //   if (Imm == 0)
  //     Digits.push_back(0);
  //   else
  //     while (Imm > 0) {
  //       Digits.push_back(Imm % 144);
  //       Imm /= 144;
  //     }
  //   std::reverse(Digits.begin(), Digits.end());

  //   BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_INIT))
  //       .addDef(MI.getOperand(0).getReg())
  //       .addImm(Digits[0]);

  //   for (unsigned i = 1; i < Digits.size(); i++) {
  //     unsigned Digit = Digits[i];
  //     BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_SUCC))
  //         .addDef(MI.getOperand(0).getReg())
  //         .addUse(MI.getOperand(0).getReg(), RegState::Kill)
  //         .addImm(Digit);
  //   }
  //   MI.eraseFromParent();
  //   return true;
  // }

  // if (MI.getOpcode() == MtG::MOV_PSEUDO) {

  //   auto Imm = MI.getOperand(1).getImm();
  //   assert(Imm >= 0);
  //   // MI.dump();
  //   // llvm::dbgs() << "MOV_PSEUDO Imm: " << Imm << "\n";

  //   std::vector<unsigned> Digits;
  //   if (Imm == 0)
  //     Digits.push_back(0);
  //   else
  //     while (Imm > 0) {
  //       Digits.push_back(Imm % 144);
  //       Imm /= 144;
  //     }
  //   std::reverse(Digits.begin(), Digits.end());

  //   BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_INIT))
  //       .addDef(MtG::R0)
  //       .addImm(Digits[0]);

  //   for (unsigned i = 1; i < Digits.size(); i++) {
  //     unsigned Digit = Digits[i];
  //     BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_SUCC))
  //         .addDef(MtG::R0)
  //         .addUse(MtG::R0, RegState::Kill)
  //         .addImm(Digit);
  //   }
  //   BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOV_GG))
  //       .addDef(MI.getOperand(0).getReg())
  //       .addUse(MtG::R0);
  //   MI.eraseFromParent();
  //   return true;
  // }

  return false;
}

void MtGInstrInfo::adjustStackPtr(unsigned SP, int64_t Amount,
                                  MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  assert(isInt<32>(Amount));
  BuildMI(MBB, I, DL, get(MtG::ADD_IMM_MACRO), SP).addUse(SP).addImm(Amount);
}
