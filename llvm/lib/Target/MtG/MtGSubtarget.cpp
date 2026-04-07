//===-- MtGSubtarget.cpp - MtG Subtarget Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MtG specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "MtGSubtarget.h"
#include "MtG.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "mtg-subtarget"

// static cl::opt<MtGSubtarget::HWMultEnum>
// HWMultModeOption("mhwmult", cl::Hidden,
//            cl::desc("Hardware multiplier use mode for MtG"),
//            cl::init(MtGSubtarget::NoHWMult),
//            cl::values(
//              clEnumValN(MtGSubtarget::NoHWMult, "none",
//                 "Do not use hardware multiplier"),
//              clEnumValN(MtGSubtarget::HWMult16, "16bit",
//                 "Use 16-bit hardware multiplier"),
//              clEnumValN(MtGSubtarget::HWMult32, "32bit",
//                 "Use 32-bit hardware multiplier"),
//              clEnumValN(MtGSubtarget::HWMultF5, "f5series",
//                 "Use F5 series hardware multiplier")));

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "MtGGenSubtargetInfo.inc"

void MtGSubtarget::anchor() {}

MtGSubtarget &MtGSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                            StringRef FS) {
  ExtendedInsts = false;
  HWMultMode = NoHWMult;

  StringRef CPUName = CPU;
  if (CPUName.empty())
    CPUName = "mtg";

  ParseSubtargetFeatures(CPUName, /*TuneCPU*/ CPUName, FS);

  // if (HWMultModeOption != NoHWMult)
  //   HWMultMode = HWMultModeOption;

  return *this;
}

MtGSubtarget::MtGSubtarget(const Triple &TT, const std::string &CPU,
                           const std::string &FS, const TargetMachine &TM)
    : MtGGenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering(*this) {}
