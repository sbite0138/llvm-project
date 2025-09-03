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

def SDT_MtGiPTRUnary : SDTypeProfile<1, 1,
                   [SDTCisPtrTy<0>,  
                    SDTCisPtrTy<1>]>;

def WRAP_ADDR : SDNode<"MtGISD::WRAP_ADDR", SDT_MtGiPTRUnary,
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


def mtgimm : Operand<i32>,
            ImmLeaf<i32, [{return isInt<32>(Imm);}]> {
  let DecoderMethod = "decodeImmOperand<32>";
}

def calltarget : Operand<iPTR> {
  let EncoderMethod   = "getCallTargetOpValue";
  let OperandType     = "OPERAND_PCREL";
  let PrintMethod     = "printCallTargetOperand";       
}


def brtarget : Operand<OtherVT>{
  let EncoderMethod = "getPCRelImmOpValue";
  let PrintMethod = "printPCRelImmOperand";
}


//===----------------------------------------------------------------------===//
//  Control Flow Instructions...
//

let isReturn = 1, isTerminator = 1, hasDelaySlot=0, isBarrier=1, hasCtrlDep=1 in {
  def RET  : Pseudo<(outs), (ins), "ret",  [(MtGretglue)]> {
  }
}


let Defs = [R8, R0], Uses = [R8], hasSideEffects = 1, isCodeGenOnly = 1,  isPseudo = 1 in {

  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins mtgimm:$amt1, mtgimm:$amt2), "adjstackdown $amt1, $amt2",
                                         [(MtGCallSeqStart timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP   : Pseudo<(outs), (ins mtgimm:$amt1, mtgimm:$amt2), "adjstackup $amt1, $amt2",
                                         [(MtGCallSeqEnd timm:$amt1, timm:$amt2)]>;
}


//===----------------------------------------------------------------------===//
// Move Instructions



// for pass building, we need a some non-pseudo instruction
def NOP : CCCForm<0b000000000000,
                    (outs GR:$dst), (ins),
                    "NOP $dst",[]>{
                      let isPseudo=0;
                      let isCodeGenOnly = 0;
                      let isAsmParserOnly = 0;
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



let isCall = 1, Defs = [R0], isCodeGenOnly = 0 in {
  def CALL_PSEUDO : Pseudo<(outs), (ins calltarget:$src),
                    "Call $src",[]>;

}

 
def : Pat<(MtGCall (i32 tglobaladdr:$dst)),
          (CALL_PSEUDO tglobaladdr:$dst)>;


def MOVE : CRRForm<0b1001, (outs GR:$dst), (ins  GR:$src),
                    "MOVE\t $dst, $src",[]>;  
let usesCustomInserter = 1, Defs = [R0] in 
def NUMBUILD_MACRO : Pseudo<(outs ), (ins  mtgimm:$imm),
                    "NUMBUILD_MACRO\t $imm",[]>;

let usesCustomInserter = 1,  Defs = [R0, R1, R2, R3, R4, R5, R6, R7]in {
def LOADBYTEWISE_MACRO : Pseudo<(outs GR:$val), (ins  GR:$addr),
                    "LOADBYTEWISE_MACRO\t $val, $addr",[(set GR:$val, (load GR:$addr))]>;
def STOREBYTEWISE_MACRO : Pseudo<(outs), (ins GR:$val, GR:$addr),
                    "STOREBYTEWISE_MACRO\t $val, $addr",[(store GR:$val, GR:$addr)]>;

def LOADBYTEWISE_IMM_MACRO : Pseudo<(outs GR:$val), (ins mtgimm:$imm),
                    "LOADBYTEWISE_IMM_MACRO\t $val, $imm",[]>; 
def STOREBYTEWISE_IMM_MACRO : Pseudo<(outs), (ins GR:$val, mtgimm:$imm),
                    "STOREBYTEWISE_IMM_MACRO\t $val, $imm",[]>;

let Constraints = "$src1 = $dst" in {
    def ADD_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "ADD_MACRO\t $dst, $src2",[(set GR:$dst, (add GR:$src1, GR:$src2))]>;
    def ADD_IMM_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, mtgimm:$src2),
                    "ADD_IMM_MACRO\t $dst, $src2",[(set GR:$dst, (add GR:$src1, mtgimm:$src2))]>;
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
    def SHL_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, mtgimm:$imm),
                    "SHL_MACRO\t $dst, $imm",[(set GR:$dst, (shl GR:$src1, mtgimm:$imm))]>;
    def SHR_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, mtgimm:$imm),
                    "SHR_MACRO\t $dst, $imm",[(set GR:$dst, (srl GR:$src1, mtgimm:$imm))]>;
    def ASHR_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, mtgimm:$imm),
                    "ASHR_MACRO\t $dst, $imm",[(set GR:$dst, (sra GR:$src1, mtgimm:$imm))]>;
    def NOT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                    "NOT_MACRO\t $dst",[(set GR:$dst, (xor GR:$src1, -1))]>;
    def NEG_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                    "NEG_MACRO\t $dst",[(set GR:$dst, (sub 0, GR:$src1))]>;
    def MOVEREG_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                    "MOVREG_MACRO\t $dst, $src1",[]>;
  } 
   def EQ_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "EQ_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (seteq GR:$src1, GR:$src2))]>;
   def NEQ_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "NEQ_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (setne GR:$src1, GR:$src2))]>;
   def LT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "LT_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (setlt GR:$src1, GR:$src2))]>;
   def GT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "GT_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (setgt GR:$src1, GR:$src2))]>;
  def MOVEIMM_MACRO : Pseudo<(outs GR:$dst), (ins mtgimm:$imm),
                    "MOVIMM_MACRO\t $dst, $imm",[(set GR:$dst, mtgimm:$imm)]>;
  def SELECT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$cond, GR:$tval, GR:$fval),
                    "SELECT_MACRO\t $dst, $cond, $tval, $fval",[(set GR:$dst, (select GR:$cond, GR:$tval, GR:$fval))]>;
}


def tFI : Operand<i32>;

def ADD_MACRO_FI : Pseudo<
  (outs GR:$dst), (ins tFI:$fi, mtgimm:$ofs),
  "ADD_MACRO_FI\t$dst, [$fi+$ofs]",[]
>;