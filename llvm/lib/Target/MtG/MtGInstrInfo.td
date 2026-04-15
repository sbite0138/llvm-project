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

// I/O builtins used by __mtg_output and __mtg_input_{a,b}.
def SDT_MtGOutput : SDTypeProfile<0, 1, [SDTCisVT<0, i32>]>;
def MtGISD_OUTPUT : SDNode<"MtGISD::OUTPUT", SDT_MtGOutput,
                           [SDNPHasChain, SDNPSideEffect]>;

def SDT_MtGInput : SDTypeProfile<1, 0, [SDTCisVT<0, i32>]>;
def MtGISD_INPUT_A : SDNode<"MtGISD::INPUT_A", SDT_MtGInput,
                            [SDNPHasChain, SDNPSideEffect]>;
def MtGISD_INPUT_B : SDNode<"MtGISD::INPUT_B", SDT_MtGInput,
                            [SDNPHasChain, SDNPSideEffect]>;


//===----------------------------------------------------------------------===//
// MtG Operand Definitions.
//===----------------------------------------------------------------------===//


def mtgimm : Operand<i32>,
            ImmLeaf<i32, [{return isInt<32>(Imm);}]> {
  let DecoderMethod = "decodeImmOperand<32>";
}

// One base-12 card digit (0..11). Used for NumBuild's two immediate fields
// and for any other slot that corresponds directly to a Y or Z card in the
// encoding. Keeping the range tight lets the binary encoder reject
// out-of-range values via assertion and documents the ISA shape.
def cardimm : Operand<i32>,
              ImmLeaf<i32, [{return Imm >= 0 && Imm <= 11;}]>;

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
  def RET_PSEUDO  : Pseudo<(outs), (ins), "RET_PSEUDO",  [(MtGretglue)]> {
  }
}


