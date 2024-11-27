//===-- MtGInstrInfo.td - MtG Instruction defs -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-IdentifiGR: Apache-2.0 WITH LLVM-exception
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

let isMoveReg = true in {
def MOV_GG : CRRForm<0b0101,
                    (outs GR:$rd), (ins GR:$rs),
                    "MOV $rd, $rs",
                    []>;

def MOV_GS : CRRForm<0b0101,
                    (outs GR:$rd), (ins SR:$rs),
                    "MOV $rd, $rs",
                    []>;

def MOV_SG : CRRForm<0b0101,
                    (outs SR:$rd), (ins GR:$rs),
                    "MOV $rd, $rs",
                    []>;
}

def NUMBUILD_INIT : CIIForm<0b1010,
                    (outs SR:$rd), (ins simm:$imm),
                    "NUMBUILD\t $rd, $imm",[]>;

let Constraints = " $dst = $src" in
def NUMBUILD_SUCC : CIIForm<0b1010,
                    (outs SR:$dst), (ins SR:$src, simm:$imm),
                    "NUMBUILD\t $dst, $imm",[]>;
                    
def NUMBUILD_PSEUDO : Pseudo<(outs SR:$dst), (ins simm:$imm),
                    "DUMMY",[]>;

def MOV_PSEUDO : Pseudo<(outs GR:$dst), (ins simm:$imm),
                    "MOV_PSEUDO\t$dst,$imm",[(set GR:$dst, simm:$imm)]>;

def LOAD : CRRForm<0b1001,
                    (outs GR:$dst), (ins GR:$src),
                    "LOAD\t $dst, $src",[]>;

def STORE : CRRForm<0b1000,
                    (outs), (ins GR:$src0, GR:$src1),
                    "STORE\t $src0, $src1",[]>;

def load_a: PatFrag<(ops node:$ptr), (load node:$ptr)>;
def : Pat<(load_a GR:$rs), (LOAD GR:$rs)>;
def store_a: PatFrag<(ops node:$val, node:$ptr), (store node:$val, node:$ptr)>;
def : Pat<(store_a GR:$rs0, GR:$rs1), (STORE GR:$rs0, GR:$rs1)>;


