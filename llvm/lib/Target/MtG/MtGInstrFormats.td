//===-- MtGInstrFormats.td - MtG Instruction Formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Every MtG instruction is a 3-card tuple (X, Y, Z). Each field is a base-12
// digit (0..11) mapped onto a basic-land card, so each field fits in 4 bits.
// The 12 meaningful bits live in Inst{15-4}; Inst{3-0} is unused padding so
// the on-disk instruction aligns to 16 bits.
//
// The format classes below split instructions by the *shape* of (X, Y, Z):
// which fields are constants baked into the opcode, which are register
// operand slots, and which are 4-bit immediate slots. This lets TableGen
// bind the instruction's operand names (via matching `bits<4>` fields) to
// the Y/Z bit positions — something the old formats did not do, which left
// the binary encoder emitting the opcode's X nibble plus zeros for Y/Z.
//
//===----------------------------------------------------------------------===//

// Generic 16-bit instruction. Subclasses set X/Y/Z via let-bindings or by
// declaring `bits<4>` fields that match operand names.
class MtGInst<dag outs, dag ins, string asmstr> : Instruction {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;

  let Namespace = "MtG";

  dag OutOperandList = outs;
  dag InOperandList  = ins;

  let AsmString = asmstr;
  let Size = 16;
  let hasNoSchedulingInfo = 1;

  // Bottom 4 bits are unused padding.
  let Inst{3-0} = 0;
}

// ==== Concrete format classes ============================================
// Naming convention: MtGForm<X-shape><Y-shape><Z-shape> where each shape is
//   C : constant baked into the opcode
//   R : register operand  (4-bit register number)
//   I : 4-bit immediate  (0..11, i.e. one card digit)
//   S : same as the previous R position (Y and Z share the same register)

// All three fields are constants. Covers Halt{A,B,D}, Return (Z=0 form) and
// the Call variants when emitted as concrete mnemonics.
class MtGFormCCC<bits<4> X, bits<4> Y, bits<4> Z,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  let Inst{15-12} = X;
  let Inst{11-8}  = Y;
  let Inst{7-4}   = Z;
  let Pattern = pattern;
}

// X and Y constant, Z is a register operand named $rZ.
// Covers Add1, Halve, Output, Divide.
class MtGFormCCR<bits<4> X, bits<4> Y,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  bits<4> rZ;
  let Inst{15-12} = X;
  let Inst{11-8}  = Y;
  let Inst{7-4}   = rZ;
  let Pattern = pattern;
}

// X and Y constant, Z is a 4-bit immediate named $zimm.
// Covers the conditional-immediate Jump forms and Return (Z=1..11 case).
class MtGFormCCI<bits<4> X, bits<4> Y,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  bits<4> zimm;
  let Inst{15-12} = X;
  let Inst{11-8}  = Y;
  let Inst{7-4}   = zimm;
  let Pattern = pattern;
}

// X constant, Y is a register operand named $rY, Z constant.
// Covers SetF, SetNF, AInput, BInput (Rand6, Rand20 would fit here too).
class MtGFormCRC<bits<4> X, bits<4> Z,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  bits<4> rY;
  let Inst{15-12} = X;
  let Inst{11-8}  = rY;
  let Inst{7-4}   = Z;
  let Pattern = pattern;
}

// X constant, Y and Z are separate register operands ($rY, $rZ).
// Covers Add, SubCond, Move, Mult, Store, Load, FLess.
class MtGFormCRR<bits<4> X,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  bits<4> rY;
  bits<4> rZ;
  let Inst{15-12} = X;
  let Inst{11-8}  = rY;
  let Inst{7-4}   = rZ;
  let Pattern = pattern;
}

// X constant, Y and Z are both the same register operand $r.
// The spec encodes "same register" by duplicating the register number into
// both fields. Covers Sub1Cond (1 Z Z), Zero (5 Y Y), FIsZero (11 Z Z).
class MtGFormCSR<bits<4> X,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  bits<4> r;
  let Inst{15-12} = X;
  let Inst{11-8}  = r;
  let Inst{7-4}   = r;
  let Pattern = pattern;
}

// X constant, Y and Z are 4-bit immediate operands ($yimm, $zimm).
// Covers NumBuild.
class MtGFormCII<bits<4> X,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  bits<4> yimm;
  bits<4> zimm;
  let Inst{15-12} = X;
  let Inst{11-8}  = yimm;
  let Inst{7-4}   = zimm;
  let Pattern = pattern;
}

// Pseudo instructions — no real encoding; get expanded or are IR-only.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}
