//===-- MtGISelLowering.cpp - MtG DAG Lowering Implementation  ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MtGTargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "MtGISelLowering.h"
#include "MCTargetDesc/MtGMCTargetDesc.h"
#include "MtG.h"
#include "MtGFrameLowering.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGRegisterInfo.h"
#include "MtGSubtarget.h"
#include "MtGTargetMachine.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>
using namespace llvm;

#define DEBUG_TYPE "mtg-lower"

static cl::opt<bool> MtGNoLegalImmediate(
    "mtg-no-legal-immediate", cl::Hidden,
    cl::desc("Enable non legal immediates (for testing purposes only)"),
    cl::init(false));
// MtGTargetLowering.cpp
MtGTargetLowering::MtGTargetLowering(const TargetMachine &TM,
                                     const MtGSubtarget &STI)
    : TargetLowering(TM, STI) {

  addRegisterClass(MVT::i32, &MtG::GRRegClass);

  // setStackPointerRegisterToSaveRestore(MtG::R8);
  setOperationAction(ISD::SDIV, MVT::i32, Custom);
  setOperationAction(ISD::MULHS, MVT::i32, Expand);
  setOperationAction(ISD::MULHU, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setMinimumJumpTableEntries(UINT_MAX);
  setOperationAction(ISD::BR_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT, MVT::i32, Legal);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);

  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  // Sub-word loads and truncating stores — needed for `char` / `short`
  // struct fields and any byte-level memory traffic. MtG's memory model
  // stores a full value into each addressed cell, so:
  //   - truncstore i8/i16 → plain Store of the low-byte/low-halfword
  //     (the callers mask if needed; for constants clang already masks).
  //   - zextload / "any" extload i8/i16 → plain Load; the cell already
  //     holds the byte value in [0, 2^N) range.
  //   - sextload i8/i16 → expand via `(x << (32-N)) >>s (32-N)` so the
  //     existing SHL_MACRO / ASHR_MACRO sign-fill the top bits.
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i8, Expand);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i16, Expand);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i8, Legal);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i16, Legal);
  setLoadExtAction(ISD::EXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::EXTLOAD, MVT::i32, MVT::i8, Legal);
  setLoadExtAction(ISD::EXTLOAD, MVT::i32, MVT::i16, Legal);
  setTruncStoreAction(MVT::i32, MVT::i1, Expand);
  setTruncStoreAction(MVT::i32, MVT::i8, Legal);
  setTruncStoreAction(MVT::i32, MVT::i16, Legal);

  // No direct `sign_extend_inreg` op — expand to `(x << N) >>s N` where N
  // is (32 - sub-word width). The existing SHL_MACRO / ASHR_MACRO handle
  // both sides. This is what the generic expander does anyway; mark it
  // explicitly so the legalizer doesn't fall through to "can't select".
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);

  computeRegisterProperties(STI.getRegisterInfo());
}
EVT MtGTargetLowering::getSetCCResultType(const DataLayout &DL,
                                          LLVMContext &Ctx, EVT VT) const {
  return MVT::i32;
}

bool MtGTargetLowering::isIntDivCheap(EVT VT, AttributeList Attr) const {
  return true;
}

std::pair<unsigned, const TargetRegisterClass *>
MtGTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                StringRef Constraint,
                                                MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'r':
      return std::make_pair(0U, &MtG::GRRegClass);
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

SDValue MtGTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default: {
    Op.dump();
    llvm_unreachable("unimplemented operand");
  }
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::SDIV:
    return LowerSDIV(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::VAARG:
    return LowerVAARG(Op, DAG);
  }
}

