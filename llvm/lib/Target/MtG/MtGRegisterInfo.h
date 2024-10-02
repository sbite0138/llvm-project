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
  const TargetRegisterClass*
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II,
                           int SPAdj, unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;
};

} // end namespace llvm

#endif
