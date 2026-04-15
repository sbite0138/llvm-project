//===-- MtGRegisterInfo.cpp - MtG Register Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MtG implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "MtGRegisterInfo.h"
#include "MCTargetDesc/MtGMCTargetDesc.h"
#include "MtG.h"
#include "MtGFrameLowering.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mtg-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "MtGGenRegisterInfo.inc"

// FIXME: Provide proper call frame setup / destroy opcodes.
MtGRegisterInfo::MtGRegisterInfo() : MtGGenRegisterInfo(0) {}

// Emit a byte-wise save of $VictimReg into the emergency-spill slot at *SP
// (the 4 extra bytes reserved by MtGFrameLowering::emitPrologue). The
// expansion deliberately uses no scratch register beyond what the MtG ISA
// already implicitly owns:
//   * R0 holds the constant 256 used by Divide.
//   * R6 receives Divide's quotient (the upper bytes for the next iteration).
//   * R2 (SP) is the address iterator — it is mutated and restored.
//   * VictimReg itself is destroyed during byte extraction, but its value is
//     persisted to memory so destroying it is fine.
void MtGRegisterInfo::emitEmergencySave(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator II,
                                        const TargetInstrInfo &TII,
                                        Register VictimReg) {
  DebugLoc DL = II != MBB.end() ? II->getDebugLoc() : DebugLoc();
  // R0 = 256
  BuildMI(MBB, II, DL, TII.get(MtG::NUMBUILD_MACRO)).addImm(256);
  for (int i = 0; i < 4; ++i) {
    // VictimReg = VictimReg % 256 (low byte); R6 = VictimReg / 256 (upper).
    BuildMI(MBB, II, DL, TII.get(MtG::DIVIDE), VictimReg).addUse(VictimReg);
    // *SP = low byte. Store is spec-ordered (Y=value, Z=address): first
    // operand is the byte to write, second is where to write it.
    BuildMI(MBB, II, DL, TII.get(MtG::STORE)).addUse(VictimReg).addUse(MtG::R2);
    if (i < 3) {
      // VictimReg <- upper bytes for the next iteration.
      BuildMI(MBB, II, DL, TII.get(MtG::MOVE), VictimReg).addUse(MtG::R6);
      // SP++ : address advances to the next byte slot.
      BuildMI(MBB, II, DL, TII.get(MtG::ADD1), MtG::R2).addUse(MtG::R2);
    }
  }
  // Restore SP (we incremented it 3 times).
  for (int i = 0; i < 3; ++i)
    BuildMI(MBB, II, DL, TII.get(MtG::SUB1COND), MtG::R2).addUse(MtG::R2);
}

// Emit a byte-wise reload of $VictimReg from the emergency-spill slot at
// *SP. We process the high byte first so the accumulator can be shifted up
// by 256 (via Mult) before each lower byte is added in.
//   * R0 holds the constant 256.
//   * R6 acts as the per-iteration byte temp (no Divide runs in this loop,
//     so R6 is stable). It is allowed because R6 is reserved and never
//     carries a user value.
//   * R2 (SP) is the address iterator — destroyed and restored, ending at
//     its original position.
//   * VictimReg is the destination of the reload.
void MtGRegisterInfo::emitEmergencyReload(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator II,
                                          const TargetInstrInfo &TII,
                                          Register VictimReg) {
  DebugLoc DL = II != MBB.end() ? II->getDebugLoc() : DebugLoc();
  // SP += 3 : start at the high-byte slot (SP+3).
  for (int i = 0; i < 3; ++i)
    BuildMI(MBB, II, DL, TII.get(MtG::ADD1), MtG::R2).addUse(MtG::R2);
  // R0 = 256
  BuildMI(MBB, II, DL, TII.get(MtG::NUMBUILD_MACRO)).addImm(256);
  // VictimReg = byte 3
  BuildMI(MBB, II, DL, TII.get(MtG::LOAD), VictimReg).addUse(MtG::R2);
  for (int i = 0; i < 3; ++i) {
    // SP -= 1 : move to the next-lower byte (i + bytes left).
    BuildMI(MBB, II, DL, TII.get(MtG::SUB1COND), MtG::R2).addUse(MtG::R2);
    // VictimReg <<= 8
    BuildMI(MBB, II, DL, TII.get(MtG::MULT), VictimReg)
        .addUse(VictimReg)
        .addUse(MtG::R0);
    // R6 = next byte
    BuildMI(MBB, II, DL, TII.get(MtG::LOAD), MtG::R6).addUse(MtG::R2);
    // VictimReg += byte
    BuildMI(MBB, II, DL, TII.get(MtG::ADD), VictimReg)
        .addUse(VictimReg)
        .addUse(MtG::R6);
  }
  // SP is now back at its original position (3 ADD1's matched by 3 SUB1COND's).
}