// Expand sdiv into the standard "absolute value + udiv + sign fix-up"
// sequence. MtG hardware only has an unsigned Divide instruction, but
// the existing udiv pattern already lowers to it, so we lean on that.
//
//   sa = a >> 31           (arithmetic, gives 0 or -1)
//   sb = b >> 31
//   |a| = (a ^ sa) - sa
//   |b| = (b ^ sb) - sb
//   q = udiv(|a|, |b|)
//   sign = sa ^ sb         (-1 if signs of a, b differ; 0 otherwise)
//   result = (q ^ sign) - sign
SDValue MtGTargetLowering::LowerSDIV(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  SDValue Shift31 = DAG.getConstant(31, DL, VT);
  SDValue SA = DAG.getNode(ISD::SRA, DL, VT, LHS, Shift31);
  SDValue SB = DAG.getNode(ISD::SRA, DL, VT, RHS, Shift31);

  SDValue AbsA = DAG.getNode(
      ISD::SUB, DL, VT, DAG.getNode(ISD::XOR, DL, VT, LHS, SA), SA);
  SDValue AbsB = DAG.getNode(
      ISD::SUB, DL, VT, DAG.getNode(ISD::XOR, DL, VT, RHS, SB), SB);

  SDValue Quot = DAG.getNode(ISD::UDIV, DL, VT, AbsA, AbsB);

  SDValue ResultSign = DAG.getNode(ISD::XOR, DL, VT, SA, SB);
  return DAG.getNode(
      ISD::SUB, DL, VT,
      DAG.getNode(ISD::XOR, DL, VT, Quot, ResultSign), ResultSign);
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "MtGGenCallingConv.inc"

/// For each argument in a function store the number of pieces it is
/// composed of.
template <typename ArgT>
static void ParseFunctionArgs(const SmallVectorImpl<ArgT> &Args,
                              SmallVectorImpl<unsigned> &Out) {
  unsigned CurrentArgIndex;

  if (Args.empty())
    return;

  CurrentArgIndex = Args[0].OrigArgIndex;
  Out.push_back(0);

  for (auto &Arg : Args) {
    if (CurrentArgIndex == Arg.OrigArgIndex) {
      Out.back() += 1;
    } else {
      Out.push_back(1);
      CurrentArgIndex = Arg.OrigArgIndex;
    }
  }
}

static void AnalyzeVarArgs(CCState &State,
                           const SmallVectorImpl<ISD::OutputArg> &Outs) {
  State.AnalyzeCallOperands(Outs, CC_MtG);
}

static void AnalyzeVarArgs(CCState &State,
                           const SmallVectorImpl<ISD::InputArg> &Ins) {
  State.AnalyzeFormalArguments(Ins, CC_MtG);
}

static void AnalyzeRetResult(CCState &State,
                             const SmallVectorImpl<ISD::InputArg> &Ins) {
  State.AnalyzeCallResult(Ins, RetCC_MtG);
}

static void AnalyzeRetResult(CCState &State,
                             const SmallVectorImpl<ISD::OutputArg> &Outs) {
  State.AnalyzeReturn(Outs, RetCC_MtG);
}

template <typename ArgT>
static void AnalyzeReturnValues(CCState &State,
                                SmallVectorImpl<CCValAssign> &RVLocs,
                                const SmallVectorImpl<ArgT> &Args) {
  AnalyzeRetResult(State, Args);
}

SDValue MtGTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                     SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &dl = CLI.DL;
  SDValue Chain = CLI.Chain;

  // MtG has no tail-call (branch-to-callee) form — the only call
  // encoding pushes a return address onto the hardware return stack.
  // Force every supposedly-tail call back to a regular call so we
  // don't trip the CallLoweringInfo invariant that tail-call lowers
  // must leave InVals empty (which our LowerCallResult always fills).
  CLI.IsTailCall = false;

  // Intercept calls to the MtG "output" builtin. Programs can emit a value
  // by declaring `declare void @__mtg_output(i32)` and calling it; we lower
  // the call directly to the hardware Output instruction instead of a real
  // ABI call sequence.
  auto isMtGOutput = [&] {
    if (auto *GA = dyn_cast<GlobalAddressSDNode>(CLI.Callee))
      if (const Function *F = dyn_cast<Function>(GA->getGlobal()))
        return F->getName() == "__mtg_output" && F->arg_size() == 1;
    if (auto *ES = dyn_cast<ExternalSymbolSDNode>(CLI.Callee))
      return StringRef(ES->getSymbol()) == "__mtg_output";
    return false;
  }();
  if (isMtGOutput) {
    assert(CLI.OutVals.size() == 1 && "__mtg_output expects 1 arg");
    SDValue Arg = CLI.OutVals[0];
    SDLoc DL = CLI.DL;

    // Emit MtGISD::OUTPUT with the value operand; TableGen pattern will
    // match it to the real Output instruction.
    Chain = DAG.getNode(MtGISD::OUTPUT, DL, MVT::Other, Chain, Arg);
    return Chain;
  }

  // Intercept calls to the MtG input builtins. They lower directly to the
  // hardware AInput / BInput instructions and produce an i32 result.
  auto matchInput = [&](StringRef Name) {
    if (auto *GA = dyn_cast<GlobalAddressSDNode>(CLI.Callee))
      if (const Function *F = dyn_cast<Function>(GA->getGlobal()))
        return F->getName() == Name && F->arg_size() == 0;
    if (auto *ES = dyn_cast<ExternalSymbolSDNode>(CLI.Callee))
      return StringRef(ES->getSymbol()) == Name;
    return false;
  };
  bool isInputA = matchInput("__mtg_input_a");
  bool isInputB = matchInput("__mtg_input_b");
  if (isInputA || isInputB) {
    assert(CLI.OutVals.empty() && "__mtg_input_* takes no arguments");
    assert(CLI.Ins.size() == 1 && CLI.Ins[0].VT == MVT::i32 &&
           "__mtg_input_* returns a single i32");
    SDLoc DL = CLI.DL;
    unsigned Opc = isInputA ? MtGISD::INPUT_A : MtGISD::INPUT_B;
    SDValue Result = DAG.getNode(Opc, DL,
                                 DAG.getVTList(MVT::i32, MVT::Other), Chain);
    Chain = Result.getValue(1);
    InVals.push_back(Result.getValue(0));
    return Chain;
  }
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Callee = CLI.Callee;
  bool &isTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;
  SmallVector<CCValAssign, 16> RVLocs;
  SmallVector<CCValAssign, 16> ArgLocs;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  MachineFunction &MF = DAG.getMachineFunction();

  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_MtG);
  unsigned NumBytes = CCInfo.getStackSize();

  // TODO: Handle byval arguments

  if (!isTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, CLI.DL);
  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    SDValue ArgValue = OutVals[i];
    CCValAssign &VA = ArgLocs[i];
    MVT LocVT = VA.getLocVT();

    if (VA.getLocInfo() != CCValAssign::Full) {
      llvm_unreachable("Unsupported argument location");
    }
    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), ArgValue));
      continue;
    }
    assert(VA.isMemLoc() && "Unknown argument location");

    if (!StackPtr.getNode()) {
      StackPtr = DAG.getCopyFromReg(Chain, dl, MtG::R2, PtrVT);
    }
    // Outgoing-arg slots start just above the 4-byte emergency-spill slot
    // that MtGFrameLowering reserves at *SP; see MtGRegisterInfo's
    // eliminateFrameIndex, which applies the same shift to all FI-relative
    // loads/stores. Mismatching the two sides would make the callee read
    // from a slot the caller never wrote (bug surfaced as uninitialized-
    // memory asserts under ursa).
    SDValue Address = DAG.getNode(
        ISD::ADD, dl, PtrVT, StackPtr,
        DAG.getIntPtrConstant(VA.getLocMemOffset() +
                                  MtGFrameLowering::kEmergencySlotSize,
                              dl));

    // Emit the store.
    MemOpChains.push_back(
        DAG.getStore(Chain, dl, ArgValue, Address,
                     MachinePointerInfo::getStack(MF, VA.getLocMemOffset())));
  }
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);
  SDValue Glue;

  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, dl, Reg.first, Reg.second, Glue);
    Glue = Chain.getValue(1);
  }

  if (GlobalAddressSDNode *S = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = S->getGlobal();
    unsigned OpFlags = MtGII::MO_CALL;

    Callee = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, OpFlags);

  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), PtrVT, MtGII::MO_CALL);
  } else {
    llvm_unreachable("Unsupported callee");
  }
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  if (Glue.getNode())
    Ops.push_back(Glue);

  Chain = DAG.getNode(MtGISD::CALL, dl, NodeTys, Ops);
  Glue = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, dl);
  Glue = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, Glue, CallConv, isVarArg, Ins, dl, DAG, InVals);
}
/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
///
SDValue MtGTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  AnalyzeReturnValues(CCInfo, RVLocs, Ins);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    Chain = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                               RVLocs[i].getValVT(), InGlue)
                .getValue(1);
    InGlue = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}
