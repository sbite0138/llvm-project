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

def SDT_MtGCall: SDTypeProfile<0,1,[SDTCisVT<0,iPTR>]>;

def MtGCall: SDNode<"MtGISD::CALL",SDT_MtGCall, [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;

def SDT_MtGCallSeqStart : SDCallSeqStart <[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_MtGCallSeqEnd : SDCallSeqEnd <[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

def MtGCallSeqStart : SDNode<"ISD::CALLSEQ_START", SDT_MtGCallSeqStart, [SDNPHasChain, SDNPOutGlue]>;
def MtGCallSeqEnd : SDNode<"ISD::CALLSEQ_END", SDT_MtGCallSeqEnd, [SDNPHasChain, SDNPOutGlue]>;


//===----------------------------------------------------------------------===//
// MtG Operand Definitions.
//===----------------------------------------------------------------------===//


def simm : Operand<i32>,
            ImmLeaf<i32, [{return isInt<32>(Imm);}]> {
  let DecoderMethod = "decodeImmOperand<32>";
}

def uimm : Operand<i32>,
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


let Defs = [R8, R0], Uses = [R8], hasSideEffects = 1, isCodeGenOnly = 1,  isPseudo = 1 in {

  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins simm:$amt1, simm:$amt2), "adjstackdown $amt1, $amt2",
                                         [(MtGCallSeqStart timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP   : Pseudo<(outs), (ins simm:$amt1, simm:$amt2), "adjstackup $amt1, $amt2",
                                         [(MtGCallSeqEnd timm:$amt1, timm:$amt2)]>;
}


//===----------------------------------------------------------------------===//
// Move Instructions


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

let Constraints = " $dst = $src", Defs = [R0, R6], Uses = [R6] in
def DIV_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src,GR:$src2),
                    "DIV_PSEUDO\t $dst, $src2",[(set GR:$dst, (sdiv GR:$src, GR:$src2))]>;

let Constraints = " $dst = $src", Defs = [R0, R6], Uses = [R6] in
def REM_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src,GR:$src2),
                    "REM_PSEUDO\t $dst, $src2",[(set GR:$dst, (srem GR:$src, GR:$src2))]>;

let Constraints = " $dst = $src", Defs = [FLAG, R0] in
def SHL_PSEUDO : Pseudo<(outs GR:$dst), (ins GR:$src, simm:$imm),
                    "SHL_PSEUDO\t $dst, $imm",[(set GR:$dst, (shl GR:$src, simm:$imm))]>;

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
                    "Sub\t $dst, $src2",[(set GR:$dst, (sub GR:$src, GR:$src2))]>;

let Constraints = "$dst = $src", Defs = [R0] in
def ADD_OR_SUB_PSEUDO : Pseudo<
                    (outs GR:$dst), (ins GR:$src,uimm:$imm2),
                    "ADD_OR_SUB_PSEUDO\t $dst, $imm2",[]>;


// let  Defs = [R0] in 
// def NUMBUILD_INIT : CIIForm<0b1010,
//                     (outs SR:$rd), (ins simm:$imm),
//                     "NumBuild\t $rd, $imm",[]>;

// let Constraints = " $dst = $src" , Defs = [R0] in
// def NUMBUILD_SUCC : CIIForm<0b1010,
//                     (outs SR:$dst), (ins SR:$src, simm:$imm),
//                     "NumBuild\t $dst, $imm",[]>;

// let  Defs = [R0], Uses = [R0] in 
// def NUMBUILD_PSEUDO : Pseudo<(outs SR:$dst), (ins simm:$imm),
//                     "DUMMY",[]>;
// let  Defs = [R0], Uses = [R0] in 
// def MOV_PSEUDO : Pseudo<(outs GR:$dst), (ins simm:$imm),
//                     "MOV_PSEUDO\t$dst,$imm",[]>;

// def LOAD : CRRForm<0b1001,
//                     (outs GR:$dst), (ins GR:$src),
//                     "Load\t $dst, $src",[]>;

// def STORE : CRRForm<0b1000,
//                     (outs), (ins GR:$src0, GR:$src1),
//                     "Store\t $src0, $src1",[]>;

// let  Defs = [FLAG] in 
// def FLESS : CRRForm<0b1011,
//                     (outs), (ins GR:$src0, GR:$src1),
//                     "FLess\t $src0, $src1",[]>;

// let  Defs = [FLAG] in
// def FISZERO : CRRForm<0b1011,
//                     (outs), (ins GR:$src0),
//                     "FIsZero\t $src0, $src0",[]>;

// let Constraints = "$dst = $src", Defs = [FLAG] in
// def HALVE : CCRForm<0b01000010,
//                     (outs GR:$dst), (ins GR:$src),
//                     "Halve\t $src",[]>;

// let Uses = [FLAG] in
// def SETF : CCRForm<0b00100010,
//                     (outs), (ins GR:$src0),
//                     "SetF\t $src0",[]>;

// let  Uses = [FLAG] in
// def SETNF : CCRForm<0b00100011,
//                     (outs), (ins GR:$src0),
//                     "SetNF\t $src0",[]>;


// def ZERO : CCRForm<0b00000101,
//                     (outs GR:$dst), (ins),
//                     "Zero\t $dst",[]>;

// let Uses = [FLAG]  in     
// def JUMPBWDF_IMM : CCIForm<0b00111001,
//                     (outs), (ins simm:$imm),
//                     "JumpBwdF\t $imm",[]>;


// let Constraints = "$dst = $src" ,  Uses = [FLAG] in
// def SUB1COND : CCRForm<0b00100011,
//                     (outs GR:$dst), (ins GR:$src),
//                     "Sub1Cond\t $src",[]>;





def brtarget : Operand<OtherVT>{
  let EncoderMethod = "getPCRelImmOpValue";
  
  let PrintMethod = "printPCRelImmOperand";
}

let isBarrier = 1, isBranch = 1, isTerminator = 1, Defs = [R0]  in 
def BR_PSEUDO : Pseudo<(outs), (ins brtarget:$imm),
                    "BR_PSEUDO $imm",[(br bb:$imm)]> {
                        let usesCustomInserter = 1;
                    }

  

let isBarrier = 1, isBranch = 1, isTerminator = 1, Defs = [R0]  in 
def BRCOND_PSEUDO : Pseudo<(outs), (ins GR:$cond, brtarget:$imm),
                    "BRCOND_PSEUDO $cond, $imm",[(brcond GR:$cond, bb:$imm)]> {
                    }





// let  Defs = [R0] in 
// def LOADADDR_PSEUDO : Pseudo<(outs SR:$dst), (ins tglobaladdr:$src),
//                     "LOADADDR_PSEUDO $dst, $src",[]>;


let isCall = 1, Defs = [R0], isCodeGenOnly = 0 in {
  def CALL_PSEUDO : Pseudo<(outs), (ins simm:$src),
                    "Call $src",[]>;
}


def : Pat<(MtGCall (i32 tglobaladdr:$dst)),
          (CALL_PSEUDO tglobaladdr:$dst)>;


let usesCustomInserter = 1,  Defs = [R0, R1, R2, R3, R4, R5, R6, R7] in {
def LOADBYTEWISE_MACRO : Pseudo<(outs GR:$val), (ins  GR:$addr),
                    "LOADBYTEWISE_MACRO\t $val, $addr",[]>;
def STOREBYTEWISE_MACRO : Pseudo<(outs), (ins GR:$val, GR:$addr),
                    "STOREBYTEWISE_MACRO\t $val, $addr",[]>;

def LOADBYTEWISE_IMM_MACRO : Pseudo<(outs GR:$val), (ins simm:$imm),
                    "LOADBYTEWISE_IMM_MACRO\t $val, $imm",[]>;
def STOREBYTEWISE_IMM_MACRO : Pseudo<(outs), (ins GR:$val, simm:$imm),
                    "STOREBYTEWISE_IMM_MACRO\t $val, $imm",[]>;

let Constraints = " $dst = $src1" in {
    def ADD_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "ADD_MACRO\t $dst, $src2",[(set GR:$dst, (add GR:$src1, GR:$src2))]>;
    def SUB_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "SUB_MACRO\t $dst, $src2",[(set GR:$dst, (sub GR:$src1, GR:$src2))]>;
    def MULHI_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "MULHI_MACRO\t $dst, $src2",[(set GR:$dst, (mulhs GR:$src1, GR:$src2))]>;
    def MULLO_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "MULLO_MACRO\t $dst, $src2",[(set GR:$dst, (mul GR:$src1, GR:$src2))]>;
    def DIV_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "DIV_MACRO\t $dst, $src2",[(set GR:$dst, (sdiv GR:$src1, GR:$src2))]>;
    def REM_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "REM_MACRO\t $dst, $src2",[(set GR:$dst, (srem GR:$src1, GR:$src2))]>;
    def AND_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "AND_MACRO\t $dst, $src2",[(set GR:$dst, (and GR:$src1, GR:$src2))]>;
    def OR_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "OR_MACRO\t $dst, $src2",[(set GR:$dst, (or GR:$src1, GR:$src2))]>;
    def XOR_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "XOR_MACRO\t $dst, $src2",[(set GR:$dst, (xor GR:$src1, GR:$src2))]>;
    def SHL_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, simm:$imm),
                    "SHL_MACRO\t $dst, $imm",[(set GR:$dst, (shl GR:$src1, simm:$imm))]>;
    def SHR_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, simm:$imm),
                    "SHR_MACRO\t $dst, $imm",[(set GR:$dst, (srl GR:$src1, simm:$imm))]>;
    def ASHR_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, simm:$imm),
                    "ASHR_MACRO\t $dst, $imm",[(set GR:$dst, (sra GR:$src1, simm:$imm))]>;
    def NOT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                    "NOT_MACRO\t $dst",[(set GR:$dst, (xor GR:$src1, -1))]>;
    def NEG_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                    "NEG_MACRO\t $dst",[(set GR:$dst, (sub 0, GR:$src1))]>;
    def MOVREG_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                    "MOVREG_MACRO\t $dst, $src1",[(set GR:$dst, GR:$src1)]>;
  }
  def MOVIMM_MACRO : Pseudo<(outs GR:$dst), (ins simm:$imm),
                    "MOVIMM_MACRO\t $dst, $imm",[(set GR:$dst, simm:$imm)]>;
  def SELECT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$cond, GR:$tval, GR:$fval),
                    "SELECT_MACRO\t $dst, $cond, $tval, $fval",[(set GR:$dst, (select GR:$cond, GR:$tval, GR:$fval))]>;
let Constraints = "$dwork = $swork", Defs = [R0], Uses = [R0, R11] in 
def CALC_FI_PSEUDO : Pseudo<
                    (outs GR:$dwork), (ins GR:$swork),
                    "CALC_FI",[]>;

}

def load_a: PatFrag<(ops node:$ptr), (load node:$ptr)>;
def : Pat<(load_a GR:$rs), (LOADBYTEWISE_MACRO GR:$rs)>;

def store_a: PatFrag<(ops node:$val, node:$ptr), (store node:$val, node:$ptr)>;
def : Pat<(store_a GR:$rs0, GR:$rs1), (STOREBYTEWISE_MACRO GR:$rs0, GR:$rs1)>;