const MCPhysReg *
MtGRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  // R0/R2/R6/FLAG are reserved (handled separately). R1 is now a regular
  // allocatable register — it used to be the (uninitialized) frame
  // register but the backend now uses R2 directly. None of the
  // allocatable registers are callee-saved by convention; functions
  // simply spill what they need to temporary slots themselves.
  static const MCPhysReg CalleeSavedRegs[] = {0};
  return CalleeSavedRegs;
}

BitVector MtGRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  // FLAG: condition-flag register (read by SetF/SetNF and all branches).
  // R0: universal scratch — NumBuild's implicit destination and the operand
  //     to Mult/Divide in every macro that materializes a constant.
  // R2: stack pointer (SP). MtG also uses R2 directly as the frame base
  //     (we no longer maintain a separate frame pointer in R1; see
  //     getFrameRegister below). The emergency-spill slot lives at *R2.
  // R6: Divide's quotient destination. Keeping it reserved means it never
  //     holds a user virtual register, so byte-wise Load/Store expansions
  //     may use it as a scratch between Divides (when it happens to be
  //     free from the hardware's POV).
  Reserved.set(MtG::FLAG);
  Reserved.set(MtG::R0);
  Reserved.set(MtG::R2);
  Reserved.set(MtG::R6);

  return Reserved;
}

