//===- MtGAsmParser.cpp - Parse MtG assembly to MCInst instructions -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MtG.h"
#include "MtGRegisterInfo.h"
#include "MCTargetDesc/MtGMCTargetDesc.h"
#include "TargetInfo/MtGTargetInfo.h"

#include "llvm/ADT/APInt.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

#define DEBUG_TYPE "mtg-asm-parser"

using namespace llvm;

namespace {

/// Parses MtG assembly from a stream.
class MtGAsmParser : public MCTargetAsmParser {
  const MCSubtargetInfo &STI;
  MCAsmParser &Parser;
  const MCRegisterInfo *MRI;

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  ParseStatus parseDirective(AsmToken DirectiveID) override;
  bool ParseDirectiveRefSym(AsmToken DirectiveID);

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  bool parseJccInstruction(ParseInstructionInfo &Info, StringRef Name,
                           SMLoc NameLoc, OperandVector &Operands);

  bool ParseOperand(OperandVector &Operands);

  bool ParseLiteralValues(unsigned Size, SMLoc L);

  MCAsmParser &getParser() const { return Parser; }
  MCAsmLexer &getLexer() const { return Parser.getLexer(); }

  /// @name Auto-generated Matcher Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "MtGGenAsmMatcher.inc"

  /// }

public:
  MtGAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                  const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), STI(STI), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    MRI = getContext().getRegisterInfo();

    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

/// A parsed MtG assembly operand.
class MtGOperand : public MCParsedAsmOperand {
  typedef MCParsedAsmOperand Base;

  enum KindTy {
    k_Imm,
    k_Reg,
    k_Tok,
    k_Mem,
    k_IndReg,
    k_PostIndReg
  } Kind;

  struct Memory {
    MCRegister Reg;
    const MCExpr *Offset;
  };
  union {
    const MCExpr *Imm;
    MCRegister    Reg;
    StringRef     Tok;
    Memory        Mem;
  };

  SMLoc Start, End;

public:
  MtGOperand(StringRef Tok, SMLoc const &S)
      : Kind(k_Tok), Tok(Tok), Start(S), End(S) {}
  MtGOperand(KindTy Kind, MCRegister Reg, SMLoc const &S, SMLoc const &E)
      : Kind(Kind), Reg(Reg), Start(S), End(E) {}
  MtGOperand(MCExpr const *Imm, SMLoc const &S, SMLoc const &E)
      : Kind(k_Imm), Imm(Imm), Start(S), End(E) {}
  MtGOperand(MCRegister Reg, MCExpr const *Expr, SMLoc const &S,
                SMLoc const &E)
      : Kind(k_Mem), Mem({Reg, Expr}), Start(S), End(E) {}

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert((Kind == k_Reg || Kind == k_IndReg || Kind == k_PostIndReg) &&
        "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addExprOperand(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediate when possible
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Imm && "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    addExprOperand(Inst, Imm);
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Mem && "Unexpected operand kind");
    assert(N == 2 && "Invalid number of operands");

    Inst.addOperand(MCOperand::createReg(Mem.Reg));
    addExprOperand(Inst, Mem.Offset);
  }

  bool isReg()   const override { return Kind == k_Reg; }
  bool isImm()   const override { return Kind == k_Imm; }
  bool isToken() const override { return Kind == k_Tok; }
  bool isMem()   const override { return Kind == k_Mem; }
  bool isIndReg()         const { return Kind == k_IndReg; }
  bool isPostIndReg()     const { return Kind == k_PostIndReg; }

  bool isCGImm() const {
    if (Kind != k_Imm)
      return false;

    int64_t Val;
    if (!Imm->evaluateAsAbsolute(Val))
      return false;
    
    if (Val == 0 || Val == 1 || Val == 2 || Val == 4 || Val == 8 || Val == -1)
      return true;

    return false;
  }

  StringRef getToken() const {
    assert(Kind == k_Tok && "Invalid access!");
    return Tok;
  }

  MCRegister getReg() const override {
    assert(Kind == k_Reg && "Invalid access!");
    return Reg;
  }

  void setReg(MCRegister RegNo) {
    assert(Kind == k_Reg && "Invalid access!");
    Reg = RegNo;
  }