let Defs = [R2, R0], Uses = [R2], hasSideEffects = 1, isCodeGenOnly = 1,  isPseudo = 1 in {

  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins mtgimm:$amt1, mtgimm:$amt2), "adjstackdown $amt1, $amt2",
                                         [(MtGCallSeqStart timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP   : Pseudo<(outs), (ins mtgimm:$amt1, mtgimm:$amt2), "adjstackup $amt1, $amt2",
                                         [(MtGCallSeqEnd timm:$amt1, timm:$amt2)]>;
}


//===----------------------------------------------------------------------===//
// Move Instructions



let isBarrier = 1, isBranch = 1, isTerminator = 1, Defs = [R0] in
def BR_PSEUDO : Pseudo<(outs), (ins brtarget:$imm),
                    "BR_PSEUDO $imm",[(br bb:$imm)]>;

// $polarity: 0 = branch if $cond != 0, 1 = branch if $cond == 0.
// Expanded in expandPostRAPseudo to FISZERO + NUMBUILD placeholders +
// JUMPFWDNF/JUMPFWDF/JUMPBWDNF/JUMPBWDF depending on polarity and direction.
let isBarrier = 1, isBranch = 1, isTerminator = 1, Defs = [R0, FLAG] in
def BRCOND_PSEUDO : Pseudo<(outs), (ins GR:$cond, i32imm:$polarity, brtarget:$imm),
                    "BRCOND_PSEUDO $cond, $polarity, $imm",[]>;

def : Pat<(brcond GR:$cond, bb:$imm),
          (BRCOND_PSEUDO GR:$cond, 0, bb:$imm)>;

// Call placeholder emitted by ISel. Printed as "CallFwd $src" unconditionally;
// the ursa assembler flips the mnemonic to "CallBwdR" when the target lies
// before the call site and patches the two leading NumBuild placeholders that
// MtGExpandBranchPseudo inserts with the resolved instruction distance. This
// mirrors how ursa already resolves Jumps, and how standard targets (RISC-V,
// MIPS, etc.) defer PC-relative resolution to the assembler / linker rather
// than fixing it up inside the compiler backend.
//
// Under the stack-only ABI every allocatable register is caller-saved, so a
// call clobbers R0 (macro scratch used for the call-distance NumBuild), FLAG,
// and every general-purpose register. R2 (SP) and R6 (Divide quotient) are
// reserved and therefore not listed — they are preserved by not being
// touched, not by callee save/restore.
let isCall = 1,
    Defs = [R0, R1, R3, R4, R5, R7, R8, R9, R10, R11, FLAG],
    Uses = [R0, R2],
    isCodeGenOnly = 0 in {
  def CALL_PSEUDO : Pseudo<(outs), (ins calltarget:$src),
                    "CallFwd $src",[]>;
}

def : Pat<(MtGCall (i32 tglobaladdr:$dst)),
          (CALL_PSEUDO tglobaladdr:$dst)>;



let usesCustomInserter = 1, Defs = [R0] in 
def NUMBUILD_MACRO : Pseudo<(outs ), (ins  mtgimm:$imm),
                    "NUMBUILD_MACRO\t $imm",[]>;
def tFI : Operand<i32>;

let usesCustomInserter = 1, Defs = [R0, R1, R2, R3, R4, R5, R6, R7] in {

let hasSideEffects = 0, mayStore = 0, mayLoad = 1, isPseudo = 1 in 
def LOADBYTEWISE_MACRO : Pseudo<(outs GR:$val), (ins  GR:$addr),
                    "LOADBYTEWISE_MACRO\t $val, $addr",[(set GR:$val, (load GR:$addr))]>;

let hasSideEffects = 0, mayStore = 1, mayLoad = 0, isPseudo = 1 in
def STOREBYTEWISE_MACRO : Pseudo<(outs), (ins GR:$val, GR:$addr),
                    "STOREBYTEWISE_MACRO\t $val, $addr",[(store GR:$val, GR:$addr)]>;

def LOADBYTEWISE_IMM_MACRO : Pseudo<(outs GR:$val), (ins mtgimm:$imm),
                    "LOADBYTEWISE_IMM_MACRO\t $val, $imm",[]>; 
def STOREBYTEWISE_IMM_MACRO : Pseudo<(outs), (ins GR:$val, mtgimm:$imm),
                    "STOREBYTEWISE_IMM_MACRO\t $val, $imm",[]>;
def MOVEREG_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1),
                "MOVREG_MACRO\t $dst, $src1",[]>;
def MOVEIMM_MACRO : Pseudo<(outs GR:$dst), (ins mtgimm:$imm),
                  "MOVIMM_MACRO\t $dst, $imm",[(set GR:$dst, mtgimm:$imm)]>;
let Constraints = "$src1 = $dst" in {
    def ADD_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "ADD_MACRO\t $dst, $src2",[(set GR:$dst, (add GR:$src1, GR:$src2))]>;
    def ADD_IMM_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, mtgimm:$src2),
                    "ADD_IMM_MACRO\t $dst, $src2",[(set GR:$dst, (add GR:$src1, mtgimm:$src2))]>;
    def SUB_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "SUB_MACRO\t $dst, $src2",[(set GR:$dst, (sub GR:$src1, GR:$src2))]>;
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
  } 
   def EQ_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "EQ_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (seteq GR:$src1, GR:$src2))]>;
   def NEQ_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "NEQ_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (setne GR:$src1, GR:$src2))]>;
   def LT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "LT_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (setlt GR:$src1, GR:$src2))]>;
   def GT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$src1, GR:$src2),
                    "GT_MACRO\t $dst, $src1, $src2",[(set GR:$dst, (setgt GR:$src1, GR:$src2))]>;

  def SELECT_MACRO : Pseudo<(outs GR:$dst), (ins GR:$cond, GR:$tval, GR:$fval),
                    "SELECT_MACRO\t $dst, $cond, $tval, $fval",[(set GR:$dst, (select GR:$cond, GR:$tval, GR:$fval))]>;
}
def : Pat<(i32  (setcc GR:$src1, GR:$src2, SETUGT)),
          (GT_MACRO GR:$src1, GR:$src2)>;     

def : Pat<(i32  (setcc GR:$src1, GR:$src2, SETULT)),
          (LT_MACRO GR:$src1, GR:$src2)>;     

def : Pat<(i32  (urem GR:$src1, GR:$src2)),
          (REM_MACRO GR:$src1, GR:$src2)>;     

