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
#include "MtGSubtarget.h"
#include "MtGTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
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
#include <cassert>
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
  addRegisterClass(MVT::i32, &MtG::SRRegClass);
  addRegisterClass(MVT::i32, &MtG::GRRegClass);
  // addRegisterClass(MVT::i32, &MtG::WRRegClass);  addRegisterClass(MVT::i16,
  // &MtG::FRRegClass);

  // Compute derived properties from the register classes
  computeRegisterProperties(STI.getRegisterInfo());

  // Provide all sorts of operation actions
  setStackPointerRegisterToSaveRestore(MtG::R8);
  setOperationAction(ISD::SDIV, MVT::i32, Custom);
  setOperationAction(ISD::BR_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT, MVT::i32, Legal);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
}
EVT MtGTargetLowering::getSetCCResultType(const DataLayout &DL,
                                          LLVMContext &Ctx, EVT VT) const {
  return MVT::i32;
}

bool MtGTargetLowering::isIntDivCheap(EVT VT, AttributeList Attr) const {
  return true;
}

SDValue MtGTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("unimplemented operand");
  case ISD::SELECT:
    return LowerSELECT(Op, DAG);
  }
}

SDValue MtGTargetLowering::LowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Cond = Op.getOperand(0);
  SDValue TrueVal = Op.getOperand(1);
  SDValue FalseVal = Op.getOperand(2);
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

  assert(isVarArg == false && "VarArg not supported yet");

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MtGMachineFunctionInfo *MtGMFI = MF.getInfo<MtGMachineFunctionInfo>();

  MtGMFI->setVarArgsFrameIndex(0);

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_MtG_AssignStack);

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

MachineBasicBlock *
MtGTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unimplemented custom inserter");
  case MtG::BR_PSEUDO: {
    MI.dump();
    return BB;
  }
  case MtG::LSB_PSEUDO: {
    DebugLoc DL = MI.getDebugLoc();
    MachineFunction &MF = *BB->getParent();
    const MtGSubtarget &STI = MF.getSubtarget<MtGSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(1).getReg();
    Register TmpReg = MF.getRegInfo().createVirtualRegister(&MtG::GRRegClass);
    BuildMI(*BB, MI, DL, TII.get(MtG::HALVE), TmpReg).addUse(SrcReg);
    BuildMI(*BB, MI, DL, TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
    return BB;
  }

  case MtG::SLT_PSEUDO: {
    DebugLoc DL = MI.getDebugLoc();
    MachineFunction &MF = *BB->getParent();
    const MtGSubtarget &STI = MF.getSubtarget<MtGSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    unsigned DstReg = MI.getOperand(0).getReg();
    unsigned SrcReg1 = MI.getOperand(1).getReg();
    unsigned SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(*BB, MI, DL, TII.get(MtG::FLESS)).addUse(SrcReg1).addUse(SrcReg2);
    BuildMI(*BB, MI, DL, TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
    return BB;
  }
  case MtG::SGT_PSEUDO: {
    DebugLoc DL = MI.getDebugLoc();
    MachineFunction &MF = *BB->getParent();
    const MtGSubtarget &STI = MF.getSubtarget<MtGSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    unsigned DstReg = MI.getOperand(0).getReg();
    unsigned SrcReg1 = MI.getOperand(1).getReg();
    unsigned SrcReg2 = MI.getOperand(2).getReg();
    auto MIB = BuildMI(*BB, MI, DL, TII.get(MtG::SLT_PSEUDO), DstReg)
                   .addUse(SrcReg2)
                   .addUse(SrcReg1);
    EmitInstrWithCustomInserter(*MIB.getInstr(), BB);
    MI.eraseFromParent();
    return BB;
  }
  case MtG::SNEQ_PSEUDO: {
    DebugLoc DL = MI.getDebugLoc();
    MachineFunction &MF = *BB->getParent();
    const MtGSubtarget &STI = MF.getSubtarget<MtGSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg1 = MI.getOperand(1).getReg();
    Register SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(*BB, MI, DL, TII.get(MtG::FLESS)).addUse(SrcReg1).addUse(SrcReg2);
    BuildMI(*BB, MI, DL, TII.get(MtG::FLESS)).addUse(SrcReg2).addUse(SrcReg1);
    BuildMI(*BB, MI, DL, TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
    return BB;
  }
  case MtG::SEQ_PSEUDO: {
    DebugLoc DL = MI.getDebugLoc();
    MachineFunction &MF = *BB->getParent();
    const MtGSubtarget &STI = MF.getSubtarget<MtGSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg1 = MI.getOperand(1).getReg();
    Register SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(*BB, MI, DL, TII.get(MtG::FLESS)).addUse(SrcReg1).addUse(SrcReg2);
    BuildMI(*BB, MI, DL, TII.get(MtG::FLESS)).addUse(SrcReg2).addUse(SrcReg1);
    BuildMI(*BB, MI, DL, TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
    return BB;
  }
  case MtG::SGEQ_PSEUDO: {
    DebugLoc DL = MI.getDebugLoc();
    MachineFunction &MF = *BB->getParent();
    const MtGSubtarget &STI = MF.getSubtarget<MtGSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    unsigned DstReg = MI.getOperand(0).getReg();
    unsigned SrcReg1 = MI.getOperand(1).getReg();
    unsigned SrcReg2 = MI.getOperand(2).getReg();
    Register TmpReg1 = MF.getRegInfo().createVirtualRegister(&MtG::GRRegClass);
    auto MIB = BuildMI(*BB, MI, DL, TII.get(MtG::SLT_PSEUDO), TmpReg1)
                   .addUse(SrcReg1)
                   .addUse(SrcReg2);
    EmitInstrWithCustomInserter(*MIB.getInstr(), BB);
    BuildMI(*BB, MI, DL, TII.get(MtG::FISZERO)).addUse(TmpReg1);
    BuildMI(*BB, MI, DL, TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
    return BB;
  }
  case MtG::SLEQ_PSEUDO: {
    DebugLoc DL = MI.getDebugLoc();
    MachineFunction &MF = *BB->getParent();
    const MtGSubtarget &STI = MF.getSubtarget<MtGSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    unsigned DstReg = MI.getOperand(0).getReg();
    unsigned SrcReg1 = MI.getOperand(1).getReg();
    unsigned SrcReg2 = MI.getOperand(2).getReg();
    auto MIB = BuildMI(*BB, MI, DL, TII.get(MtG::SGEQ_PSEUDO), DstReg)
                   .addUse(SrcReg1)
                   .addUse(SrcReg2);
    EmitInstrWithCustomInserter(*MIB.getInstr(), BB);
    MI.eraseFromParent();
    return BB;
  }
  }
  return BB;
}

// SDValue MtGTargetLowering::LowerGlobalAddress(SDValue Op,
//                                               SelectionDAG &DAG) const {
//   SDLoc DL(Op);
//   EVT Ty
// }
