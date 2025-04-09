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

def uimm : Operand<i32>,
            ImmLeaf<i32, [{return isInt<32>(Imm) && Imm >= 0;}]> {
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
let Uses = [R9] in 
def MOV_GW1 : CRRForm<0b0101,
                    (outs GR:$rd), (ins),
                    "MOV $rd, r9",
                    []>;
let Uses = [R10] in 
def MOV_GW2 : CRRForm<0b0101,
                    (outs GR:$rd), (ins),
                    "MOV $rd, r10",
                    []>;
let Defs = [R9] in 
def MOV_W1G : CRRForm<0b0101,
                    (outs), (ins GR:$rd),
                    "MOV  r9, $rd",
                    []>;
let Defs = [R10] in 
def MOV_W2G : CRRForm<0b0101,
                    (outs), (ins GR:$rd),
                    "MOV  r10, $rd",
                    []>;
}

let Constraints = " $dst = $src"  in
def ADD_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src,uimm:$imm2),
                    "ADD_PSEUDO\t $dst, $imm2",[]>;

let Constraints = " $dst = $src"  in
def ADD : CRRForm<0b0001,
                    (outs GR:$dst), (ins GR:$src,GR:$src2),
                    "ADD\t $dst, $src2",[(set GR:$dst, (add GR:$src, GR:$src2))]>;

let Constraints = " $dst = $src"  in
def MUL : CRRForm<0b0010,
                    (outs GR:$dst), (ins GR:$src,GR:$src2),
                    "MUL\t $dst, $src2",[(set GR:$dst, (mul GR:$src, GR:$src2))]>;

let Constraints = " $dst = $src", Defs = [R9, R10] in
def DIV_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src,GR:$src2),
                    "DIV_PSEUDO\t $dst, $src2",[(set GR:$dst, (sdiv GR:$src, GR:$src2))]>;

let Constraints = " $dst = $src1", Defs = [R9, R10] in
def SLT_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "SLT_PSEUDO\t $dst, $src2",[(set GR:$dst, (setlt GR:$src1, GR:$src2))]>{
                      let usesCustomInserter =1;
                    }

let Constraints = " $dst = $src1", Defs = [R9, R10] in
def AND_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "AND_PSEUDO\t $dst, $src2",[(set GR:$dst, (and GR:$src1, GR:$src2))]>;

let Constraints = " $dst = $src"  in
def SUB_PSEUDO : Pseudo<
                    (outs GR:$dst), (ins GR:$src,uimm:$imm2),
                    "SUB_PSEUDO\t $dst, $imm2",[]>;
let Constraints = " $dst = $src"  in
def SUB : CRRForm<0b0001,
                    (outs GR:$dst), (ins GR:$src,GR:$src2),
                    "SUB\t $dst, $src2",[]>;

let Constraints = "$dst = $src", Defs = [R0] in
def ADD_OR_SUB_PSEUDO : Pseudo<
                    (outs GR:$dst), (ins GR:$src,uimm:$imm2),
                    "ADD_OR_SUB_PSEUDO\t $dst, $imm2",[]>;


let  Defs = [R0] in 
def NUMBUILD_INIT : CIIForm<0b1010,
                    (outs SR:$rd), (ins simm:$imm),
                    "NUMBUILD\t $rd, $imm",[]>;

let Constraints = " $dst = $src" , Defs = [R0] in
def NUMBUILD_SUCC : CIIForm<0b1010,
                    (outs SR:$dst), (ins SR:$src, simm:$imm),
                    "NUMBUILD\t $dst, $imm",[]>;

let  Defs = [R0], Uses = [R0] in 
def NUMBUILD_PSEUDO : Pseudo<(outs SR:$dst), (ins simm:$imm),
                    "DUMMY",[]>;
let  Defs = [R0], Uses = [R0] in 
def MOV_PSEUDO : Pseudo<(outs GR:$dst), (ins simm:$imm),
                    "MOV_PSEUDO\t$dst,$imm",[]>;

def LOAD : CRRForm<0b1001,
                    (outs GR:$dst), (ins GR:$src),
                    "LOAD\t $dst, $src",[]>;

def STORE : CRRForm<0b1000,
                    (outs), (ins GR:$src0, GR:$src1),
                    "STORE\t $src0, $src1",[]>;

let  Defs = [FLAG] in 
def FLESS : CRRForm<0b1011,
                    (outs), (ins GR:$src0, GR:$src1),
                    "FLESS\t $src0, $src1",[]>;

let  Defs = [FLAG] in
def FISZERO : CRRForm<0b1011,
                    (outs), (ins GR:$src0),
                    "FISZERO\t $src0, $src0",[]>;

let  Defs = [FLAG] in
def HALVE : CCRForm<0b01000010,
                    (outs), (ins GR:$src0),
                    "HALVE\t $src0",[]>;
let  Defs = [FLAG] in
def SETF : CCRForm<0b00100010,
                    (outs), (ins GR:$src0),
                    "SETF\t $src0",[]>;

let  Defs = [FLAG] in
def SETNF : CCRForm<0b00100011,
                    (outs), (ins GR:$src0),
                    "SETF\t $src0",[]>;

let Constraints = "$dwork = $swork", Defs = [R0], Uses = [R0, R11] in 
def CALC_FI_PSEUDO : Pseudo<
                    (outs GR:$dwork), (ins GR:$swork),
                    "CALC_FI",[]>;

def load_a: PatFrag<(ops node:$ptr), (load node:$ptr)>;
def : Pat<(load_a GR:$rs), (LOAD GR:$rs)>;
def store_a: PatFrag<(ops node:$val, node:$ptr), (store node:$val, node:$ptr)>;
def : Pat<(store_a GR:$rs0, GR:$rs1), (STORE GR:$rs0, GR:$rs1)>;

def : Pat<(simm:$imm),(MOV_PSEUDO simm:$imm)>;