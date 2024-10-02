//===-- MtGMCTargetDesc.cpp - MtG Target Descriptions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides MtG specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MtGMCTargetDesc.h"
#include "MtGInstPrinter.h"
#include "MtGMCAsmInfo.h"
#include "TargetInfo/MtGTargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "MtGGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "MtGGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MtGGenRegisterInfo.inc"

static MCInstrInfo *createMtGMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitMtGMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createMtGMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitMtGMCRegisterInfo(X, MtG::PC);
  return X;
}

static MCAsmInfo *createMtGMCAsmInfo(const MCRegisterInfo &MRI,
                                        const Triple &TT,
                                        const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new MtGMCAsmInfo(TT);

  // Initialize initial frame state.
  int stackGrowth = -2;

  // Initial state of the frame pointer is sp+ptr_size.
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(MtG::SP, true), -stackGrowth);
  MAI->addInitialFrameState(Inst);

  // Add return address to move list
  MCCFIInstruction Inst2 = MCCFIInstruction::createOffset(
      nullptr, MRI.getDwarfRegNum(MtG::PC, true), stackGrowth);
  MAI->addInitialFrameState(Inst2);

  return MAI;
}

static MCSubtargetInfo *
createMtGMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createMtGMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCInstPrinter *createMtGMCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new MtGInstPrinter(MAI, MII, MRI);
  return nullptr;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMtGTargetMC() {
  Target &T = getTheMtGTarget();

  TargetRegistry::RegisterMCAsmInfo(T, createMtGMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createMtGMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createMtGMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createMtGMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createMtGMCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createMtGMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createMtGMCAsmBackend);
  TargetRegistry::RegisterObjectTargetStreamer(
      T, createMtGObjectTargetStreamer);
}
