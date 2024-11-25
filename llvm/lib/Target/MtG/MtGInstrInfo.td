//===-- MtGInstrInfo.td - MtG Instruction defs -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the MtG instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "MtGInstrFormats.td"


//===----------------------------------------------------------------------===//
// MtG Specific Node Definitions.
//===----------------------------------------------------------------------===//
def MtGretglue  : SDNode<"MtGISD::RET_GLUE", SDTNone,
                       [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def MtGImm  : SDNode<"MtGISD::IMM", SDTNone,
                       []>;


//===----------------------------------------------------------------------===//
// MtG Operand Definitions.
//===----------------------------------------------------------------------===//


def simm : Operand<i32>,
            ImmLeaf<i32, [{return isInt<32>(Imm);}]> {
  let DecoderMethod = "decodeImmOperand<32>";
}


//===----------------------------------------------------------------------===//
//  Control Flow Instructions...
//

let isReturn = 1, isTerminator = 1, hasDelaySlot=0, isBarrier=1, hasCtrlDep=1 in {
  def RET  : Pseudo<(outs), (ins), "ret",  [(MtGretglue)]> {
  }
}

//===----------------------------------------------------------------------===//
// Move Instructions

def MOV : CRRForm<0b0101,
                    (outs GR:$rd), (ins GR:$rs),
                    "MOV $rd, $rs",
                    []>;

def NUMBUILD_INIT : CIIForm<0b1010,
                    (outs SR:$rd), (ins simm:$imm),
                    "NUMBUILD\t $imm",[]>;

let Constraints = " $dst = $src" in
def NUMBUILD_SUCC : CIIForm<0b1010,
                    (outs SR:$dst), (ins SR:$src, simm:$imm),
                    "NUMBUILD\t $imm",[]>;
                    
def NUMBUILD_PSEUDO : Pseudo<(outs SR:$dst), (ins simm:$imm),
                    "DUMMY",[]>;
def : Pat<(simm:$in), (NUMBUILD_PSEUDO simm:$in)>;