  static std::unique_ptr<MtGOperand> CreateToken(StringRef Str, SMLoc S) {
    return std::make_unique<MtGOperand>(Str, S);
  }

  static std::unique_ptr<MtGOperand> CreateReg(MCRegister Reg, SMLoc S,
                                                  SMLoc E) {
    return std::make_unique<MtGOperand>(k_Reg, Reg, S, E);
  }

  static std::unique_ptr<MtGOperand> CreateImm(const MCExpr *Val, SMLoc S,
                                                  SMLoc E) {
    return std::make_unique<MtGOperand>(Val, S, E);
  }

  static std::unique_ptr<MtGOperand>
  CreateMem(MCRegister Reg, const MCExpr *Val, SMLoc S, SMLoc E) {
    return std::make_unique<MtGOperand>(Reg, Val, S, E);
  }

  static std::unique_ptr<MtGOperand> CreateIndReg(MCRegister Reg, SMLoc S,
                                                     SMLoc E) {
    return std::make_unique<MtGOperand>(k_IndReg, Reg, S, E);
  }

  static std::unique_ptr<MtGOperand> CreatePostIndReg(MCRegister Reg,
                                                         SMLoc S, SMLoc E) {
    return std::make_unique<MtGOperand>(k_PostIndReg, Reg, S, E);
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void print(raw_ostream &O) const override {
    switch (Kind) {
    case k_Tok:
      O << "Token " << Tok;
      break;
    case k_Reg:
      O << "Register " << Reg;
      break;
    case k_Imm:
      O << "Immediate " << *Imm;
      break;
    case k_Mem:
      O << "Memory ";
      O << *Mem.Offset << "(" << Reg << ")";
      break;
    case k_IndReg:
      O << "RegInd " << Reg;
      break;
    case k_PostIndReg:
      O << "PostInc " << Reg;
      break;
    }
  }
};
} // end anonymous namespace

bool MtGAsmParser::matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (MatchResult) {
  case Match_Success:
    Inst.setLoc(Loc);
    Out.emitInstruction(Inst, STI);
    return false;
  case Match_MnemonicFail:
    return Error(Loc, "invalid instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = Loc;
    if (ErrorInfo != ~0U) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");

      ErrorLoc = ((MtGOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = Loc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  default:
    return true;
  }
}

// Auto-generated by TableGen
static MCRegister MatchRegisterName(StringRef Name);
static MCRegister MatchRegisterAltName(StringRef Name);

bool MtGAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                    SMLoc &EndLoc) {
  ParseStatus Res = tryParseRegister(Reg, StartLoc, EndLoc);
  if (Res.isFailure())
    return Error(StartLoc, "invalid register name");
  if (Res.isSuccess())
    return false;
  if (Res.isNoMatch())
    return true;

  llvm_unreachable("unknown parse status");
}

ParseStatus MtGAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                              SMLoc &EndLoc) {
  if (getLexer().getKind() == AsmToken::Identifier) {
    auto Name = getLexer().getTok().getIdentifier().lower();
    Reg = MatchRegisterName(Name);
    if (Reg == MtG::NoRegister) {
      Reg = MatchRegisterAltName(Name);
      if (Reg == MtG::NoRegister)
        return ParseStatus::NoMatch;
    }

    AsmToken const &T = getParser().getTok();
    StartLoc = T.getLoc();
    EndLoc = T.getEndLoc();
    getLexer().Lex(); // eat register token

    return ParseStatus::Success;
  }

  return ParseStatus::Failure;
}

