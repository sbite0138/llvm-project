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
  setOperationAction(ISD::BR_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT, MVT::i32, Legal);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
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
    SDValue Address =
        DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr,
                    DAG.getIntPtrConstant(VA.getLocMemOffset(), dl));

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

  for (unsigned i = 0; i != Outs.size(); ++i) {
    auto VT = Outs[i].VT;
    if (VT != MVT::i32) {
      // print VT type
      errs() << "VT: " << VT.getScalarType() << '\n';
    }
  }
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
    dbgs() << "[!!!!] MtGTargetLowering::LowerReturn: "
           << "CallConv: " << CallConv << ", isVarArg: " << isVarArg
           << ", Chain: " << Chain.getNode() << ", Flag: " << Flag.getNode()
           << "\n";

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Val, Flag);

    // Guarantee that all emitted copies are stuck together,
    // avoiding something bad.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }
  dbgs() << "MtGTargetLowering::LowerReturn: "
         << "CallConv: " << CallConv << ", isVarArg: " << isVarArg
         << ", Chain: " << Chain.getNode() << ", Flag: " << Flag.getNode()
         << "\n";
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

  assert(isVarArg == false && "VarArg not supported yet");

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MtGMachineFunctionInfo *MtGMFI = MF.getInfo<MtGMachineFunctionInfo>();

  MtGMFI->setVarArgsFrameIndex(0);

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

  // if (isVarArg)
  //   writeVarArgRegs(OutChains, Chain, DL, DAG, CCInfo);
  // @} MYRISCVXISelLowering_LowerFormalArguments_IsVarArg

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
    BuildMI(*EntryMBB, MI, DL, TII.get(MtG::MOVE), AInit).addUse(Src1Reg);
    BuildMI(*EntryMBB, MI, DL, TII.get(MtG::MOVE), BInit).addUse(Src2Reg);
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

    // ExitMBB: dst = final result.
    BuildMI(*ExitMBB, ExitMBB->begin(), DL, TII.get(MtG::MOVE), DstReg)
        .addUse(ResultNew);

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
  default:
    break;
  }
  return BB;
}

SDValue MtGTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  llvm::dbgs() << "LowerGlobalAddress\n";
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();

  int64_t Offset = cast<GlobalAddressSDNode>(Op)->getOffset();
  assert(Offset == 0 && "Offset must be zero for MtG");

  SDValue Addr = DAG.getTargetGlobalAddress(GV, SDLoc(Op), MVT::iPTR, 0);

  // Create the TargetGlobalAddress node, folding in the constant offset.
  // SDValue Result = DAG.getTargetGlobalAddress(GV, SDLoc(Op), PtrVT,
  // Offset);
  return DAG.getNode(MtGISD::WRAP_ADDR, SDLoc(Op), MVT::iPTR, Addr, Addr);
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