SDValue
MtGTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &dl, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  // CCValAssign - represent the assignment of the return value
  // to a location
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  CCInfo.AnalyzeReturn(Outs, RetCC_MtG);
  // SDValue Glue;
  // SmallVector<SDValue, 4> RetOps(1, Chain);
  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    SDValue Val = OutVals[i];
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");
    assert(RVLocs[i].getValVT() == RVLocs[i].getLocVT() &&
           "Return value and register value types must match");

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Val, Flag);

    // Guarantee that all emitted copies are stuck together,
    // avoiding something bad.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }
  RetOps[0] = Chain; // Update chain.

  // Add the glue if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(MtGISD::RET_GLUE, dl, MVT::Other, RetOps);
}

SDValue MtGTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MtGMachineFunctionInfo *MtGMFI = MF.getInfo<MtGMachineFunctionInfo>();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_MtG);

  Function::const_arg_iterator FuncArg =
      DAG.getMachineFunction().getFunction().arg_begin();

  std::vector<SDValue> OutChains;

  unsigned CurArgIdx = 0;
  CCInfo.rewindByValRegsInfo();

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    if (Ins[i].isOrigArg()) {
      std::advance(FuncArg, Ins[i].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[i].getOrigArgIndex();
    }
    EVT ValVT = VA.getValVT();

    bool IsRegLoc = VA.isRegLoc();

    if (IsRegLoc) {
      MVT RegVT = VA.getLocVT();
      unsigned ArgReg = VA.getLocReg();
      const TargetRegisterClass *RC = getRegClassFor(RegVT);

      unsigned Reg = MF.getRegInfo().createVirtualRegister(RC);
      MF.getRegInfo().addLiveIn(ArgReg, Reg);

      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, Reg, RegVT);
      if (VA.getLocInfo() != CCValAssign::Full) {
        unsigned Opcode = 0;
        if (VA.getLocInfo() == CCValAssign::SExt)
          Opcode = ISD::AssertSext;
        else if (VA.getLocInfo() == CCValAssign::ZExt)
          Opcode = ISD::AssertZext;
        if (Opcode)
          ArgValue =
              DAG.getNode(Opcode, dl, RegVT, ArgValue, DAG.getValueType(ValVT));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, ValVT, ArgValue);
      }
      InVals.push_back(ArgValue);
    } else {
      MVT LocVT = VA.getLocVT();
      assert(VA.isMemLoc());

      int FI = MFI.CreateFixedObject(ValVT.getSizeInBits() / 8,
                                     VA.getLocMemOffset(), true);
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      SDValue Load = DAG.getLoad(
          LocVT, dl, Chain, FIN,
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));
      InVals.push_back(Load);
      OutChains.push_back(Load.getValue(1));
    }
  }

  if (isVarArg) {
    int VarArgsOffset = CCInfo.getStackSize();
    int FI = MFI.CreateFixedObject(4, VarArgsOffset, true);
    MtGMFI->setVarArgsFrameIndex(FI);
  }

  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains);
  }
  return Chain;
}

const char *MtGTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((MtGISD::NodeType)Opcode) {
  case MtGISD::FIRST_NUMBER:
    break;
  case MtGISD::RET_GLUE:
    return "MtGISD::RET_GLUE";
  case MtGISD::RETI_GLUE:
    return "MtGISD::RETI_GLUE";
  case MtGISD::RRA:
    return "MtGISD::RRA";
  case MtGISD::RLA:
    return "MtGISD::RLA";
  case MtGISD::RRC:
    return "MtGISD::RRC";
  case MtGISD::RRCL:
    return "MtGISD::RRCL";
  case MtGISD::CALL:
    return "MtGISD::CALL";
  case MtGISD::Wrapper:
    return "MtGISD::Wrapper";
  case MtGISD::BR_CC:
    return "MtGISD::BR_CC";
  case MtGISD::CMP:
    return "MtGISD::CMP";
  case MtGISD::SETCC:
    return "MtGISD::SETCC";
  case MtGISD::SELECT_CC:
    return "MtGISD::SELECT_CC";
  case MtGISD::DADD:
    return "MtGISD::DADD";
  case MtGISD::INPUT_A:
    return "MtGISD::INPUT_A";
  case MtGISD::INPUT_B:
    return "MtGISD::INPUT_B";
  }
  return nullptr;
}
static MachineBasicBlock *isolateInstrInNewBlock(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();

  // すでに MI の前に terminator があるか確認
  bool TerminatorBefore = false;
  for (auto I = MBB->begin(), E = MachineBasicBlock::iterator(MI); I != E;
       ++I) {
    if (I->isTerminator()) {
      TerminatorBefore = true;
      break;
    }
  }
  if (!TerminatorBefore)
    return MBB; // 安全、分割不要

  // 分割：MI 以降を NewMBB へ
  auto *NewMBB = MF.CreateMachineBasicBlock(MBB->getBasicBlock());
  MF.insert(std::next(MachineFunction::iterator(MBB)), NewMBB);

  // [MI, end) を丸ごと移す（MI を含む！）
  NewMBB->splice(NewMBB->end(), MBB, MachineBasicBlock::iterator(MI),
                 MBB->end());

  // 旧後続を NewMBB へ移し、PHI も更新
  NewMBB->transferSuccessorsAndUpdatePHIs(MBB);

  // ここで MBB 側に追加命令は置かない（先に terminator があるので触らない）
  // NewMBB 内なら MI の“前”に非終端をいくらでも置ける
  return NewMBB;
}

