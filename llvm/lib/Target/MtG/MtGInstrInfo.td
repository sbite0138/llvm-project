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
// Type Constraints.
//===----------------------------------------------------------------------===//
class SDTCisI8<int OpNum> : SDTCisVT<OpNum, i8>;
class SDTCisI16<int OpNum> : SDTCisVT<OpNum, i16>;

//===----------------------------------------------------------------------===//
// Type Profiles.
//===----------------------------------------------------------------------===//
def SDT_MtGCall         : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_MtGCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>,
                                             SDTCisVT<1, i16>]>;
def SDT_MtGCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_MtGWrapper      : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                                  SDTCisPtrTy<0>]>;
def SDT_MtGCmp          : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_MtGBrCC         : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>,
                                                  SDTCisVT<1, i8>]>;
def SDT_MtGSelectCC     : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>,
                                                  SDTCisSameAs<1, 2>, 
                                                  SDTCisVT<3, i8>]>;
def SDT_MtGDAdd         : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>,
                                                  SDTCisSameAs<0, 2>,
                                                  SDTCisInt<0>]>;

//===----------------------------------------------------------------------===//
// MtG Specific Node Definitions.
//===----------------------------------------------------------------------===//
def MtGretglue  : SDNode<"MtGISD::RET_GLUE", SDTNone,
                       [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def MtGretiglue : SDNode<"MtGISD::RETI_GLUE", SDTNone,
                       [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def MtGrra     : SDNode<"MtGISD::RRA", SDTIntUnaryOp, []>;
def MtGrla     : SDNode<"MtGISD::RLA", SDTIntUnaryOp, []>;
def MtGrrc     : SDNode<"MtGISD::RRC", SDTIntUnaryOp, []>;
def MtGrrcl    : SDNode<"MtGISD::RRCL", SDTIntUnaryOp, []>;

def MtGcall    : SDNode<"MtGISD::CALL", SDT_MtGCall,
                     [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue, SDNPVariadic]>;
def MtGcallseq_start :
                 SDNode<"ISD::CALLSEQ_START", SDT_MtGCallSeqStart,
                        [SDNPHasChain, SDNPOutGlue]>;
def MtGcallseq_end :
                 SDNode<"ISD::CALLSEQ_END",   SDT_MtGCallSeqEnd,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def MtGWrapper : SDNode<"MtGISD::Wrapper", SDT_MtGWrapper>;
def MtGcmp     : SDNode<"MtGISD::CMP", SDT_MtGCmp, [SDNPOutGlue]>;
def MtGbrcc    : SDNode<"MtGISD::BR_CC", SDT_MtGBrCC,
                            [SDNPHasChain, SDNPInGlue]>;
def MtGselectcc: SDNode<"MtGISD::SELECT_CC", SDT_MtGSelectCC,
                            [SDNPInGlue]>;
def MtGdadd    : SDNode<"MtGISD::DADD", SDT_MtGDAdd, []>;

//===----------------------------------------------------------------------===//
// MtG Operand Definitions.
//===----------------------------------------------------------------------===//

def MemAsmOperand : AsmOperandClass {
  let Name = "Mem";
}

// Address operands
def memsrc : Operand<i64> {
  let PrintMethod = "printSrcMemOperand";
  let MIOperandInfo = (ops GR, i16imm);
  let ParserMatchClass = MemAsmOperand;
  let EncoderMethod = "getMemOpValue";
  let DecoderMethod = "DecodeMemOperand";
}

def memdst : Operand<i16> {
  let PrintMethod = "printSrcMemOperand";
  let MIOperandInfo = (ops GR, i16imm);
  let ParserMatchClass = MemAsmOperand;
  let EncoderMethod = "getMemOpValue";
  let DecoderMethod = "DecodeMemOperand";
}

def IndRegAsmOperand : AsmOperandClass {
  let Name = "IndReg";
  let RenderMethod = "addRegOperands";
}

def indreg : Operand<i16> {
  let PrintMethod = "printIndRegOperand";
  let MIOperandInfo = (ops GR);
  let ParserMatchClass = IndRegAsmOperand;
  let DecoderMethod = "DecodeGRRegisterClass";
}

def PostIndRegAsmOperand : AsmOperandClass {
  let Name = "PostIndReg";
  let RenderMethod = "addRegOperands";
}

def postreg : Operand<i16> {
  let PrintMethod = "printPostIndRegOperand";
  let MIOperandInfo = (ops GR);
  let ParserMatchClass = PostIndRegAsmOperand;
  let DecoderMethod = "DecodeGRRegisterClass";
}

// Short jump targets have OtherVT type and are printed as pcrel imm values.
def jmptarget : Operand<OtherVT> {
  let PrintMethod = "printPCRelImmOperand";
  let EncoderMethod = "getPCRelImmOpValue";
}


//===----------------------------------------------------------------------===//
//  Control Flow Instructions...
//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
  def RETr  : CCRForm<0b00110111, (outs), (ins), "ret",  [(MtGretglue)]> {
  }
}

//===----------------------------------------------------------------------===//
// Move Instructions

def MOV : CRRForm<0b0101,
                    (outs GR:$rd), (ins GR:$rs),
                    "mov\t{$rs, $rd}",
                    []>;

def NUMBUILD : CIIForm<0b1010,
                    (outs GR:$rd), (ins i64imm:$imm),
                    "mov\t{$imm, $rd}",[]>;

let isCodeGenOnly = 1  , isMoveImm = 1 in {
def SET : Pseudo<(outs GR:$dst), (ins i64imm:$imm),
                    "",
                    [(set GR:$dst, imm:$imm)]>;
}