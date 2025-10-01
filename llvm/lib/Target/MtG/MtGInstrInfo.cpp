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
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

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
  const TargetInstrInfo &TII = *MF.getSubtarget<MtGSubtarget>().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  auto BuildSafeMove = [&](MachineBasicBlock &MBB, MachineInstr &MI,
                           const DebugLoc &DL, const TargetInstrInfo &TII,
                           MCRegister DstReg, MCRegister SrcReg) {
    if (DstReg != SrcReg)
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE), DstReg)
          .addUse(SrcReg);
  };
  if (MI.getOpcode() == MtG::ADD_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32);
    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    MI.eraseFromParent();
    expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    return true;
  } else if (MI.getOpcode() == MtG::ADD_IMM_MACRO) {
    auto DstReg = MI.getOperand(1).getReg();
    auto SrcImm = MI.getOperand(2).getImm();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    std::vector<MachineInstr *> NumBuildMIs;
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(SrcImm));
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(MtG::R0);
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32));

    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    for (auto *NumBuildMI : NumBuildMIs)
      expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::SUB_MACRO) {

    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NEG_MACRO), SrcReg)
        .addUse(SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32);

    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::MULHI_MACRO) {
    llvm_unreachable("MULHI_MACRO not implemented yet");
  } else if (MI.getOpcode() == MtG::MULLO_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32);
    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::DIV_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");
    assert(DstReg != MtG::R0 && "DstReg cannot be R0");
    assert(DstReg != MtG::R6 && "DstReg cannot be R6");

    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R0, SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
        .addUse(DstReg);
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, DstReg, MtG::R6);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::STOREBYTEWISE_MACRO) {
    auto ValReg = MI.getOperand(0).getReg();
    auto AddrReg = MI.getOperand(1).getReg();
    // print register in string format
    std::set<Register> UseRegs = {MtG::R0, MtG::R3, MtG::R4, MtG::R5, MtG::R6};

    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R5, AddrReg);
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R3, AddrReg);
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R4, ValReg);

    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(256);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), MtG::R4)
        .addUse(MtG::R4);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::STORE))
        .addUse(MtG::R3)
        .addUse(MtG::R4);

    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R4, MtG::R6);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD1), MtG::R3)
        .addUse(MtG::R3);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), MtG::R4)
        .addUse(MtG::R4);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::STORE))
        .addUse(MtG::R3)
        .addUse(MtG::R4);
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R4, MtG::R6);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD1), MtG::R3)
        .addUse(MtG::R3);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), MtG::R4)
        .addUse(MtG::R4);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::STORE))
        .addUse(MtG::R3)
        .addUse(MtG::R4);
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R4, MtG::R6);

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD1), MtG::R3)
        .addUse(MtG::R3);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), MtG::R4)
        .addUse(MtG::R4);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::STORE))
        .addUse(MtG::R3)
        .addUse(MtG::R4);
    assert((UseRegs.count(AddrReg) == 0 || !isRegisterLiveAfter(MI, AddrReg)) &&
           "Invalid AddrReg");
    assert((UseRegs.count(ValReg) == 0 || !isRegisterLiveAfter(MI, ValReg)) &&
           "Invalid ValReg");
    expandPostRAPseudo(*NumBuildMI);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::LOADBYTEWISE_MACRO) {
    auto ValReg = MI.getOperand(0).getReg();
    auto AddrReg = MI.getOperand(1).getReg();
    llvm::dbgs() << "Expanding LOADBYTEWISE_MACRO\n";
    llvm::dbgs() << "AddrReg" << TRI->getName(AddrReg) << "\n";
    llvm::dbgs() << "ValReg" << TRI->getName(ValReg) << "\n";

    std::vector<Register> WorkRegs = {MtG::R3, MtG::R4, MtG::R5, MtG::R6};
    erase(WorkRegs, AddrReg);
    erase(WorkRegs, ValReg);
    const auto AddrWorkReg = WorkRegs.at(0);
    const auto ValWorkReg = WorkRegs.at(1);
    std::vector<MachineInstr *> NumBuildMIs;
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, AddrWorkReg, AddrReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ZERO), ValReg);
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(3));

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), AddrWorkReg)
        .addUse(AddrWorkReg)
        .addUse(MtG::R0);
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(256));
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::LOAD), ValWorkReg)
        .addUse(AddrWorkReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), ValReg)
        .addUse(ValReg)
        .addUse(ValWorkReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), ValReg)
        .addUse(ValReg)
        .addUse(MtG::R0);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SUB1COND), AddrWorkReg)
        .addUse(AddrWorkReg);

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::LOAD), ValWorkReg)
        .addUse(AddrWorkReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), ValReg)
        .addUse(ValReg)
        .addUse(ValWorkReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), ValReg)
        .addUse(ValReg)
        .addUse(MtG::R0);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SUB1COND), AddrWorkReg)
        .addUse(AddrWorkReg);

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::LOAD), ValWorkReg)
        .addUse(AddrWorkReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), ValReg)
        .addUse(ValReg)
        .addUse(ValWorkReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), ValReg)
        .addUse(ValReg)
        .addUse(MtG::R0);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SUB1COND), AddrWorkReg)
        .addUse(AddrWorkReg);

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::LOAD), ValWorkReg)
        .addUse(AddrWorkReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), ValReg)
        .addUse(ValReg)
        .addUse(ValWorkReg);

    for (auto *NumBuildMI : NumBuildMIs)
      expandPostRAPseudo(*NumBuildMI);

    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::REM_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0, MtG::R6};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R0, SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
        .addUse(DstReg);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::NUMBUILD_MACRO) {
    auto OrgImm = MI.getOperand(0).getImm();
    if (OrgImm < 0) {
      OrgImm = (1ULL << 32) + OrgImm;
    }
    std::vector<uint32_t> Imms;
    while (OrgImm) {
      Imms.push_back(OrgImm % 144);
      OrgImm /= 144;
    }
    if (Imms.size() == 0) {
      Imms.push_back(0);
    }
    std::reverse(Imms.begin(), Imms.end());
    for (auto Imm : Imms) {
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD))
          .addImm(Imm / 12)
          .addImm(Imm % 12);
    }
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::EQ_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg1 = MI.getOperand(1).getReg();
    auto SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg1)
        .addUse(SrcReg2);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg2)
        .addUse(SrcReg1);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETNF), DstReg);
    MI.eraseFromParent();
  } else if (MI.getOpcode() == MtG::NEQ_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg1 = MI.getOperand(1).getReg();
    auto SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg1)
        .addUse(SrcReg2);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg2)
        .addUse(SrcReg1);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
  } else if (MI.getOpcode() == MtG::LT_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg1 = MI.getOperand(1).getReg();
    auto SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg1)
        .addUse(SrcReg2);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
  } else if (MI.getOpcode() == MtG::GT_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg1 = MI.getOperand(1).getReg();
    auto SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg2)
        .addUse(SrcReg1);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
  } else if (MI.getOpcode() == MtG::CALL_PSEUDO) {
    auto Callee = MI.getOperand(0).getGlobal()->getName();
    if (Callee == "wrap_putchar") {
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::OUTPUT)).addUse(MtG::R8);
      MI.eraseFromParent();
      return true;
    }
    return false;
  } else if (MI.getOpcode() == MtG::RET_PSEUDO) {
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD))
        .addImm(0)
        .addImm(0);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD))
        .addImm(0)
        .addImm(0);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::RETURN));
    MI.eraseFromParent();
    return true;
  }

  return false;
}

