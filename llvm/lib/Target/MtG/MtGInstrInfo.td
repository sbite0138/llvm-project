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
                    "Mov $rd, $rs",
                    []>;

def MOV_GS : CRRForm<0b0101,
                    (outs GR:$rd), (ins SR:$rs),
                    "Mov $rd, $rs",
                    []>;

def MOV_SG : CRRForm<0b0101,
                    (outs SR:$rd), (ins GR:$rs),
                    "Mov $rd, $rs",
                    []>;
let Uses = [R9] in 
def MOV_GW1 : CRRForm<0b0101,
                    (outs GR:$rd), (ins),
                    "Mov $rd, r9",
                    []>;
let Uses = [R10] in 
def MOV_GW2 : CRRForm<0b0101,
                    (outs GR:$rd), (ins),
                    "Mov $rd, r10",
                    []>;
let Defs = [R9] in 
def MOV_W1G : CRRForm<0b0101,
                    (outs), (ins GR:$rd),
                    "Mov  r9, $rd",
                    []>;
let Defs = [R10] in 
def MOV_W2G : CRRForm<0b0101,
                    (outs), (ins GR:$rd),
                    "Mov  r10, $rd",
                    []>;
}

let Constraints = " $dst = $src"  in
def ADD_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src,uimm:$imm2),
                    "ADD_PSEUDO\t $dst, $imm2",[]>;

let Constraints = " $dst = $src"  in
def ADD : CRRForm<0b0001,
                    (outs GR:$dst), (ins GR:$src,GR:$src2),
                    "Add\t $dst, $src2",[(set GR:$dst, (add GR:$src, GR:$src2))]>;

let Constraints = " $dst = $src"  in
def MUL : CRRForm<0b0010,
                    (outs GR:$dst), (ins GR:$src,GR:$src2),
                    "Mul\t $dst, $src2",[(set GR:$dst, (mul GR:$src, GR:$src2))]>;

let Constraints = " $dst = $src", Defs = [R9, R10] in
def DIV_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src,GR:$src2),
                    "DIV_PSEUDO\t $dst, $src2",[(set GR:$dst, (sdiv GR:$src, GR:$src2))]>;

let  Defs = [FLAG, R9, R10] in
def SLT_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "SLT_PSEUDO\t $dst, $src2",[(set GR:$dst, (setlt GR:$src1, GR:$src2))]>{
                      let usesCustomInserter =1;
                    }

let  Defs = [FLAG, R9, R10] in
def SGT_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "SGT_PSEUDO\t $dst, $src2",[(set GR:$dst, (setgt GR:$src1, GR:$src2))]>{
                      let usesCustomInserter =1;
                    }


let  Defs = [FLAG, R9, R10] in
def SEQ_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "SEQ_PSEUDO\t $dst, $src2",[(set GR:$dst, (seteq GR:$src1, GR:$src2))]>{
                      let usesCustomInserter =1;
                    }

let  Defs = [FLAG, R9, R10] in
def SNEQ_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "SNEQ_PSEUDO\t $dst, $src2",[(set GR:$dst, (setne GR:$src1, GR:$src2))]>{
                      let usesCustomInserter =1;
                    }


let  Defs = [FLAG, R9, R10] in
def SGEQ_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "SNEQ_PSEUDO\t $dst, $src2",[(set GR:$dst, (setge GR:$src1, GR:$src2))]>{
                      let usesCustomInserter =1;
                    }

let  Defs = [FLAG, R9, R10] in
def SLEQ_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1,GR:$src2),
                    "SNEQ_PSEUDO\t $dst, $src2",[(set GR:$dst, (setle GR:$src1, GR:$src2))]>{
                      let usesCustomInserter =1;
                    }


let Constraints  = "$dst = $src1", Defs = [R9, R10] in
def LSB_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                    "LSB_PSEUDO\t $dst",[(set GR:$dst, (and GR:$src1, 1))]>{
                      let usesCustomInserter =1;
                    }

let Constraints = " $dst = $src"  in
def SUB_PSEUDO : Pseudo<
                    (outs GR:$dst), (ins GR:$src,uimm:$imm2),
                    "SUB_PSEUDO\t $dst, $imm2",[]>;
let Constraints = " $dst = $src"  in
def SUB : CRRForm<0b0001,
                    (outs GR:$dst), (ins GR:$src,GR:$src2),
                    "Sub\t $dst, $src2",[]>;

let Constraints = "$dst = $src", Defs = [R0] in
def ADD_OR_SUB_PSEUDO : Pseudo<
                    (outs GR:$dst), (ins GR:$src,uimm:$imm2),
                    "ADD_OR_SUB_PSEUDO\t $dst, $imm2",[]>;


