//===-- MtG.td - Describe the MtG Target Machine -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This is the top level entry point for the MtG target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Subtarget Features. 
//===----------------------------------------------------------------------===//
def FeatureX
 : SubtargetFeature<"ext", "ExtendedInsts", "true",
                    "Enable MtG-X extensions">;

def FeatureHWMult16
 : SubtargetFeature<"hwmult16", "HWMultMode", "HWMult16",
                    "Enable 16-bit hardware multiplier">;

def FeatureHWMult32
 : SubtargetFeature<"hwmult32", "HWMultMode", "HWMult32",
                    "Enable 32-bit hardware multiplier">;

def FeatureHWMultF5
 : SubtargetFeature<"hwmultf5", "HWMultMode", "HWMultF5",
                    "Enable F5 series hardware multiplier">;

//===----------------------------------------------------------------------===//
// MtG supported processors.
//===----------------------------------------------------------------------===//
class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def : Proc<"generic",         []>;
def : Proc<"mtg",          []>;
def : Proc<"mtgx",         [FeatureX]>;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "MtGRegisterInfo.td"

//===----------------------------------------------------------------------===//
// Calling Convention Description
//===----------------------------------------------------------------------===//

include "MtGCallingConv.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "MtGInstrInfo.td"

def MtGInstrInfo : InstrInfo;

//===---------------------------------------------------------------------===//
// Assembly Printers
//===---------------------------------------------------------------------===//

def MtGAsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
}

//===---------------------------------------------------------------------===//
// Assembly Parsers
//===---------------------------------------------------------------------===//

def MtGAsmParser : AsmParser {
  let AllowDuplicateRegisterNames = 1;
  let ShouldEmitMatchRegisterAltName = 1;
}

//===----------------------------------------------------------------------===//
// Target Declaration
//===----------------------------------------------------------------------===//

def MtG : Target {
  let InstructionSet = MtGInstrInfo;
  let AssemblyParsers = [MtGAsmParser];
}