MachineBasicBlock *
MtGTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  MachineBasicBlock *MBB = BB; // 引数で渡される BB
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  auto &MRI = MF.getRegInfo();                           // レジスタ情報
  const auto *TRI = MF.getSubtarget().getRegisterInfo(); // レジスタ情報
  DebugLoc DL = MI.getDebugLoc(); // 位置情報（無ければ DebugLoc()）

  llvm::SmallVector<Register, 7> WorkRegs = {
      MtG::R0, MtG::R1, MtG::R2, MtG::R3, MtG::R4, MtG::R5, MtG::R6, MtG::R7};
  auto RegIdx = 0;
  switch (MI.getOpcode()) {
  case MtG::AND_MACRO:
  case MtG::OR_MACRO:
  case MtG::XOR_MACRO: {
    // 32-iteration bit decomposition loop. Each iteration extracts the
    // current bit from each source via SubCond+SetNF, combines the two
    // bits per the operation (AND = ba*bb, OR = ba+bb-ba*bb,
    // XOR = ba+bb-2*ba*bb), then adds bit*combined into the accumulator
    // and halves the bit for the next iteration. The loop exits when
    // bit = 0.
    //
    // SubCond rZ, rY: if rZ >= rY then rZ -= rY, FLAG=0; else FLAG=1.
    // So after `SubCond Anew, A, bit`, FLAG=0 iff "bit was set in A" and
    // SetNF gives us 1/0 accordingly.
    unsigned Op = MI.getOpcode();
    Register DstReg = MI.getOperand(0).getReg();
    Register Src1Reg = MI.getOperand(1).getReg();
    Register Src2Reg = MI.getOperand(2).getReg();

    MachineBasicBlock *EntryMBB = MBB;
    auto *LoopMBB = MF.CreateMachineBasicBlock(EntryMBB->getBasicBlock());
    auto *ExitMBB = MF.CreateMachineBasicBlock(EntryMBB->getBasicBlock());

    MF.insert(std::next(MachineFunction::iterator(EntryMBB)), LoopMBB);
    MF.insert(std::next(MachineFunction::iterator(LoopMBB)), ExitMBB);

    // Move the tail (everything after MI) into ExitMBB and forward the
    // original EntryMBB successors / PHIs to it.
    ExitMBB->splice(ExitMBB->end(), EntryMBB,
                    std::next(MachineBasicBlock::iterator(MI)),
                    EntryMBB->end());
    ExitMBB->transferSuccessorsAndUpdatePHIs(EntryMBB);

    EntryMBB->addSuccessor(LoopMBB);
    LoopMBB->addSuccessor(LoopMBB);
    LoopMBB->addSuccessor(ExitMBB);

    // ---- Initial values, computed in EntryMBB just before MI. ----
    Register BitInit = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register AInit = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register BInit = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register ResultInit = MRI.createVirtualRegister(&MtG::GRRegClass);

    BuildMI(*EntryMBB, MI, DL, TII.get(MtG::MOVEIMM_MACRO), BitInit)
        .addImm((int64_t)(1LL << 31));
    // Use TargetOpcode::COPY here so the eventual physreg move goes
    // through copyPhysReg (which elides same-reg cases). Emitting
    // MtG::MOVE directly would become "Move rN, rN" if coalescing
    // assigns source and destination to the same physreg, and MtG's
    // Move encoding forbids rY == rZ.
    BuildMI(*EntryMBB, MI, DL, TII.get(TargetOpcode::COPY), AInit)
        .addReg(Src1Reg);
    BuildMI(*EntryMBB, MI, DL, TII.get(TargetOpcode::COPY), BInit)
        .addReg(Src2Reg);
    BuildMI(*EntryMBB, MI, DL, TII.get(MtG::ZERO), ResultInit);

    // ---- Loop body vregs. ----
    Register BitPhi = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register APhi = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register BPhi = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register ResultPhi = MRI.createVirtualRegister(&MtG::GRRegClass);

    Register ANew = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register BNew = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Ba = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Bb = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register BaProd = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Contrib = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register ResultNew = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register BitNew = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Cond = MRI.createVirtualRegister(&MtG::GRRegClass);

    // PHIs at the head of LoopMBB.
    BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI), BitPhi)
        .addReg(BitInit).addMBB(EntryMBB)
        .addReg(BitNew).addMBB(LoopMBB);
    BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI), APhi)
        .addReg(AInit).addMBB(EntryMBB)
        .addReg(ANew).addMBB(LoopMBB);
    BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI), BPhi)
        .addReg(BInit).addMBB(EntryMBB)
        .addReg(BNew).addMBB(LoopMBB);
    BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI), ResultPhi)
        .addReg(ResultInit).addMBB(EntryMBB)
        .addReg(ResultNew).addMBB(LoopMBB);

    // Extract bit from A.
    BuildMI(LoopMBB, DL, TII.get(MtG::SUBCOND), ANew)
        .addUse(APhi).addUse(BitPhi);
    BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), Ba);
    // Extract bit from B.
    BuildMI(LoopMBB, DL, TII.get(MtG::SUBCOND), BNew)
        .addUse(BPhi).addUse(BitPhi);
    BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), Bb);
    // BaProd = bit-level operation on Ba and Bb (each is 0 or 1).
    //   AND : Ba * Bb                         (Mult)
    //   OR  : (Ba + Bb) > 0  →  FIsZero + SetNF on Ba+Bb
    //   XOR : (Ba + Bb) odd  →  Halve sets FLAG = oddness; SetF
    Register Combined;
    if (Op == MtG::AND_MACRO) {
      BuildMI(LoopMBB, DL, TII.get(MtG::MULT), BaProd).addUse(Ba).addUse(Bb);
      Combined = BaProd;
    } else {
      // ApB = Ba + Bb (∈ {0,1,2}).
      Register ApB = MRI.createVirtualRegister(&MtG::GRRegClass);
      BuildMI(LoopMBB, DL, TII.get(MtG::ADD), ApB).addUse(Ba).addUse(Bb);
      Combined = MRI.createVirtualRegister(&MtG::GRRegClass);
      if (Op == MtG::OR_MACRO) {
        BuildMI(LoopMBB, DL, TII.get(MtG::FISZERO)).addUse(ApB);
        BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), Combined);
      } else {
        // XOR: HALVE sets FLAG = (ApB odd) and produces an unused half.
        Register ApBHalved = MRI.createVirtualRegister(&MtG::GRRegClass);
        BuildMI(LoopMBB, DL, TII.get(MtG::HALVE), ApBHalved).addUse(ApB);
        BuildMI(LoopMBB, DL, TII.get(MtG::SETF), Combined);
      }
    }
    // Contrib = Combined * bit (= bit if Combined was 1, 0 otherwise).
    BuildMI(LoopMBB, DL, TII.get(MtG::MULT), Contrib)
        .addUse(Combined).addUse(BitPhi);
    // ResultNew = ResultPhi + Contrib.
    BuildMI(LoopMBB, DL, TII.get(MtG::ADD), ResultNew)
        .addUse(ResultPhi).addUse(Contrib);
    // BitNew = bit / 2.
    BuildMI(LoopMBB, DL, TII.get(MtG::HALVE), BitNew).addUse(BitPhi);
    // Cond = (BitNew != 0).
    BuildMI(LoopMBB, DL, TII.get(MtG::FISZERO)).addUse(BitNew);
    BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), Cond);
    // Branch back if Cond != 0 (i.e., still have bits to process).
    BuildMI(LoopMBB, DL, TII.get(MtG::BRCOND_PSEUDO))
        .addReg(Cond).addImm(0).addMBB(LoopMBB);

    // ExitMBB: dst = final result. Same reasoning as above — prefer COPY
    // over a direct MtG::MOVE so self-copies are elided by copyPhysReg.
    BuildMI(*ExitMBB, ExitMBB->begin(), DL, TII.get(TargetOpcode::COPY), DstReg)
        .addReg(ResultNew);

    MI.eraseFromParent();
    // Continue inserting subsequent ISel output into ExitMBB rather than the
    // (now-empty) EntryMBB.
    return ExitMBB;
  }
  case MtG::SELECT_MACRO: {
    // "$dst = SELECT_MACRO $cond, $tval, $fval"
    //  →  dst = (cond != 0) * tval + (cond == 0) * fval
    //
    // Implemented purely with arithmetic + flag ops: FIsZero sets FLAG from
    // $cond, SetNF materializes 1-if-nonzero into a scratch, SetF
    // materializes 1-if-zero into another, and the two products are summed.
    //
    // No block splitting / PHI is needed, and we don't go through R0, which
    // avoids the BranchFolder tail-merge hazards that stem from
    // "MOVE $dst, $r0" patterns.
    //
    // MULT and ADD have "$src1 = $dst" tied constraints; in SSA we still
    // feed distinct vregs as src1/dst and let TwoAddressInstructionPass
    // insert the necessary COPY later.
    Register DstReg = MI.getOperand(0).getReg();
    Register CondReg = MI.getOperand(1).getReg();
    Register TValReg = MI.getOperand(2).getReg();
    Register FValReg = MI.getOperand(3).getReg();

    Register CReg = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register NCReg = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register TProdReg = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register FProdReg = MRI.createVirtualRegister(&MtG::GRRegClass);

    // FLAG = (CondReg == 0)
    BuildMI(*MBB, MI, DL, TII.get(MtG::FISZERO)).addUse(CondReg);
    // CReg = !FLAG  (1 if CondReg != 0)
    BuildMI(*MBB, MI, DL, TII.get(MtG::SETNF), CReg);
    // NCReg = FLAG  (1 if CondReg == 0)
    BuildMI(*MBB, MI, DL, TII.get(MtG::SETF), NCReg);

    // TProdReg = CReg * TValReg
    BuildMI(*MBB, MI, DL, TII.get(MtG::MULT), TProdReg)
        .addUse(CReg)
        .addUse(TValReg);

    // FProdReg = NCReg * FValReg
    BuildMI(*MBB, MI, DL, TII.get(MtG::MULT), FProdReg)
        .addUse(NCReg)
        .addUse(FValReg);

    // DstReg = TProdReg + FProdReg
    BuildMI(*MBB, MI, DL, TII.get(MtG::ADD), DstReg)
        .addUse(TProdReg)
        .addUse(FProdReg);

    MI.eraseFromParent();
    break;
  }
  case MtG::LT_MACRO:
  case MtG::GT_MACRO:
  case MtG::LE_MACRO:
  case MtG::GE_MACRO: {
    // Signed comparisons: FLess is unsigned. Bias both operands by
    // (val + 0x80000000) mod 2^32, converting signed order to unsigned.
    // ADD produces the sum (may exceed 32 bits since MtG is arbitrary-
    // precision), then SubCond wraps it back: if sum >= 2^32 then sum -= 2^32.
    unsigned Op = MI.getOpcode();
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg1 = MI.getOperand(1).getReg();
    Register SrcReg2 = MI.getOperand(2).getReg();

    // Bias both operands: B = (src + 2^31) mod 2^32 = src XOR 2^31 (flip
    // bit 31). Signed compare on the two B values is equivalent to signed
    // compare on src1/src2 since FLess is unsigned.
    //
    // A straightforward "ADD by 2^31 then SubCond by 2^32 to wrap" would
    // need 2^32 in a vreg, and 2^32 does not fit in a 32-bit spill slot
    // (STOREBYTEWISE truncates to 4 bytes → reload of 0 → SubCond becomes
    // a silent no-op). We instead peel bit 31 off each source using
    // 2^31 only, and add 2^31 back only when bit 31 was originally 0.
    // Every intermediate fits in [0, 2^32-1], so any spill is safe.
    //
    //   Peeled = SubCond(src, 2^31)   // FLAG = 1 iff src < 2^31 (bit31 clear)
    //   NotBit31 = SetF               // 1 if bit31 was 0, else 0
    //   Delta = NotBit31 * 2^31       // 0 or 2^31
    //   B = Peeled + Delta            // = src XOR 2^31
    //
    // Trace:
    //   bit31 set  (src >= 2^31): Peeled=src-2^31, Delta=0.   B=src-2^31. ✓
    //   bit31 clear (src < 2^31): Peeled=src,      Delta=2^31. B=src+2^31. ✓
    //
    // ADD and MULT don't touch FLAG (verified against td and ursa), so
    // the SubCond → SetF pair stays correctly paired under scheduling —
    // SetF's Uses=[FLAG] forces it to follow the nearest preceding FLAG
    // definer.
    Register Bias = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Peeled1 = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register NotBit31_1 = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Delta1 = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register B1 = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Peeled2 = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register NotBit31_2 = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register Delta2 = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register B2 = MRI.createVirtualRegister(&MtG::GRRegClass);

    BuildMI(*MBB, MI, DL, TII.get(MtG::MOVEIMM_MACRO), Bias)
        .addImm((int64_t)0x80000000LL);
    // src1 → B1
    BuildMI(*MBB, MI, DL, TII.get(MtG::SUBCOND), Peeled1)
        .addUse(SrcReg1).addUse(Bias);
    BuildMI(*MBB, MI, DL, TII.get(MtG::SETF), NotBit31_1);
    BuildMI(*MBB, MI, DL, TII.get(MtG::MULT), Delta1)
        .addUse(NotBit31_1).addUse(Bias);
    BuildMI(*MBB, MI, DL, TII.get(MtG::ADD), B1)
        .addUse(Peeled1).addUse(Delta1);
    // src2 → B2
    BuildMI(*MBB, MI, DL, TII.get(MtG::SUBCOND), Peeled2)
        .addUse(SrcReg2).addUse(Bias);
    BuildMI(*MBB, MI, DL, TII.get(MtG::SETF), NotBit31_2);
    BuildMI(*MBB, MI, DL, TII.get(MtG::MULT), Delta2)
        .addUse(NotBit31_2).addUse(Bias);
    BuildMI(*MBB, MI, DL, TII.get(MtG::ADD), B2)
        .addUse(Peeled2).addUse(Delta2);

    if (Op == MtG::LT_MACRO) {
      BuildMI(*MBB, MI, DL, TII.get(MtG::FLESS))
          .addUse(B2).addUse(B1);
      BuildMI(*MBB, MI, DL, TII.get(MtG::SETF), DstReg);
    } else if (Op == MtG::GT_MACRO) {
      BuildMI(*MBB, MI, DL, TII.get(MtG::FLESS))
          .addUse(B1).addUse(B2);
      BuildMI(*MBB, MI, DL, TII.get(MtG::SETF), DstReg);
    } else if (Op == MtG::LE_MACRO) {
      BuildMI(*MBB, MI, DL, TII.get(MtG::FLESS))
          .addUse(B1).addUse(B2);
      BuildMI(*MBB, MI, DL, TII.get(MtG::SETNF), DstReg);
    } else { // GE_MACRO
      BuildMI(*MBB, MI, DL, TII.get(MtG::FLESS))
          .addUse(B2).addUse(B1);
      BuildMI(*MBB, MI, DL, TII.get(MtG::SETNF), DstReg);
    }
    MI.eraseFromParent();
    break;
  }
  // case MtG::AND_MACRO: {

  //   auto *LoopMBB = MF.CreateMachineBasicBlock(MBB->getBasicBlock());
  //   auto *ExitMBB = MF.CreateMachineBasicBlock(MBB->getBasicBlock());

  //   // 関数に挿入
  //   MF.insert(std::next(MachineFunction::iterator(MBB)), LoopMBB);
  //   MF.insert(std::next(MachineFunction::iterator(*LoopMBB)), ExitMBB);

  //   ExitMBB->splice(ExitMBB->begin(), MBB,
  //                   std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  //   ExitMBB->transferSuccessors(MBB);

  //   MBB->addSuccessor(LoopMBB);
  //   LoopMBB->addSuccessor(LoopMBB);
  //   MBB->addSuccessor(ExitMBB);
  //   Register Src1_0 = MRI.createVirtualRegister(&MtG::GRRegClass);
  //   Register Src1_1 = MRI.createVirtualRegister(&MtG::GRRegClass);
  //   Register Src1_2 = MRI.createVirtualRegister(&MtG::GRRegClass);
  //   Register Src2_0 = MRI.createVirtualRegister(&MtG::GRRegClass);
  //   Register Src2_1 = MRI.createVirtualRegister(&MtG::GRRegClass);
  //   Register Src2_2 = MRI.createVirtualRegister(&MtG::GRRegClass);

  //   const auto DstReg = MI.getOperand(0).getReg();
  //   const auto Src1Reg = MI.getOperand(1).getReg();
  //   const auto Src2Reg = MI.getOperand(2).getReg();

  //   BuildMI(MBB, DL, TII.get(MtG::MOVE), MtG::R3).addUse(Src1Reg);
  //   BuildMI(MBB, DL, TII.get(MtG::ZERO), MtG::R4);

  //   BuildMI(MBB, DL, TII.get(MtG::NUMBUILD_MACRO)).addImm(1ULL << 31);
  //   BuildMI(MBB, DL, TII.get(MtG::MOVE), MtG::R7).addUse(MtG::R0);
  //   BuildMI(MBB, DL, TII.get(MtG::MOVE), Src1_0).addUse(MtG::R3);
  //   BuildMI(MBB, DL, TII.get(MtG::MOVE), Src2_0).addUse(Src2Reg);

  //   BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI),
  //   Src1_1)
  //       .addReg(Src1_0)
  //       .addMBB(MBB)
  //       .addReg(Src1_2)
  //       .addMBB(LoopMBB);
  //   BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI),
  //   Src2_1)
  //       .addReg(Src2_0)
  //       .addMBB(MBB)
  //       .addReg(Src2_2)
  //       .addMBB(LoopMBB);

  //   BuildMI(LoopMBB, DL, TII.get(MtG::SUBCOND), Src1_2)
  //       .addUse(Src1_1)
  //       .addUse(MtG::R7);
  //   BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), MtG::R5);

  //   BuildMI(LoopMBB, DL, TII.get(MtG::SUBCOND), Src2_2)
  //       .addUse(Src2_1)
  //       .addUse(MtG::R7);
  //   BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), MtG::R6);

  //   BuildMI(LoopMBB, DL, TII.get(MtG::MULT), MtG::R5)
  //       .addUse(MtG::R5)
  //       .addUse(MtG::R6);

  //   BuildMI(LoopMBB, DL, TII.get(MtG::FISZERO)).addUse(MtG::R5);
  //   BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), MtG::R5);

  //   BuildMI(LoopMBB, DL, TII.get(MtG::ADD), MtG::R4)
  //       .addUse(MtG::R4)
  //       .addUse(MtG::R4);
  //   BuildMI(LoopMBB, DL, TII.get(MtG::ADD), MtG::R4)
  //       .addUse(MtG::R4)
  //       .addUse(MtG::R5);
  //   BuildMI(LoopMBB, DL, TII.get(MtG::HALVE), MtG::R7).addUse(MtG::R7);
  //   BuildMI(MBB, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
  //   BuildMI(MBB, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
  //   BuildMI(MBB, DL, TII.get(MtG::FISZERO)).addUse(MtG::R7);
  //   BuildMI(LoopMBB, DL, TII.get(MtG::JUMPBWDNF)).addMBB(LoopMBB);

  //   BuildMI(MBB, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
  //   BuildMI(MBB, DL, TII.get(MtG::NUMBUILD)).addImm(0).addImm(0);
  //   BuildMI(LoopMBB, DL, TII.get(MtG::JUMPFWD)).addMBB(ExitMBB);

  //   Register Src2_3 = MRI.createVirtualRegister(&MtG::GRRegClass);
  //   Register Dst_0 = MRI.createVirtualRegister(&MtG::GRRegClass);

  //   auto IP = ExitMBB->getFirstNonPHI();
  //   BuildMI(*ExitMBB, IP, DL, TII.get(MtG::MOVE), DstReg)
  //       .addUse(MtG::R4, RegState::Kill);
  //   MI.eraseFromParent();
  //   return ExitMBB;
  // }
  case MtG::SHL_VAR_MACRO:
  case MtG::SHR_VAR_MACRO:
  case MtG::ASHR_VAR_MACRO: {
    unsigned Op = MI.getOpcode();
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(1).getReg();
    Register AmtReg = MI.getOperand(2).getReg();

    MachineBasicBlock *EntryMBB = MBB;
    auto *LoopMBB = MF.CreateMachineBasicBlock(EntryMBB->getBasicBlock());
    auto *ExitMBB = MF.CreateMachineBasicBlock(EntryMBB->getBasicBlock());

    MF.insert(std::next(MachineFunction::iterator(EntryMBB)), LoopMBB);
    MF.insert(std::next(MachineFunction::iterator(LoopMBB)), ExitMBB);

    ExitMBB->splice(ExitMBB->end(), EntryMBB,
                    std::next(MachineBasicBlock::iterator(MI)),
                    EntryMBB->end());
    ExitMBB->transferSuccessorsAndUpdatePHIs(EntryMBB);

    EntryMBB->addSuccessor(LoopMBB);
    EntryMBB->addSuccessor(ExitMBB);
    LoopMBB->addSuccessor(LoopMBB);
    LoopMBB->addSuccessor(ExitMBB);

    // ---- Entry block: set up initial values and skip if amt == 0. ----
    Register ResultInit = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register CounterInit = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register SkipCond = MRI.createVirtualRegister(&MtG::GRRegClass);

    BuildMI(*EntryMBB, MI, DL, TII.get(TargetOpcode::COPY), ResultInit)
        .addReg(SrcReg);
    BuildMI(*EntryMBB, MI, DL, TII.get(TargetOpcode::COPY), CounterInit)
        .addReg(AmtReg);

    // For ASHR: compute sign mask before the loop.
    // SignMask = (src >= 0x80000000) ? 0x80000000 : 0
    Register SignMask;
    if (Op == MtG::ASHR_VAR_MACRO) {
      Register Threshold = MRI.createVirtualRegister(&MtG::GRRegClass);
      Register Sign = MRI.createVirtualRegister(&MtG::GRRegClass);
      Register SMVal = MRI.createVirtualRegister(&MtG::GRRegClass);
      SignMask = MRI.createVirtualRegister(&MtG::GRRegClass);

      BuildMI(*EntryMBB, MI, DL, TII.get(MtG::MOVEIMM_MACRO), Threshold)
          .addImm(0x7FFFFFFFL);
      BuildMI(*EntryMBB, MI, DL, TII.get(MtG::FLESS))
          .addUse(ResultInit)
          .addUse(Threshold);
      BuildMI(*EntryMBB, MI, DL, TII.get(MtG::SETF), Sign);
      BuildMI(*EntryMBB, MI, DL, TII.get(MtG::MOVEIMM_MACRO), SMVal)
          .addImm((int64_t)0x80000000LL);
      BuildMI(*EntryMBB, MI, DL, TII.get(MtG::MULT), SignMask)
          .addUse(Sign)
          .addUse(SMVal);
    }

    // For SHL: we previously materialized a 2^32 wrap mask and did
    // `ResultNew = SubCond(Doubled, WrapMask)`. But 2^32 does not fit in
    // a 32-bit spill slot (STOREBYTEWISE truncates to 4 bytes), so if RA
    // ever spills WrapMask across a BB it reloads as 0 and the SubCond
    // becomes a silent no-op. We can't keep WrapMask in R0 either because
    // BRCOND_PSEUDO at the loop tail clobbers R0 every iteration.
    // Instead we use a 2^31 threshold (spill-safe) and the identity
    //   (val + val) mod 2^32  ==  2 * (val - 2^31 if bit31 else val)
    // i.e. peel bit 31 off before doubling. See SHL_VAR case below.
    Register HalfMask;
    if (Op == MtG::SHL_VAR_MACRO) {
      HalfMask = MRI.createVirtualRegister(&MtG::GRRegClass);
      BuildMI(*EntryMBB, MI, DL, TII.get(MtG::MOVEIMM_MACRO), HalfMask)
          .addImm((int64_t)0x80000000LL);
    }

    BuildMI(*EntryMBB, MI, DL, TII.get(MtG::FISZERO)).addUse(CounterInit);
    BuildMI(*EntryMBB, MI, DL, TII.get(MtG::SETF), SkipCond);
    BuildMI(*EntryMBB, MI, DL, TII.get(MtG::BRCOND_PSEUDO))
        .addReg(SkipCond)
        .addImm(0)
        .addMBB(ExitMBB);

    // ---- Loop block: shift by 1, decrement counter, loop back. ----
    Register ResultPhi = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register CounterPhi = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register ResultNew = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register CounterNew = MRI.createVirtualRegister(&MtG::GRRegClass);
    Register LoopCond = MRI.createVirtualRegister(&MtG::GRRegClass);

    BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI),
            CounterPhi)
        .addReg(CounterInit)
        .addMBB(EntryMBB)
        .addReg(CounterNew)
        .addMBB(LoopMBB);
    BuildMI(*LoopMBB, LoopMBB->begin(), DL, TII.get(TargetOpcode::PHI),
            ResultPhi)
        .addReg(ResultInit)
        .addMBB(EntryMBB)
        .addReg(ResultNew)
        .addMBB(LoopMBB);

    if (Op == MtG::SHL_VAR_MACRO) {
      // result = (val + val) mod 2^32 without ever forming 2^32 in a vreg.
      // Peel bit 31 off first: Peeled = val - (bit31 ? 2^31 : 0). Since
      // SubCond(val, 2^31) subtracts 2^31 exactly when val >= 2^31, this
      // gives Peeled ∈ [0, 2^31-1]. Then 2 * Peeled ∈ [0, 2^32-2] and
      // equals (val + val) mod 2^32:
      //   * bit31 == 0:  2*Peeled = 2*val            (no wrap needed)
      //   * bit31 == 1:  2*Peeled = 2*(val - 2^31)   = 2*val mod 2^32
      Register Peeled = MRI.createVirtualRegister(&MtG::GRRegClass);
      BuildMI(LoopMBB, DL, TII.get(MtG::SUBCOND), Peeled)
          .addUse(ResultPhi)
          .addUse(HalfMask);
      BuildMI(LoopMBB, DL, TII.get(MtG::ADD), ResultNew)
          .addUse(Peeled)
          .addUse(Peeled);
    } else if (Op == MtG::SHR_VAR_MACRO) {
      BuildMI(LoopMBB, DL, TII.get(MtG::HALVE), ResultNew)
          .addUse(ResultPhi);
    } else {
      // ASHR: halve then restore sign bit.
      Register Halved = MRI.createVirtualRegister(&MtG::GRRegClass);
      BuildMI(LoopMBB, DL, TII.get(MtG::HALVE), Halved).addUse(ResultPhi);
      BuildMI(LoopMBB, DL, TII.get(MtG::ADD), ResultNew)
          .addUse(Halved)
          .addUse(SignMask);
    }

    // Decrement counter.
    BuildMI(LoopMBB, DL, TII.get(MtG::SUB1COND), CounterNew)
        .addUse(CounterPhi);
    // Loop back if counter is still > 0.
    BuildMI(LoopMBB, DL, TII.get(MtG::FISZERO)).addUse(CounterNew);
    BuildMI(LoopMBB, DL, TII.get(MtG::SETNF), LoopCond);
    BuildMI(LoopMBB, DL, TII.get(MtG::BRCOND_PSEUDO))
        .addReg(LoopCond)
        .addImm(0)
        .addMBB(LoopMBB);

    // ---- Exit block: PHI selects between skipped (original) and shifted. ----
    Register ExitPhi = MRI.createVirtualRegister(&MtG::GRRegClass);
    BuildMI(*ExitMBB, ExitMBB->begin(), DL, TII.get(TargetOpcode::PHI),
            ExitPhi)
        .addReg(ResultInit)
        .addMBB(EntryMBB)
        .addReg(ResultNew)
        .addMBB(LoopMBB);
    BuildMI(*ExitMBB, std::next(ExitMBB->begin()), DL,
            TII.get(TargetOpcode::COPY), DstReg)
        .addReg(ExitPhi);

    MI.eraseFromParent();
    return ExitMBB;
  }
  default:
    break;
  }
  return BB;
}

