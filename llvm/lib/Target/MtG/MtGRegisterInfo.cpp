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

// ADD_MACRO's .td declaration lists implicit-defs of R0-R7 to cover the
// worst case, but its actual post-expansion form (ADD + NUMBUILD_MACRO +
// REM_MACRO) only clobbers R0 (NUMBUILD_MACRO) and R6 (DIVIDE via
// REM_MACRO). When we emit ADD_MACRO as part of FI elimination, leaving
// the over-broad implicit-defs on the MI confuses the LivePhys walk that
// the next FI-elimination step performs: registers like $r1 appear dead
// at the spill site even though they're still used later in the MBB.
// Strip the inaccurate bits so liveness stays faithful.
static void narrowAddMacroClobbers(MachineInstr &MI) {
  static const MCPhysReg Spurious[] = {MtG::R1, MtG::R3, MtG::R4,
                                       MtG::R5, MtG::R7};
  for (MCPhysReg R : Spurious) {
    int Idx = MI.findRegisterDefOperandIdx(R, /*TRI=*/nullptr,
                                           /*isDead=*/false,
                                           /*Overlap=*/false);
    if (Idx >= 0 && MI.getOperand(Idx).isImplicit())
      MI.removeOperand(Idx);
  }
}

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
                                        Register VictimReg,
                                        unsigned Slot) {
  DebugLoc DL = II != MBB.end() ? II->getDebugLoc() : DebugLoc();
  // Slot 1 is at SP+4; advance SP to reach it.
  if (Slot == 1)
    for (int i = 0; i < 4; ++i)
      BuildMI(MBB, II, DL, TII.get(MtG::ADD1), MtG::R2).addUse(MtG::R2);
  // R0 = 256
  BuildMI(MBB, II, DL, TII.get(MtG::NUMBUILD_MACRO)).addImm(256);
  for (int i = 0; i < 4; ++i) {
    BuildMI(MBB, II, DL, TII.get(MtG::DIVIDE), VictimReg).addUse(VictimReg);
    BuildMI(MBB, II, DL, TII.get(MtG::STORE)).addUse(VictimReg).addUse(MtG::R2);
    if (i < 3) {
      BuildMI(MBB, II, DL, TII.get(MtG::MOVE), VictimReg).addUse(MtG::R6);
      BuildMI(MBB, II, DL, TII.get(MtG::ADD1), MtG::R2).addUse(MtG::R2);
    }
  }
  // Restore SP: undo 3 loop ADD1s + 4 if Slot 1.
  for (int i = 0; i < 3 + (Slot == 1 ? 4 : 0); ++i)
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
                                          Register VictimReg,
                                          unsigned Slot) {
  DebugLoc DL = II != MBB.end() ? II->getDebugLoc() : DebugLoc();
  // SP += 3 (+ 4 if Slot 1) : start at the high-byte slot.
  for (int i = 0; i < 3 + (Slot == 1 ? 4 : 0); ++i)
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
  // SP is now back at slot base (3 ADD1's matched by 3 SUB1COND's in the loop).
  // If Slot 1, undo the extra 4 ADD1s to restore SP to its original position.
  if (Slot == 1)
    for (int i = 0; i < 4; ++i)
      BuildMI(MBB, II, DL, TII.get(MtG::SUB1COND), MtG::R2).addUse(MtG::R2);
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
    // ADD_MACRO handles the 32-bit wrap via its REM expansion — needed when
    // totalOffset is negative (e.g. frame-index slots below SP). The pseudo's
    // Defs = [R0..R7] declaration is overly conservative, though: the real
    // post-expansion clobbers are just R0 (NUMBUILD_MACRO) and R6 (DIVIDE via
    // REM_MACRO). Strip the spurious R1/R3/R4/R5/R7 implicit-defs after
    // BuildMI so LivePhys doesn't think other live values die here when we
    // process the next spill in the same MBB.
    {
      auto AddMI = BuildMI(MF, DL, TII->get(MtG::ADD_MACRO), DstReg)
                       .addReg(DstReg)
                       .addReg(MtG::R0);
      narrowAddMacroClobbers(*AddMI.getInstr());
      MBB.insert(II, AddMI);
    }
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::STOREBYTEWISE_FI_MACRO ||
             MI.getOpcode() == MtG::LOADBYTEWISE_FI_MACRO) {
    // Monolithic FI expansion: produce the full byte-wise sequence in one
    // place rather than leaving an intermediate STOREBYTEWISE_MACRO /
    // LOADBYTEWISE_MACRO for later expansion. This lets us coordinate the
    // address-computation scratch (TmpReg) and the byte-loop scratch
    // (ByteWorkReg) with a single LivePhysRegs analysis and a single
    // emergency-slot bookkeeping. Previously the two phases each picked
    // independently and could collide on emergency slot 0 when both
    // needed eviction (commit 976d3c63c179 partially mitigated one such
    // case via a kill flag on the inserted intermediate, but the
    // underlying design still had the gap).
    //
    // What we need:
    //   * STORE: TmpReg (= IterAddrReg, holds FP+offset; killed by loop)
    //            ByteWorkReg (= OpReg if OpReg killed; else separate so
    //            the spilled value survives the divide-by-256 chain)
    //   * LOAD : TmpReg (= IterAddrReg). OpReg is the destination
    //            accumulator — no separate ByteWorkReg.
    const bool IsStore = MI.getOpcode() == MtG::STOREBYTEWISE_FI_MACRO;
    const auto OpReg = MI.getOperand(0).getReg();
    const auto FrameIndex = MI.getOperand(1).getIndex();
    const int64_t totalOffset = computeFIOffset(FrameIndex, /*ExtraOffset=*/0);
    const bool OpKilled = IsStore && MI.getOperand(0).isKill();

    LivePhysRegs LivePhys(*this);
    LivePhys.addLiveOuts(MBB);
    for (auto It = MBB.rbegin(); It != MBB.rend(); ++It) {
      if (&*It == &MI)
        break;
      LivePhys.stepBackward(*It);
    }

    static const Register Candidates[] = {MtG::R1, MtG::R3, MtG::R4, MtG::R5,
                                          MtG::R7, MtG::R8, MtG::R9, MtG::R10,
                                          MtG::R11};
    SmallVector<Register, 9> Free;
    for (Register R : Candidates) {
      if (R == OpReg)
        continue;
      if (LivePhys.available(MF.getRegInfo(), R))
        Free.push_back(R);
    }

    const bool NeedSeparateByteWork = IsStore && !OpKilled;
    const unsigned NumScratchesNeeded = 1 + (NeedSeparateByteWork ? 1 : 0);

    auto pickNonFree = [&](Register Skip) -> Register {
      for (Register R : Candidates) {
        if (R == OpReg || R == Skip)
          continue;
        if (!LivePhys.available(MF.getRegInfo(), R))
          return R;
      }
      return Register();
    };

    Register TmpReg, ByteWorkReg, Victim, Victim2;
    if (Free.size() >= NumScratchesNeeded) {
      TmpReg = Free[0];
      if (NeedSeparateByteWork)
        ByteWorkReg = Free[1];
    } else if (Free.size() + 1 >= NumScratchesNeeded) {
      // Need to evict exactly one register. Prefer to keep TmpReg from Free
      // (it's used first and longest), and evict for ByteWorkReg.
      if (NumScratchesNeeded == 1) {
        Victim = pickNonFree(/*Skip=*/Register());
        TmpReg = Victim;
      } else {
        TmpReg = Free[0];
        Victim = pickNonFree(/*Skip=*/TmpReg);
        ByteWorkReg = Victim;
      }
    } else if (NeedSeparateByteWork &&
               Free.size() + 2 >= NumScratchesNeeded) {
      // Both need eviction — slot 0 for TmpReg, slot 1 for ByteWorkReg.
      Victim = pickNonFree(/*Skip=*/Register());
      Victim2 = pickNonFree(/*Skip=*/Victim);
      TmpReg = Victim;
      ByteWorkReg = Victim2;
    } else {
      report_fatal_error("MtG: byte-wise FI macro cannot satisfy its scratch "
                         "register needs");
    }

    if (OpKilled)
      ByteWorkReg = OpReg; // safe: OpReg's value dies after this MI

    assert(TmpReg.isValid());
    assert(TmpReg != OpReg);
    assert(!IsStore || ByteWorkReg.isValid());
    assert(!IsStore || ByteWorkReg != TmpReg);

    // Emergency saves (in order so the corresponding reloads can pop in
    // reverse, even though both slots are independent).
    if (Victim.isValid())
      emitEmergencySave(MBB, II, *TII, Victim, /*Slot=*/0);
    if (Victim2.isValid())
      emitEmergencySave(MBB, II, *TII, Victim2, /*Slot=*/1);

    // === Address: TmpReg = FP + totalOffset (mod 2^32) ===
    if (TmpReg != getFrameRegister(MF))
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MOVE), TmpReg)
                         .addUse(getFrameRegister(MF)));
    MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO))
                       .addImm(totalOffset));
    {
      auto AddMI = BuildMI(MF, DL, TII->get(MtG::ADD_MACRO), TmpReg)
                       .addReg(TmpReg)
                       .addReg(MtG::R0);
      narrowAddMacroClobbers(*AddMI.getInstr());
      MBB.insert(II, AddMI);
    }

    // === Byte-wise body ===
    if (IsStore) {
      // Mirror of STOREBYTEWISE_MACRO's expansion (MtGInstrInfo.cpp), but
      // emitted directly here so we never leave a dangling
      // STOREBYTEWISE_MACRO whose own scratch logic would re-run LivePhys
      // and possibly re-use slot 0.
      if (ByteWorkReg != OpReg)
        MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MOVE), ByteWorkReg)
                           .addUse(OpReg));
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO))
                         .addImm(256));
      for (int i = 0; i < 4; ++i) {
        MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::DIVIDE), ByteWorkReg)
                           .addUse(ByteWorkReg));
        MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::STORE))
                           .addUse(ByteWorkReg)
                           .addUse(TmpReg));
        if (i < 3) {
          MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MOVE), ByteWorkReg)
                             .addUse(MtG::R6));
          MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::ADD1), TmpReg)
                             .addUse(TmpReg));
        }
      }
    } else {
      // Mirror of LOADBYTEWISE_MACRO: high byte first, accumulate via *256.
      // OpReg serves as both ValReg and accumulator; no separate ByteWorkReg
      // needed (R6 is the per-iteration byte temp). The first iteration
      // loads directly into OpReg so we skip the usual Zero + initial Add
      // pair (saves 2 primitives per expansion — matches the analogous
      // optimization in MtGInstrInfo.cpp's LOADBYTEWISE_MACRO handler).
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO)).addImm(3));
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::ADD), TmpReg)
                         .addUse(TmpReg)
                         .addUse(MtG::R0));
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::NUMBUILD_MACRO))
                         .addImm(256));
      // High byte straight into the accumulator.
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::LOAD), OpReg)
                         .addUse(TmpReg));
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MULT), OpReg)
                         .addUse(OpReg)
                         .addUse(MtG::R0));
      MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::SUB1COND), TmpReg)
                         .addUse(TmpReg));
      for (int i = 1; i < 4; ++i) {
        MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::LOAD), MtG::R6)
                           .addUse(TmpReg));
        MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::ADD), OpReg)
                           .addUse(OpReg)
                           .addUse(MtG::R6));
        if (i < 3) {
          MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::MULT), OpReg)
                             .addUse(OpReg)
                             .addUse(MtG::R0));
          MBB.insert(II, BuildMI(MF, DL, TII->get(MtG::SUB1COND), TmpReg)
                             .addUse(TmpReg));
        }
      }
    }

    // Reload in reverse order (slot 1 first, then slot 0).
    if (Victim2.isValid())
      emitEmergencyReload(MBB, II, *TII, Victim2, /*Slot=*/1);
    if (Victim.isValid())
      emitEmergencyReload(MBB, II, *TII, Victim, /*Slot=*/0);

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