def : Pat<(i32  (udiv GR:$src1, GR:$src2)),
          (DIV_MACRO GR:$src1, GR:$src2)>;     


let hasSideEffects = 0, mayStore = 0, mayLoad = 1, isPseudo = 1 in
def LOADBYTEWISE_FI_MACRO : Pseudo<(outs GR:$val), (ins  tFI:$fi),
                    "",[]>;

let hasSideEffects = 0, mayStore = 1, mayLoad = 0, isPseudo = 1 in
def STOREBYTEWISE_FI_MACRO : Pseudo<(outs), (ins GR:$val, tFI:$fi),
                    "",[]>;

def ADD_MACRO_FI : Pseudo<
  (outs GR:$dst), (ins tFI:$fi, mtgimm:$ofs),
  "ADD_MACRO_FI\t$dst, [$fi+$ofs]",[]
>;



// =========================================================================
// Real (non-pseudo) instructions, grouped by X field per the MtG spec.
// =========================================================================

// --- X=0 : Add (rY += rZ) ------------------------------------------------
let Constraints = "$rY = $rY_in" in
def ADD : MtGFormCRR<0,
                     (outs GR:$rY), (ins GR:$rY_in, GR:$rZ),
                     "Add\t $rY, $rZ", []>;

// --- X=1 : Sub family ----------------------------------------------------
let Constraints = "$rY = $rY_in", Defs = [FLAG] in
def SUBCOND : MtGFormCRR<1,
                         (outs GR:$rY), (ins GR:$rY_in, GR:$rZ),
                         "SubCond\t $rY, $rZ", []>;

let Constraints = "$r = $r_in", Defs = [FLAG] in
def SUB1COND : MtGFormCSR<1,
                          (outs GR:$r), (ins GR:$r_in),
                          "Sub1Cond\t $r", []>;

// --- X=2 : I/O, flag reads, (Rand6/Rand20 not wired yet) -----------------
let hasSideEffects = 1, mayLoad = 1 in {
def INPUTA : MtGFormCRC<2, 0,
                        (outs GR:$rY), (ins),
                        "AInput\t $rY",
                        [(set GR:$rY, (MtGISD_INPUT_A))]>;
def INPUTB : MtGFormCRC<2, 1,
                        (outs GR:$rY), (ins),
                        "BInput\t $rY",
                        [(set GR:$rY, (MtGISD_INPUT_B))]>;
}

let Uses = [FLAG] in {
def SETF  : MtGFormCRC<2, 2, (outs GR:$rY), (ins), "SetF\t $rY",  []>;
def SETNF : MtGFormCRC<2, 3, (outs GR:$rY), (ins), "SetNF\t $rY", []>;
}

// --- X=3 : Jump / Call / Return -----------------------------------------
// The hardware encoding for Jumps is JumpFwd/Bwd at (3,0)/(3,1) with the
// bottom 4 bits selecting either a constant Z (1..11) or "read r0" (Z=0).
// LLVM only emits the r0 form — the preceding pair of NumBuilds loads the
// real displacement into r0 — so at the binary level Z is always 0.
let isBarrier = 1, isBranch = 1, isTerminator = 1, Uses = [R0] in {
def JUMPFWD : MtGFormCCC<3, 0, 0, (outs), (ins brtarget:$brt),
                         "JumpFwd\t $brt", []>;
def JUMPBWD : MtGFormCCC<3, 1, 0, (outs), (ins brtarget:$brt),
                         "JumpBwd\t $brt", []>;
}
let isBarrier = 1, isBranch = 1, isTerminator = 1, Uses = [R0, FLAG] in {
def JUMPFWDNF : MtGFormCCC<3, 2, 0, (outs), (ins brtarget:$brt),
                           "JumpFwdNF\t $brt", []>;
def JUMPBWDNF : MtGFormCCC<3, 3, 0, (outs), (ins brtarget:$brt),
                           "JumpBwdNF\t $brt", []>;
def JUMPFWDF  : MtGFormCCC<3, 4, 0, (outs), (ins brtarget:$brt),
                           "JumpFwdF\t $brt", []>;
def JUMPBWDF  : MtGFormCCC<3, 5, 0, (outs), (ins brtarget:$brt),
                           "JumpBwdF\t $brt", []>;
}