bool MtGInstrInfo::isRegisterLiveAfter(const MachineInstr &MI,
                                       Register Reg) const {
  const MachineBasicBlock *MBB = MI.getParent();
  const MachineFunction *MF = MBB->getParent();

  // Analyze within the same basic block
  MachineBasicBlock::const_iterator NextI = std::next(MI.getIterator());
  for (auto I = NextI; I != MBB->end(); ++I) {
    // Check if the register is read (used)
    if (I->readsRegister(Reg, &getRegisterInfo())) {
      llvm::dbgs() << "Register " << printReg(Reg, &getRegisterInfo())
                   << " is live after instruction: " << *I << "\n";
      llvm::dbgs() << "In function: " << MF->getName() << "\n";
      llvm::dbgs() << "In basic block: " << MBB->getName() << "\n";
      llvm::dbgs() << "In instruction: ";
      MI.dump();

      MF->dump();
      return true;
    }
    // Check if the register is redefined (killed)
    if (I->definesRegister(Reg, &getRegisterInfo())) {
      return false;
    }
  }

  // Check across basic block boundaries
  // If the register is live-in to any successor block, it's live
  for (MachineBasicBlock *Succ : MBB->successors()) {
    if (Succ->isLiveIn(Reg)) {
      llvm::dbgs() << "Register " << printReg(Reg, &getRegisterInfo())
                   << " is live after instruction: " << *Succ << "\n";
      llvm::dbgs() << "In function: " << MF->getName() << "\n";
      llvm::dbgs() << "In basic block: " << MBB->getName() << "\n";
      llvm::dbgs() << "In instruction: ";
      MI.dump();
      MF->dump();
      return true;
    }
  }

  return false;
}

void MtGInstrInfo::adjustStackPtr(unsigned SP, int64_t Amount,
                                  MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  assert(isInt<32>(Amount));
  BuildMI(MBB, I, DL, get(MtG::ADD_IMM_MACRO), SP).addUse(SP).addImm(Amount);
}
