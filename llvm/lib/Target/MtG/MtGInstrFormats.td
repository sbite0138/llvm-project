//===-- MtGInstrFormats.td - MtG Instruction Formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe MtG instructions format here
//


// Generic MtG Format
class MtGInst<dag outs, dag ins, string asmstr> : Instruction {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;

  let Namespace = "MtG";

  dag OutOperandList = outs;
  dag InOperandList  = ins;

  let AsmString = asmstr;
  let Size = 16;
}


class CRRForm<bits<4> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
  : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;

  let Inst{15-12} = opcode;
  let Inst{11-8}  = 0b0000;
  let Inst{7-4}   = 0b0000;
  let Inst{3-0}   = 0b0000;
}

class CRCForm<bits<8> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
  : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;

  let Inst{15-12} = opcode{7-4};
  let Inst{11-8}  = 0b0000;
  let Inst{7-4}   = opcode{3-0};
  let Inst{3-0}   = 0b0000;
}
class CCCForm<bits<12> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
  : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;

  let Inst{15-4} = opcode;
  let Inst{3-0}   = 0b0000;
}

class CCRForm<bits<8> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
  : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;

  let Inst{15-8} = opcode;
  let Inst{7-4}   = 0b0000;
  let Inst{3-0}   = 0b0000;
}

class CIIForm<bits<4> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
  : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;

  let Inst{15-12} = opcode;
  let Inst{11-0}  = 0b000000000000;
}


class CCIForm<bits<8> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
  : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;

  let Inst{15-8} = opcode;
  let Inst{7-0}   = 0b00000000;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
  : MtGInst<outs, ins, asmstr> {
  let Pattern = pattern;
  //let isCodeGenOnly = true;
  let isPseudo = true;
}
