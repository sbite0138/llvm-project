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
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

MtGFrameLowering::MtGFrameLowering(const MtGSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(2), -2,
                          Align(2)),
      STI(STI), TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()) {}

void MtGFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {}

void MtGFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {}
