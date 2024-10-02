//===-- MtGInstPrinter.cpp - Convert MtG MCInst to assembly syntax --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an MtG MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "MtGInstPrinter.h"
#include "MtG.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "MtGGenAsmWriter.inc"

void MtGInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) const {
  O << getRegisterName(Reg);
}

void MtGInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                  StringRef Annot, const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  if (!printAliasInstr(MI, Address, O))
    printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void MtGInstPrinter::printPCRelImmOperand(const MCInst *MI, unsigned OpNo,
                                             raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    int64_t Imm = Op.getImm() * 2 + 2;
    O << "$";
    if (Imm >= 0)
      O << '+';
    O << Imm;
  } else {
    assert(Op.isExpr() && "unknown pcrel immediate operand");
    Op.getExpr()->print(O, &MAI);
  }
}

void MtGInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O, const char *Modifier) {
  assert((Modifier == nullptr || Modifier[0] == 0) && "No modifiers supported");
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    O << getRegisterName(Op.getReg());
  } else if (Op.isImm()) {
    O << '#' << Op.getImm();
  } else {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    O << '#';
    Op.getExpr()->print(O, &MAI);
  }
}

void MtGInstPrinter::printSrcMemOperand(const MCInst *MI, unsigned OpNo,
                                           raw_ostream &O,
                                           const char *Modifier) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Disp = MI->getOperand(OpNo+1);

  // Print displacement first

  // If the global address expression is a part of displacement field with a
  // register base, we should not emit any prefix symbol here, e.g.
  //   mov.w &foo, r1
  // vs
  //   mov.w glb(r1), r2
  // Otherwise (!) mtg-as will silently miscompile the output :(
  if (Base.getReg() == MtG::SR)
    O << '&';

  if (Disp.isExpr())
    Disp.getExpr()->print(O, &MAI);
  else {
    assert(Disp.isImm() && "Expected immediate in displacement field");
    O << Disp.getImm();
  }

  // Print register base field
  if ((Base.getReg() != MtG::SR) &&
      (Base.getReg() != MtG::PC))
    O << '(' << getRegisterName(Base.getReg()) << ')';
}

void MtGInstPrinter::printIndRegOperand(const MCInst *MI, unsigned OpNo,
                                           raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  O << "@" << getRegisterName(Base.getReg());
}

void MtGInstPrinter::printPostIndRegOperand(const MCInst *MI, unsigned OpNo,
                                               raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  O << "@" << getRegisterName(Base.getReg()) << "+";
}

void MtGInstPrinter::printCCOperand(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  unsigned CC = MI->getOperand(OpNo).getImm();

  switch (CC) {
  default:
   llvm_unreachable("Unsupported CC code");
  case MtGCC::COND_E:
   O << "eq";
   break;
  case MtGCC::COND_NE:
   O << "ne";
   break;
  case MtGCC::COND_HS:
   O << "hs";
   break;
  case MtGCC::COND_LO:
   O << "lo";
   break;
  case MtGCC::COND_GE:
   O << "ge";
   break;
  case MtGCC::COND_L:
   O << 'l';
   break;
  case MtGCC::COND_N:
   O << 'n';
   break;
  }
}