SDValue MtGTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  int64_t Offset = cast<GlobalAddressSDNode>(Op)->getOffset();
  // MtG uses 32-bit pointers (see MtGTargetMachine::computeDataLayout).
  // Materialise the global's address into a register via MOVEADDR_MACRO;
  // the post-RA expansion lowers it to "NumBuildAddr <sym>; Move $dst, r0",
  // and ursa fills in the actual base-144 digits at assemble time once it
  // knows where the symbol lives in memory.
  //
  // Clang emits non-zero Offsets when a field/element of a global is
  // addressed (e.g. `&global_struct.field` becomes `&global_struct + 4`).
  // NumBuildAddr is a symbol-only resolver, so fold the offset in by
  // materialising the base address first and adding the constant on top.
  SDLoc DL(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue TGA = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0);
  SDValue Base =
      SDValue(DAG.getMachineNode(MtG::MOVEADDR_MACRO, DL, PtrVT, TGA), 0);
  if (Offset == 0)
    return Base;
  // Truncate to PtrVT bits. For a negative Offset (e.g. the compiler
  // materialising `&global - 1` as a loop sentinel), the uint64_t
  // overload of getConstant would hit APInt's isUIntN assertion at
  // 32-bit PtrVT. Using the APInt overload with an explicit trunc
  // keeps the two's-complement bit pattern MtG's address arithmetic
  // expects.
  APInt OffsetAPI =
      APInt(64, static_cast<uint64_t>(Offset)).trunc(PtrVT.getSizeInBits());
  return DAG.getNode(ISD::ADD, DL, PtrVT, Base,
                     DAG.getConstant(OffsetAPI, DL, PtrVT));
}