bool MtGAsmParser::parseJccInstruction(ParseInstructionInfo &Info,
                                          StringRef Name, SMLoc NameLoc,
                                          OperandVector &Operands) {
  if (!Name.starts_with_insensitive("j"))
    return true;

  auto CC = Name.drop_front().lower();
  unsigned CondCode;
  if (CC == "ne" || CC == "nz")
    CondCode = MtGCC::COND_NE;
  else if (CC == "eq" || CC == "z")
    CondCode = MtGCC::COND_E;
  else if (CC == "lo" || CC == "nc")
    CondCode = MtGCC::COND_LO;
  else if (CC == "hs" || CC == "c")
    CondCode = MtGCC::COND_HS;
  else if (CC == "n")
    CondCode = MtGCC::COND_N;
  else if (CC == "ge")
    CondCode = MtGCC::COND_GE;
  else if (CC == "l")
    CondCode = MtGCC::COND_L;
  else if (CC == "mp")
    CondCode = MtGCC::COND_NONE;
  else
    return Error(NameLoc, "unknown instruction");

  if (CondCode == (unsigned)MtGCC::COND_NONE)
    Operands.push_back(MtGOperand::CreateToken("jmp", NameLoc));
  else {
    Operands.push_back(MtGOperand::CreateToken("j", NameLoc));
    const MCExpr *CCode = MCConstantExpr::create(CondCode, getContext());
    Operands.push_back(MtGOperand::CreateImm(CCode, SMLoc(), SMLoc()));
  }

  // Skip optional '$' sign.
  (void)parseOptionalToken(AsmToken::Dollar);

  const MCExpr *Val;
  SMLoc ExprLoc = getLexer().getLoc();
  if (getParser().parseExpression(Val))
    return Error(ExprLoc, "expected expression operand");

  int64_t Res;
  if (Val->evaluateAsAbsolute(Res))
    if (Res < -512 || Res > 511)
      return Error(ExprLoc, "invalid jump offset");

  Operands.push_back(MtGOperand::CreateImm(Val, ExprLoc,
    getLexer().getLoc()));

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool MtGAsmParser::parseInstruction(ParseInstructionInfo &Info,
                                       StringRef Name, SMLoc NameLoc,
                                       OperandVector &Operands) {
  // Drop .w suffix
  if (Name.ends_with_insensitive(".w"))
    Name = Name.drop_back(2);

  if (!parseJccInstruction(Info, Name, NameLoc, Operands))
    return false;

  // First operand is instruction mnemonic
  Operands.push_back(MtGOperand::CreateToken(Name, NameLoc));

  // If there are no more operands, then finish
  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  // Parse first operand
  if (ParseOperand(Operands))
    return true;

  // Parse second operand if any
  if (parseOptionalToken(AsmToken::Comma) && ParseOperand(Operands))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool MtGAsmParser::ParseDirectiveRefSym(AsmToken DirectiveID) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier in directive");

  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
  getStreamer().emitSymbolAttribute(Sym, MCSA_Global);
  return parseEOL();
}

ParseStatus MtGAsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal.lower() == ".long")
    return ParseLiteralValues(4, DirectiveID.getLoc());
  if (IDVal.lower() == ".word" || IDVal.lower() == ".short")
    return ParseLiteralValues(2, DirectiveID.getLoc());
  if (IDVal.lower() == ".byte")
    return ParseLiteralValues(1, DirectiveID.getLoc());
  if (IDVal.lower() == ".refsym")
    return ParseDirectiveRefSym(DirectiveID);
  return ParseStatus::NoMatch;
}