bool MtGRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc DL = MI.getDebugLoc();
  MachineBasicBlock &MBB = *MI.getParent();
  auto *TII = MF.getSubtarget().getInstrInfo();
  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  // Frame layout and the MtG-specific offset formula.
  //
  // Non-fixed frame objects (locals, spill slots) are LOWER in the frame
  // than the emergency slot, so their SP-relative address is
  //
  //     SP + objectOffset + stackSize + SPAdj + emergency
  //
  // where `emergency` compensates for the 4 bytes the prologue carves out
  // at *SP for the scavenger.
  //
  // Fixed frame objects (incoming stack arguments) live ABOVE the frame,
  // in the *caller's* outgoing-arg region. Per MtGISelLowering::LowerCall
  // the caller writes them at `caller_SP + emergency + locMemOffset`, i.e.
  // just above the caller's own emergency slot. From the callee's SP:
  //
  //   * If the callee has a prologue (stackSize != 0 || adjustsStack),
  //     SP has been lowered by stackSize+emergency and the arg is at
  //       callee_SP + stackSize + emergency (top of callee frame)
  //                + emergency               (skip caller's emergency)
  //                + locMemOffset
  //     which is the plain formula plus one extra `emergency`.
  //
  //   * If the callee has no prologue (leaf with stackSize=0), callee_SP
  //     equals caller_SP and the single `emergency` in the plain formula
  //     already accounts for the caller's emergency slot — no extra term
  //     is needed.
  const bool HasPrologue = MFI.getStackSize() != 0 || MFI.adjustsStack();
  auto computeFIOffset = [&](int FI, int64_t ExtraOffset) -> int64_t {
    int64_t Off = MFI.getObjectOffset(FI) + MFI.getStackSize() + SPAdj +
                  MtGFrameLowering::kEmergencySlotSize + ExtraOffset;
    if (MFI.isFixedObjectIndex(FI) && HasPrologue)
      Off += MtGFrameLowering::kEmergencySlotSize;
    return Off;
  };

  if (MI.getOpcode() == MtG::ADD_MACRO_FI) {
    const auto DstReg = MI.getOperand(0).getReg();
    const auto FrameIndex = MI.getOperand(1).getIndex();
    const auto Offset = MI.getOperand(2).getImm();
    const int64_t totalOffset = computeFIOffset(FrameIndex, Offset);

    if (DstReg != getFrameRegister(MF))
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MOVE), DstReg)
                         .addUse(getFrameRegister(MF)));

    MBB.insert(
        II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO)).addImm(totalOffset));

    if (!MI.isDebugValue() && !isInt<16>(Offset)) {
      assert("(!MI.isDebugValue() && !isInt<16>(Offset))");
    }
    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::ADD_MACRO), DstReg)
                       .addReg(DstReg)
                       .addReg(MtG::R0));
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::STOREBYTEWISE_FI_MACRO ||
             MI.getOpcode() == MtG::LOADBYTEWISE_FI_MACRO) {
    const auto OpReg = MI.getOperand(0).getReg();
    const auto FrameIndex = MI.getOperand(1).getIndex();
    const int64_t totalOffset = computeFIOffset(FrameIndex, /*ExtraOffset=*/0);

    // We need a scratch register to compute the address (FP + offset). MtG
    // has no base+offset store, so the scratch is mandatory; the trailing
    // STOREBYTEWISE / LOADBYTEWISE will then take that scratch as its
    // address operand.
    //
    // Liveness analysis (LivePhysRegs walked backwards through MBB to MI)
    // tells us which physical registers carry a value at this point. We
    // pick the first allocatable register that's both *not live in* to MI
    // and *not equal to OpReg* — OpReg's live value is still needed here
    // (we're either about to store from it or, for the load-FI case, the
    // pseudo will produce its new value via the LOADBYTEWISE expansion
    // and we mustn't trash it before that).
    LivePhysRegs LivePhys(*this);
    LivePhys.addLiveOuts(MBB);
    for (auto It = MBB.rbegin(); It != MBB.rend(); ++It) {
      if (&*It == &MI)
        break;
      LivePhys.stepBackward(*It);
    }

    static const Register Candidates[] = {MtG::R3, MtG::R4, MtG::R5, MtG::R7,
                                          MtG::R8, MtG::R9, MtG::R10, MtG::R11};
    Register TmpReg;
    for (Register R : Candidates) {
      if (R == OpReg)
        continue;
      if (LivePhys.available(MF.getRegInfo(), R)) {
        TmpReg = R;
        break;
      }
    }

    bool NeedEmergency = !TmpReg.isValid();
    Register VictimReg;
    if (NeedEmergency) {
      // Every allocatable register at this point is either live or equal to
      // OpReg. Pick a victim (any allocatable register != OpReg), save its
      // value to the emergency slot at *SP via the byte-wise FP-trick, then
      // use the victim as our scratch. We'll reload the victim after the
      // FI elimination's emitted code is in place.
      for (Register R : Candidates) {
        if (R != OpReg) {
          VictimReg = R;
          break;
        }
      }
      assert(VictimReg.isValid() &&
             "MtG: not even a victim register available for emergency spill");
      emitEmergencySave(MBB, II, *TII, VictimReg);
      TmpReg = VictimReg;
    }

    if (TmpReg != getFrameRegister(MF))
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MOVE), TmpReg)
                         .addUse(getFrameRegister(MF)));
    MBB.insert(
        II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO)).addImm(totalOffset));

    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::ADD_MACRO), TmpReg)
                       .addReg(TmpReg)
                       .addReg(MtG::R0));

    if (MI.getOpcode() == MtG::STOREBYTEWISE_FI_MACRO) {
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::STOREBYTEWISE_MACRO))
                         .addUse(OpReg)
                         .addUse(TmpReg));
    } else {
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::LOADBYTEWISE_MACRO), OpReg)
                         .addUse(TmpReg));
    }

    if (NeedEmergency) {
      // Now reload the victim's value so subsequent code sees its
      // original contents.
      emitEmergencyReload(MBB, II, *TII, VictimReg);
    }

    MI.eraseFromParent();

    return true;
  }
  assert(false && "Unknown FrameIndex elimination!");
  return false;
}

Register MtGRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // Use SP (R2) as the frame base. The frame-offset formula in
  // eliminateFrameIndex (`getObjectOffset(FI) + getStackSize() + SPAdj`)
  // produces an SP-relative offset, so the base must really be the
  // post-prologue SP. Previously we returned R1, which was never
  // initialized in the prologue and held the caller's SP at runtime —
  // that made every frame access land in the *caller's* memory area
  // (latently broken on real hardware; only ursa's sparse memory model
  // tolerated it).
  return MtG::R2;
}
