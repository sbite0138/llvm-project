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
#include "MtG.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGSubtarget.h"
#include "MtGTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
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
using namespace llvm;

#define DEBUG_TYPE "mtg-lower"

static cl::opt<bool> MtGNoLegalImmediate(
    "mtg-no-legal-immediate", cl::Hidden,
    cl::desc("Enable non legal immediates (for testing purposes only)"),
    cl::init(false));

MtGTargetLowering::MtGTargetLowering(const TargetMachine &TM,
                                     const MtGSubtarget &STI)
    : TargetLowering(TM) {

  // Set up the register classes.
  addRegisterClass(MVT::i64, &MtG::GRRegClass);

  // Compute derived properties from the register classes
  computeRegisterProperties(STI.getRegisterInfo());

  // Provide all sorts of operation actions
  setStackPointerRegisterToSaveRestore(MtG::R11);
}

SDValue MtGTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("unimplemented operand");
  }
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "MtGGenCallingConv.inc"

/// For each argument in a function store the number of pieces it is composed
/// of.
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
  State.AnalyzeCallOperands(Outs, CC_MtG_AssignStack);
}

static void AnalyzeVarArgs(CCState &State,
                           const SmallVectorImpl<ISD::InputArg> &Ins) {
  State.AnalyzeFormalArguments(Ins, CC_MtG_AssignStack);
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

bool MtGTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_MtG);
}

SDValue
MtGTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &dl, SelectionDAG &DAG) const {

  MachineFunction &MF = DAG.getMachineFunction();

  // CCValAssign - represent the assignment of the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;

  // ISRs cannot return any value.
  if (CallConv == CallingConv::MtG_INTR && !Outs.empty())
    report_fatal_error("ISRs cannot return any value");

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analize return values.
  AnalyzeReturnValues(CCInfo, RVLocs, Outs);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[i], Glue);

    // Guarantee that all emitted copies are stuck together,
    // avoiding something bad.
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  if (MF.getFunction().hasStructRetAttr()) {
    MtGMachineFunctionInfo *FuncInfo = MF.getInfo<MtGMachineFunctionInfo>();
    Register Reg = FuncInfo->getSRetReturnReg();

    if (!Reg)
      llvm_unreachable("sret virtual register not created in entry block");

    MVT PtrVT = getFrameIndexTy(DAG.getDataLayout());
    SDValue Val = DAG.getCopyFromReg(Chain, dl, Reg, PtrVT);
    unsigned R12 = MtG::R12;

    Chain = DAG.getCopyToReg(Chain, dl, R12, Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(R12, PtrVT));
  }

  unsigned Opc = (CallConv == CallingConv::MtG_INTR ? MtGISD::RETI_GLUE
                                                    : MtGISD::RET_GLUE);

  RetOps[0] = Chain; // Update chain.

  // Add the glue if we have it.
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(Opc, dl, MVT::Other, RetOps);
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