bool MtGAsmParser::ParseOperand(OperandVector &Operands) {
  switch (getLexer().getKind()) {
    default: return true;
    case AsmToken::Identifier: {
      // try rN
      MCRegister RegNo;
      SMLoc StartLoc, EndLoc;
      if (!parseRegister(RegNo, StartLoc, EndLoc)) {
        Operands.push_back(MtGOperand::CreateReg(RegNo, StartLoc, EndLoc));
        return false;
      }
      [[fallthrough]];
    }
    case AsmToken::Integer:
    case AsmToken::Plus:
    case AsmToken::Minus: {
      SMLoc StartLoc = getParser().getTok().getLoc();
      const MCExpr *Val;
      // Try constexpr[(rN)]
      if (!getParser().parseExpression(Val)) {
        MCRegister RegNo = MtG::PC;
        SMLoc EndLoc = getParser().getTok().getLoc();
        // Try (rN)
        if (parseOptionalToken(AsmToken::LParen)) {
          SMLoc RegStartLoc;
          if (parseRegister(RegNo, RegStartLoc, EndLoc))
            return true;
          EndLoc = getParser().getTok().getEndLoc();
          if (!parseOptionalToken(AsmToken::RParen))
            return true;
        }
        Operands.push_back(MtGOperand::CreateMem(RegNo, Val, StartLoc,
          EndLoc));
        return false;
      }
      return true;
    }
    case AsmToken::Amp: {
      // Try &constexpr
      SMLoc StartLoc = getParser().getTok().getLoc();
      getLexer().Lex(); // Eat '&'
      const MCExpr *Val;
      if (!getParser().parseExpression(Val)) {
        SMLoc EndLoc = getParser().getTok().getLoc();
        Operands.push_back(MtGOperand::CreateMem(MtG::SR, Val, StartLoc,
          EndLoc));
        return false;
      }
      return true;
    }
    case AsmToken::At: {
      // Try @rN[+]
      SMLoc StartLoc = getParser().getTok().getLoc();
      getLexer().Lex(); // Eat '@'
      MCRegister RegNo;
      SMLoc RegStartLoc, EndLoc;
      if (parseRegister(RegNo, RegStartLoc, EndLoc))
        return true;
      if (parseOptionalToken(AsmToken::Plus)) {
        Operands.push_back(MtGOperand::CreatePostIndReg(RegNo, StartLoc, EndLoc));
        return false;
      }
      if (Operands.size() > 1) // Emulate @rd in destination position as 0(rd)
        Operands.push_back(MtGOperand::CreateMem(RegNo,
            MCConstantExpr::create(0, getContext()), StartLoc, EndLoc));
      else
        Operands.push_back(MtGOperand::CreateIndReg(RegNo, StartLoc, EndLoc));
      return false;
    }
    case AsmToken::Hash:
      // Try #constexpr
      SMLoc StartLoc = getParser().getTok().getLoc();
      getLexer().Lex(); // Eat '#'
      const MCExpr *Val;
      if (!getParser().parseExpression(Val)) {
        SMLoc EndLoc = getParser().getTok().getLoc();
        Operands.push_back(MtGOperand::CreateImm(Val, StartLoc, EndLoc));
        return false;
      }
      return true;
  }
}

bool MtGAsmParser::ParseLiteralValues(unsigned Size, SMLoc L) {
  auto parseOne = [&]() -> bool {
    const MCExpr *Value;
    if (getParser().parseExpression(Value))
      return true;
    getParser().getStreamer().emitValue(Value, Size, L);
    return false;
  };
  return (parseMany(parseOne));
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMtGAsmParser() {
  RegisterMCAsmParser<MtGAsmParser> X(getTheMtGTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "MtGGenAsmMatcher.inc"

static MCRegister convertGR16ToGR8(MCRegister Reg) {
  switch (Reg.id()) {
  default:
    llvm_unreachable("Unknown GR16 register");
  case MtG::PC:  return MtG::PCB;
  case MtG::SP:  return MtG::SPB;
  case MtG::SR:  return MtG::SRB;
  case MtG::CG:  return MtG::CGB;
  case MtG::R4:  return MtG::R4B;
  case MtG::R5:  return MtG::R5B;
  case MtG::R6:  return MtG::R6B;
  case MtG::R7:  return MtG::R7B;
  case MtG::R8:  return MtG::R8B;
  case MtG::R9:  return MtG::R9B;
  case MtG::R10: return MtG::R10B;
  case MtG::R11: return MtG::R11B;
  case MtG::R12: return MtG::R12B;
  case MtG::R13: return MtG::R13B;
  case MtG::R14: return MtG::R14B;
  case MtG::R15: return MtG::R15B;
  }
}

unsigned MtGAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                     unsigned Kind) {
  MtGOperand &Op = static_cast<MtGOperand &>(AsmOp);

  if (!Op.isReg())
    return Match_InvalidOperand;

  MCRegister Reg = Op.getReg();
  bool isGR16 =
      MtGMCRegisterClasses[MtG::GR16RegClassID].contains(Reg);

  if (isGR16 && (Kind == MCK_GR8)) {
    Op.setReg(convertGR16ToGR8(Reg));
    return Match_Success;
  }

  return Match_InvalidOperand;
}