SDValue MtGTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MtGMachineFunctionInfo *FuncInfo = MF.getInfo<MtGMachineFunctionInfo>();

  SDLoc DL(Op);
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                 getPointerTy(DAG.getDataLayout()));

  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FI, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue MtGTargetLowering::LowerVAARG(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  EVT VT = Node->getValueType(0);
  SDValue Chain = Node->getOperand(0);
  SDValue VAListPtr = Node->getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
  SDLoc DL(Node);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue VAList =
      DAG.getLoad(PtrVT, DL, Chain, VAListPtr, MachinePointerInfo(SV));
  Chain = VAList.getValue(1);

  SDValue Result = DAG.getLoad(VT, DL, Chain, VAList, MachinePointerInfo());
  Chain = Result.getValue(1);

  unsigned ArgSize = VT.getStoreSize();
  if (ArgSize < 4)
    ArgSize = 4;
  SDValue NextPtr =
      DAG.getNode(ISD::ADD, DL, PtrVT, VAList,
                  DAG.getIntPtrConstant(ArgSize, DL));
  Chain = DAG.getStore(Chain, DL, NextPtr, VAListPtr, MachinePointerInfo(SV));

  return DAG.getMergeValues({Result, Chain}, DL);
}

bool MtGTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                              const AddrMode &AM, Type *Ty,
                                              unsigned AS,
                                              Instruction *CtxI) const {
  if (AS != 0)
    return false;
  if (!AM.HasBaseReg)
    return false;
  if (AM.BaseOffs != 0)
    return false;
  if (AM.BaseGV != nullptr)
    return false;
  if (AM.Scale != 0)
    return false;
  if (AM.ScalableOffset != 0)
    return false;
  return true;
}

