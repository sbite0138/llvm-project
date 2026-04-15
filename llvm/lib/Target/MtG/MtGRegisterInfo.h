//===-- MtGRegisterInfo.h - MtG Register Information Impl -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MtG implementation of the MRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MtG_MtGREGISTERINFO_H
#define LLVM_LIB_TARGET_MtG_MtGREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "MtGGenRegisterInfo.inc"

namespace llvm {

class MtGRegisterInfo : public MtGGenRegisterInfo {
public:
  MtGRegisterInfo();

  /// Code Generation virtual methods...
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // We eliminate byte-wise FI pseudos post-regalloc and need a scratch
  // register to hold the computed FP + offset address (MtG has no
  // base+offset store). Request a RegScavenger so the generic code runs it
  // with an emergency spill slot reserved.
  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool useFPForScavengingIndex(const MachineFunction &MF) const override {
    return true;
  }

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;

  // Byte-wise save/reload of a whole 32-bit value into the 4-byte
  // emergency-spill slot at *SP. These are used both by eliminateFrameIndex
  // (when a byte-wise FI pseudo can't find a free scratch and needs to
  // evict a live register) and by the byte-wise load/store macro
  // expansions in MtGInstrInfo when *they* can't find free scratches.
  //
  // The expansion itself touches only R0 (via NumBuild), R6 (Divide's
  // quotient on save, per-iteration byte temp on reload), R2 (SP, mutated
  // and restored), and VictimReg. The caller is responsible for ensuring
  // the emergency slot at *SP is available — i.e. the containing function
  // must have emitted a prologue that reserved it.
  static void emitEmergencySave(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator II,
                                const TargetInstrInfo &TII,
                                Register VictimReg);
  static void emitEmergencyReload(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator II,
                                  const TargetInstrInfo &TII,
                                  Register VictimReg);
};

} // end namespace llvm

#endif