// Return uses Z=0 (read r0) unconditionally; the preceding NumBuild pair
// holds Z'. See MtGBranchSelector.
let isReturn = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1,
    hasDelaySlot = 0, Uses = [R0] in
def RETURN : MtGFormCCC<3, 7, 0, (outs), (ins), "Return\t r0", []>;

// --- X=4 : Halts, unary arithmetic, I/O out, Divide ---------------------
let isTerminator = 1, isBarrier = 1, hasCtrlDep = 1 in {
def HALTD : MtGFormCCC<4, 0, 0, (outs), (ins), "HaltD", []>;
def HALTA : MtGFormCCC<4, 0, 1, (outs), (ins), "HaltA", []>;
def HALTB : MtGFormCCC<4, 0, 2, (outs), (ins), "HaltB", []>;
}

let Constraints = "$rZ = $rZ_in" in
def ADD1 : MtGFormCCR<4, 1,
                      (outs GR:$rZ), (ins GR:$rZ_in),
                      "Add1\t $rZ", []>;

let Constraints = "$rZ = $rZ_in", Defs = [FLAG] in
def HALVE : MtGFormCCR<4, 2,
                       (outs GR:$rZ), (ins GR:$rZ_in),
                       "Halve\t $rZ", []>;

let hasSideEffects = 1 in
def OUTPUT : MtGFormCCR<4, 3,
                        (outs), (ins GR:$rZ),
                        "Output\t $rZ",
                        [(MtGISD_OUTPUT GR:$rZ)]>;

let Constraints = "$rZ = $rZ_in", Uses = [R0], Defs = [R6, FLAG] in
def DIVIDE : MtGFormCCR<4, 6,
                        (outs GR:$rZ), (ins GR:$rZ_in),
                        "Divide\t $rZ ; R6, $rZ <- ($rZ / R0, $rZ % R0)",
                        []>;

// --- X=5 : Move / Zero --------------------------------------------------
def MOVE : MtGFormCRR<5,
                      (outs GR:$rY), (ins GR:$rZ),
                      "Move\t $rY, $rZ", []>;

def ZERO : MtGFormCSR<5,
                      (outs GR:$r), (ins),
                      "Zero\t $r", []>;

// --- X=6 : Mult (rZ *= rY; destination is Z per spec) -------------------
let Constraints = "$rZ = $rZ_in" in
def MULT : MtGFormCRR<6,
                      (outs GR:$rZ), (ins GR:$rY, GR:$rZ_in),
                      "Mult\t $rZ, $rY", []>;

// --- X=8/9 : Memory -----------------------------------------------------
// NOTE: Per spec `Store mem[rZ] = rY` and `Load rY = mem[rZ]`. The asm
// form here prints `$addr, $val` / `$val, $addr` which is the *old* order
// — kept for this commit so pure format normalization is isolated. Stage 2
// fixes the order to match the spec's Y/Z roles.
let mayStore = 1 in
def STORE : MtGFormCRR<8,
                       (outs), (ins GR:$rY, GR:$rZ),
                       "Store\t $rY, $rZ", []>;

let mayLoad = 1 in
def LOAD : MtGFormCRR<9,
                      (outs GR:$rY), (ins GR:$rZ),
                      "Load\t $rY, $rZ", []>;

// --- X=10 : NumBuild ----------------------------------------------------
let Defs = [R0] in
def NUMBUILD : MtGFormCII<10,
                          (outs), (ins cardimm:$yimm, cardimm:$zimm),
                          "NumBuild\t $yimm, $zimm", []>;

// --- X=11 : Flag-setting compare ----------------------------------------
let Defs = [FLAG] in {
def FLESS : MtGFormCRR<11,
                       (outs), (ins GR:$rY, GR:$rZ),
                       "FLess\t $rY, $rZ", []>;
def FISZERO : MtGFormCSR<11,
                         (outs), (ins GR:$r),
                         "FIsZero\t $r", []>;
}