let  Defs = [R0] in 
def NUMBUILD_INIT : CIIForm<0b1010,
                    (outs SR:$rd), (ins simm:$imm),
                    "NumBuild\t $rd, $imm",[]>;

let Constraints = " $dst = $src" , Defs = [R0] in
def NUMBUILD_SUCC : CIIForm<0b1010,
                    (outs SR:$dst), (ins SR:$src, simm:$imm),
                    "NumBuild\t $dst, $imm",[]>;

let  Defs = [R0], Uses = [R0] in 
def NUMBUILD_PSEUDO : Pseudo<(outs SR:$dst), (ins simm:$imm),
                    "DUMMY",[]>;
let  Defs = [R0], Uses = [R0] in 
def MOV_PSEUDO : Pseudo<(outs GR:$dst), (ins simm:$imm),
                    "MOV_PSEUDO\t$dst,$imm",[]>;

def LOAD : CRRForm<0b1001,
                    (outs GR:$dst), (ins GR:$src),
                    "Load\t $dst, $src",[]>;

def STORE : CRRForm<0b1000,
                    (outs), (ins GR:$src0, GR:$src1),
                    "Store\t $src0, $src1",[]>;

let  Defs = [FLAG] in 
def FLESS : CRRForm<0b1011,
                    (outs), (ins GR:$src0, GR:$src1),
                    "FLess\t $src0, $src1",[]>;

let  Defs = [FLAG] in
def FISZERO : CRRForm<0b1011,
                    (outs), (ins GR:$src0),
                    "FIsZero\t $src0, $src0",[]>;

let Constraints = "$dst = $src", Defs = [FLAG] in
def HALVE : CCRForm<0b01000010,
                    (outs GR:$dst), (ins GR:$src),
                    "Halve\t $src",[]>;

let Uses = [FLAG] in
def SETF : CCRForm<0b00100010,
                    (outs), (ins GR:$src0),
                    "SetF\t $src0",[]>;

let  Uses = [FLAG] in
def SETNF : CCRForm<0b00100011,
                    (outs), (ins GR:$src0),
                    "SetNF\t $src0",[]>;


def ZERO : CCRForm<0b00000101,
                    (outs GR:$dst), (ins),
                    "Zero\t $dst",[]>;

let Uses = [FLAG]  in     
def JUMPBWDF_IMM : CCIForm<0b00111001,
                    (outs), (ins simm:$imm),
                    "JumpBwdF\t $imm",[]>;


let Constraints = "$dst = $src" ,  Uses = [FLAG] in
def SUB1COND : CCRForm<0b00100011,
                    (outs GR:$dst), (ins GR:$src),
                    "Sub1Cond\t $src",[]>;



let Constraints = "$dwork = $swork", Defs = [R0], Uses = [R0, R11] in 
def CALC_FI_PSEUDO : Pseudo<
                    (outs GR:$dwork), (ins GR:$swork),
                    "CALC_FI",[]>;

def load_a: PatFrag<(ops node:$ptr), (load node:$ptr)>;
def : Pat<(load_a GR:$rs), (LOAD GR:$rs)>;
def store_a: PatFrag<(ops node:$val, node:$ptr), (store node:$val, node:$ptr)>;
def : Pat<(store_a GR:$rs0, GR:$rs1), (STORE GR:$rs0, GR:$rs1)>;

def : Pat<(simm:$imm),(MOV_PSEUDO simm:$imm)>;


def brtarget : Operand<OtherVT>{
  let EncoderMethod = "getPCRelImmOpValue";
  
  let PrintMethod = "printPCRelImmOperand";
}

let isBarrier = 1, isBranch = 1, isTerminator = 1  in 
def BR_PSEUDO : Pseudo<(outs), (ins brtarget:$imm),
                    "BR_PSEUDO $imm",[(br bb:$imm)]> {
                        let usesCustomInserter = 1;
                    }

  

let isBarrier = 1, isBranch = 1, isTerminator = 1  in 
def BRCOND_PSEUDO : Pseudo<(outs), (ins GR:$cond, brtarget:$imm),
                    "BRCOND_PSEUDO $cond, $imm",[(brcond GR:$cond, bb:$imm)]> {
                    }



def SELECT_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$cond, GR:$tval, GR:$fval),
                    "SELECT_PSEUDO $cond, $imm",[(set GR:$dst, (select GR:$cond, GR:$tval, GR:$fval))]> {
                let AsmString = "SELECT_PSEUDO $dst, $cond, $tval, $fval";
      }


