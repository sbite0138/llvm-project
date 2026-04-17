//===-- MtGInstrInfo.cpp - MtG Instruction Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MtG implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "MtGInstrInfo.h"
#include "MCTargetDesc/MtGMCTargetDesc.h"
#include "MtG.h"
#include "MtGMachineFunctionInfo.h"
#include "MtGRegisterInfo.h"
#include "MtGTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "mtg-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "MtGGenInstrInfo.inc"

// Pin the vtable to this file.
void MtGInstrInfo::anchor() {}

// MtGInstrInfo::MtGInstrInfo(MtGSubtarget &STI) : MtGGenInstrInfo(), RI() {}
MtGInstrInfo::MtGInstrInfo(MtGSubtarget &STI)
    : MtGGenInstrInfo(STI, RI, MtG::ADJCALLSTACKDOWN, MtG::ADJCALLSTACKUP),
      RI() {}

void MtGInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC,
    Register VReg,
    MachineInstr::MIFlag Flags) const {

  assert(SrcReg != MtG::FLAG && "Cannot store FLAG register to stack slot");
  // MI can legitimately be MBB.end() (fast regalloc inserts spills at the
  // end of a basic block), so pull the DebugLoc only when safe.
  DebugLoc DL = (MI == MBB.end()) ? DebugLoc() : MI->getDebugLoc();
  // Propagate isKill so the byte-wise expansion can destroy SrcReg in place
  // when it's the last use, and only pull in a scratch register otherwise.
  BuildMI(MBB, MI, DL, get(MtG::STOREBYTEWISE_FI_MACRO))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIdx);
}

void MtGInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register DestReg, int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        Register VReg, unsigned SubReg,
                                        MachineInstr::MIFlag Flags) const {

  DebugLoc DL = (MI == MBB.end()) ? DebugLoc() : MI->getDebugLoc();
  BuildMI(MBB, MI, DL, get(MtG::LOADBYTEWISE_FI_MACRO), DestReg)
      .addFrameIndex(FrameIdx);
}

void MtGInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL, Register DestReg,
                               Register SrcReg, bool KillSrc,
                               bool RenamableDest, bool RenamableSrc) const {
  // MtG's Move requires rY != rZ (the Y==Z encoding means Zero, not Move).
  // Elide self-copies so ISel / regalloc can call copyPhysReg uniformly
  // without having to filter them.
  if (DestReg == SrcReg)
    return;
  BuildMI(MBB, I, DL, get(MtG::MOVE), DestReg)
      .addUse(SrcReg, getKillRegState(KillSrc));
}

bool MtGInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget<MtGSubtarget>().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  auto BuildSafeMove = [&](MachineBasicBlock &MBB, MachineInstr &MI,
                           const DebugLoc &DL, const TargetInstrInfo &TII,
                           MCRegister DstReg, MCRegister SrcReg) {
    if (DstReg != SrcReg)
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE), DstReg)
          .addUse(SrcReg);
  };
  if (MI.getOpcode() == MtG::MOVEIMM_MACRO) {
    // Expand "$dst = MOVEIMM_MACRO imm" → "NUMBUILD_MACRO imm; MOVE $dst, $r0".
    // Mark the trailing MOVE with NoMerge so that BranchFolder's tail-merge
    // (including the run inside MachineBlockPlacement) cannot hoist it across
    // a BR_PSEUDO / BRCOND_PSEUDO — doing so would separate it from the
    // NUMBUILD_MACRO that set $r0, and the branch expansion clobbers $r0 via
    // its own NumBuild placeholders.
    auto DstReg = MI.getOperand(0).getReg();
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(MI.getOperand(1).getImm());
    if (DstReg != MtG::R0) {
      auto MoveMI = BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE),
                            DstReg)
                        .addUse(MtG::R0);
      MoveMI->setFlag(MachineInstr::NoMerge);
    }
    MI.eraseFromParent();
    expandPostRAPseudo(*NumBuildMI);
    return true;
  } else if (MI.getOpcode() == MtG::MOVEADDR_MACRO) {
    // "$dst = MOVEADDR_MACRO @sym" → "NumBuildAddr @sym; Move $dst, r0".
    // The NumBuildAddr placeholder is a meta-instruction that ursa's
    // fixup_jumps replaces with the 4 base-144 NumBuild digit pairs of
    // the symbol's resolved address.
    auto DstReg = MI.getOperand(0).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILDADDR))
        .add(MI.getOperand(1));
    if (DstReg != MtG::R0) {
      auto MoveMI = BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE),
                            DstReg)
                        .addUse(MtG::R0);
      MoveMI->setFlag(MachineInstr::NoMerge);
    }
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::MOVEREG_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(1).getReg();
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, DstReg, SrcReg);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::ADD_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32);
    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    MI.eraseFromParent();
    expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    return true;
  } else if (MI.getOpcode() == MtG::ADD_IMM_MACRO) {
    auto DstReg = MI.getOperand(1).getReg();
    auto SrcImm = MI.getOperand(2).getImm();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    std::vector<MachineInstr *> NumBuildMIs;
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(SrcImm));
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(MtG::R0);
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32));

    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    for (auto *NumBuildMI : NumBuildMIs)
      expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::ASHR_MACRO) {
    // Arithmetic right shift: preserve the sign bit.
    //   sign = (dst >= 2^31) ? 1 : 0   (via FLess + SetF)
    //   mask = sign * (2^32 - 2^(32-imm))
    //   shifted = dst / 2^imm          (unsigned, via Divide)
    //   dst = shifted + mask
    auto DstReg = MI.getOperand(0).getReg();
    int64_t Imm = MI.getOperand(2).getImm();
    std::set<Register> UseRegs = {MtG::R0, MtG::R6};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");

    if (Imm == 0) {
      MI.eraseFromParent();
      return true;
    }
    // Shifts by 32+ in LLVM IR are UB, but if one slips through, saturating
    // to 31 produces sign-extension semantics (0 or -1 based on sign).
    if (Imm >= 32)
      Imm = 31;

    // Pick a scratch distinct from DstReg. R3/R4 are work registers.
    Register SignReg = (DstReg == MtG::R3) ? MtG::R4 : MtG::R3;

    // 1. FLAG = (2^31 - 1 < dst) = (dst >= 2^31) = (sign bit set).
    //    FLess is spec-ordered "flag = rZ < rY", so pass (Y=dst, Z=R0).
    auto NumBuildSignMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm((int64_t)0x7FFFFFFFLL);
    auto FLessMI = BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
                       .addUse(DstReg)
                       .addUse(MtG::R0);
    FLessMI->setFlag(MachineInstr::NoMerge);

    // 2. SignReg = 1 if neg, 0 otherwise.
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETF), SignReg);

    // 3. SignReg *= mask_val so it becomes the mask (or 0).
    uint64_t MaskVal = (1ULL << 32) - (1ULL << (32 - Imm));
    auto NumBuildMaskMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm((int64_t)MaskVal);
    auto MultMaskMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), SignReg)
            .addUse(SignReg)
            .addUse(MtG::R0);
    MultMaskMI->setFlag(MachineInstr::NoMerge);

    // 4. Divide dst by 2^imm; quotient lives in R6.
    uint64_t Divisor = 1ULL << Imm;
    auto NumBuildDivMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm((int64_t)Divisor);
    auto DivMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
            .addUse(DstReg);
    DivMI->setFlag(MachineInstr::NoMerge);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE), DstReg)
        .addUse(MtG::R6);

    // 5. dst = LSR_result + mask.
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(SignReg);

    MI.eraseFromParent();
    expandPostRAPseudo(*NumBuildSignMI);
    expandPostRAPseudo(*NumBuildMaskMI);
    expandPostRAPseudo(*NumBuildDivMI);
    return true;
  } else if (MI.getOpcode() == MtG::SHR_MACRO) {
    // Logical right shift: dst = dst / 2^imm (unsigned).
    // MtG Divide puts dst/r0 into R6 and dst%r0 into dst, so we copy R6
    // back into dst afterwards.
    auto DstReg = MI.getOperand(0).getReg();
    int64_t Imm = MI.getOperand(2).getImm();
    std::set<Register> UseRegs = {MtG::R0, MtG::R6};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");

    if (Imm == 0) {
      MI.eraseFromParent();
      return true;
    }
    if (Imm >= 32) {
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ZERO), DstReg);
      MI.eraseFromParent();
      return true;
    }

    uint64_t Divisor = 1ULL << Imm;
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(Divisor);
    auto DivMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
            .addUse(DstReg);
    DivMI->setFlag(MachineInstr::NoMerge);
    // Quotient lives in R6; copy it back into dst.
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MOVE), DstReg)
        .addUse(MtG::R6);
    MI.eraseFromParent();
    expandPostRAPseudo(*NumBuildMI);
    return true;
  } else if (MI.getOpcode() == MtG::SHL_MACRO) {
    // "$dst = SHL_MACRO $dst, imm"  →  $dst = $dst * 2^imm (mod 2^32).
    auto DstReg = MI.getOperand(0).getReg();
    int64_t Imm = MI.getOperand(2).getImm();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");

    int64_t Factor = (Imm >= 32 || Imm < 0) ? 0 : (1LL << Imm);

    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(Factor);
    auto MultMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    MultMI->setFlag(MachineInstr::NoMerge);
    MI.eraseFromParent();
    expandPostRAPseudo(*NumBuildMI);
    return true;
  } else if (MI.getOpcode() == MtG::NEG_MACRO) {
    // "$dst = NEG_MACRO $dst"  (src1 is tied to dst by the .td Constraint)
    //   →  NUMBUILD_MACRO -1   ; R0 = 2^32 - 1 = -1 (mod 2^32)
    //       MULT $dst, R0      ; $dst *= -1
    auto DstReg = MI.getOperand(0).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");

    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(-1);
    auto MultMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    // Prevent BranchFolder tail-merge from separating the MULT from the
    // NumBuilds that set R0; the MtG jump expansion later clobbers R0.
    MultMI->setFlag(MachineInstr::NoMerge);
    MI.eraseFromParent();
    expandPostRAPseudo(*NumBuildMI);
    return true;
  } else if (MI.getOpcode() == MtG::SUB_MACRO) {

    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");
    auto NegMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NEG_MACRO), SrcReg)
            .addUse(SrcReg);
    expandPostRAPseudo(*NegMI);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32);

    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::MULLO_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");

    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), DstReg)
        .addUse(DstReg)
        .addUse(SrcReg);
    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(1UL << 32);
    auto RemMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::REM_MACRO), DstReg)
            .addUse(DstReg)
            .addUse(MtG::R0);
    expandPostRAPseudo(*NumBuildMI);
    expandPostRAPseudo(*RemMI);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::DIV_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");
    assert(DstReg != MtG::R0 && "DstReg cannot be R0");
    assert(DstReg != MtG::R6 && "DstReg cannot be R6");

    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R0, SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
        .addUse(DstReg);
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, DstReg, MtG::R6);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::STOREBYTEWISE_MACRO) {
    // Split a 32-bit value byte-wise into 4 consecutive memory cells
    // starting at $addr. The value is progressively divided by 256, the
    // low byte (remainder) is stored each iteration, and the upper part
    // (quotient) comes back in R6. Address iteration is destructive —
    // ADD1 on IterAddrReg — so whichever register we use as IterAddrReg
    // loses its incoming value.
    //
    // We can destroy $val in place (reuse it as ByteWorkReg) if $val is
    // marked killed (no one reads it after this MI). Same for $addr as
    // IterAddrReg. That lets us reduce the scratch count from 2 to as
    // few as 0 depending on how many operands are dying.
    //
    // Any allocatable register outside {$val, $addr} is a valid scratch
    // as long as LivePhysRegs says it's available. Non-free candidates
    // can be evicted through the single 4-byte emergency-spill slot at
    // *SP; we can evict at most one such register per expansion.
    //
    // With the FP-trick at hand, the worst remaining case is "both $val
    // and $addr live-after AND zero of ~9 allocatable registers free",
    // which is practically unreachable.
    auto ValReg = MI.getOperand(0).getReg();
    auto AddrReg = MI.getOperand(1).getReg();
    assert(ValReg != AddrReg && "STOREBYTEWISE expects distinct val and addr");
    const bool ValKilled = MI.getOperand(0).isKill();
    const bool AddrKilled = MI.getOperand(1).isKill();

    const auto &MBB_ = *MI.getParent();
    LivePhysRegs LivePhys(*MBB_.getParent()->getSubtarget().getRegisterInfo());
    LivePhys.addLiveOuts(MBB_);
    for (auto It = MBB_.rbegin(); It != MBB_.rend(); ++It) {
      if (&*It == &MI)
        break;
      LivePhys.stepBackward(*It);
    }
    const MachineRegisterInfo &MRI_ = MBB_.getParent()->getRegInfo();

    SmallVector<Register, 4> Free, Candidates;
    for (Register R : {MtG::R1, MtG::R3, MtG::R4, MtG::R5, MtG::R7, MtG::R8,
                       MtG::R9, MtG::R10, MtG::R11}) {
      if (R == ValReg || R == AddrReg)
        continue;
      Candidates.push_back(R);
      if (LivePhys.available(MRI_, R))
        Free.push_back(R);
    }

    unsigned NumScratchesNeeded =
        (ValKilled ? 0u : 1u) + (AddrKilled ? 0u : 1u);
    Register IterAddrReg, ByteWorkReg, Victim;

    auto pickOne = [&](unsigned Idx) -> Register {
      // Prefer free regs, fall back to eviction for Idx == last needed.
      if (Idx < Free.size())
        return Free[Idx];
      if (Candidates.size() > Free.size())
        return Candidates[Free.size()]; // first non-free
      return Register();
    };

    if (Free.size() >= NumScratchesNeeded) {
      unsigned Next = 0;
      if (!AddrKilled) IterAddrReg = pickOne(Next++);
      if (!ValKilled)  ByteWorkReg = pickOne(Next++);
    } else if (Free.size() + 1 >= NumScratchesNeeded &&
               Candidates.size() >= NumScratchesNeeded) {
      // Evict exactly one candidate; that register becomes the Victim.
      unsigned Next = 0;
      if (!AddrKilled) IterAddrReg = pickOne(Next++);
      if (!ValKilled)  ByteWorkReg = pickOne(Next++);
      // The last pickOne() will have returned the Victim (first non-free).
      Victim = Next == 2 ? ByteWorkReg : IterAddrReg;
      assert(Victim.isValid() && !LivePhys.available(MRI_, Victim));
    } else {
      report_fatal_error("MtG: STOREBYTEWISE_MACRO cannot satisfy its "
                         "scratch-register needs; no free allocatable "
                         "register is available and we can only evict one "
                         "through the emergency slot");
    }
    if (ValKilled)  ByteWorkReg = ValReg;  // in-place destruction
    if (AddrKilled) IterAddrReg = AddrReg;

    if (Victim.isValid())
      MtGRegisterInfo::emitEmergencySave(const_cast<MachineBasicBlock &>(MBB_),
                                         MI.getIterator(), TII, Victim);

    if (!AddrKilled)
      BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, IterAddrReg, AddrReg);
    if (!ValKilled)
      BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, ByteWorkReg, ValReg);

    auto NumBuildMI =
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(256);
    for (int i = 0; i < 4; ++i) {
      // ByteWorkReg = ByteWorkReg % 256 (low byte); R6 = upper bytes.
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), ByteWorkReg)
          .addUse(ByteWorkReg);
      // *iter = low byte (Store is Y=value, Z=address per the MtG spec).
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::STORE))
          .addUse(ByteWorkReg)
          .addUse(IterAddrReg);
      if (i < 3) {
        // Refill ByteWorkReg with the higher bytes and advance the iterator.
        BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, ByteWorkReg, MtG::R6);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD1), IterAddrReg)
            .addUse(IterAddrReg);
      }
    }

    expandPostRAPseudo(*NumBuildMI);
    if (Victim.isValid())
      MtGRegisterInfo::emitEmergencyReload(
          const_cast<MachineBasicBlock &>(MBB_), MI.getIterator(), TII, Victim);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::LOADBYTEWISE_MACRO) {
    // Reconstruct a 32-bit value from 4 consecutive memory cells starting at
    // $addr. The high byte is loaded first and the accumulator is shifted up
    // by 256 (via MULT) before adding each lower byte. Only one dynamic
    // scratch is needed (the iterating address register) — we reuse R6 as
    // the per-iteration byte temp because it isn't touched between the LOAD
    // / ADD / MULT / SUB1COND operations of the loop.
    //
    // IterAddrReg must avoid $val, $addr, AND any register that's still
    // live at this MI. RegAllocFast sometimes leaves values live across
    // pseudos whose Defs list should have forced a spill, so we don't
    // trust the blanket Defs and instead ask LivePhysRegs directly. If
    // no candidate is free we evict one through the emergency slot.
    auto ValReg = MI.getOperand(0).getReg();
    auto AddrReg = MI.getOperand(1).getReg();

    const auto &MBB_ = *MI.getParent();
    LivePhysRegs LivePhys(*MBB_.getParent()->getSubtarget().getRegisterInfo());
    LivePhys.addLiveOuts(MBB_);
    for (auto It = MBB_.rbegin(); It != MBB_.rend(); ++It) {
      if (&*It == &MI)
        break;
      LivePhys.stepBackward(*It);
    }
    const MachineRegisterInfo &MRI_ = MBB_.getParent()->getRegInfo();

    // Scratch candidates: every allocatable register other than $val /
    // $addr. Prefer a free one; if none are free, evict one through the
    // emergency slot.
    const auto Candidates = {
        MtG::R1,  MtG::R3,  MtG::R4,  MtG::R5, MtG::R7,
        MtG::R8,  MtG::R9,  MtG::R10, MtG::R11,
    };
    Register IterAddrReg, Victim;
    for (Register R : Candidates) {
      if (R == ValReg || R == AddrReg)
        continue;
      if (LivePhys.available(MRI_, R)) {
        IterAddrReg = R;
        break;
      }
    }
    if (!IterAddrReg.isValid()) {
      // Every candidate is live — evict one through the emergency slot.
      for (Register R : Candidates) {
        if (R == ValReg || R == AddrReg)
          continue;
        IterAddrReg = R;
        Victim = R;
        break;
      }
    }
    if (!IterAddrReg.isValid())
      report_fatal_error("MtG: LOADBYTEWISE_MACRO cannot find any scratch "
                         "distinct from $val and $addr");
    if (Victim.isValid())
      MtGRegisterInfo::emitEmergencySave(const_cast<MachineBasicBlock &>(MBB_),
                                         MI.getIterator(), TII, Victim);

    std::vector<MachineInstr *> NumBuildMIs;

    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, IterAddrReg, AddrReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ZERO), ValReg);
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(3));
    // IterAddrReg now points at the high byte (addr + 3).
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), IterAddrReg)
        .addUse(IterAddrReg)
        .addUse(MtG::R0);
    NumBuildMIs.push_back(
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD_MACRO))
            .addImm(256));

    for (int i = 0; i < 4; ++i) {
      // Byte temp lives in R6 — no Divide in this loop so it's stable.
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::LOAD), MtG::R6)
          .addUse(IterAddrReg);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::ADD), ValReg)
          .addUse(ValReg)
          .addUse(MtG::R6);
      if (i < 3) {
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::MULT), ValReg)
            .addUse(ValReg)
            .addUse(MtG::R0);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SUB1COND), IterAddrReg)
            .addUse(IterAddrReg);
      }
    }

    for (auto *NumBuildMI : NumBuildMIs)
      expandPostRAPseudo(*NumBuildMI);

    if (Victim.isValid())
      MtGRegisterInfo::emitEmergencyReload(
          const_cast<MachineBasicBlock &>(MBB_), MI.getIterator(), TII, Victim);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::REM_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg = MI.getOperand(2).getReg();
    std::set<Register> UseRegs = {MtG::R0, MtG::R6};
    assert(UseRegs.count(DstReg) == 0 && "Invalid DstReg");
    assert((UseRegs.count(SrcReg) == 0 || !isRegisterLiveAfter(MI, SrcReg)) &&
           "Invalid SrcReg - register conflicts with macro expansion");
    BuildSafeMove(MBB, MI, MI.getDebugLoc(), TII, MtG::R0, SrcReg);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::DIVIDE), DstReg)
        .addUse(DstReg);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::NUMBUILD_MACRO) {
    auto OrgImm = MI.getOperand(0).getImm();
    if (OrgImm < 0) {
      OrgImm = (1ULL << 32) + OrgImm;
    }
    std::vector<uint32_t> Imms;
    while (OrgImm) {
      Imms.push_back(OrgImm % 144);
      OrgImm /= 144;
    }
    if (Imms.size() == 0) {
      Imms.push_back(0);
    }
    std::reverse(Imms.begin(), Imms.end());
    for (auto Imm : Imms) {
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD))
          .addImm(Imm / 12)
          .addImm(Imm % 12);
    }
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::EQ_MACRO) {
    // FLess is spec-ordered "flag = rZ < rY". Pair up both directions and
    // OR them via flag-combining so the accumulated flag is "src1 != src2";
    // SETNF then gives us src1 == src2.
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg1 = MI.getOperand(1).getReg();
    auto SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg2)
        .addUse(SrcReg1);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg1)
        .addUse(SrcReg2);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETNF), DstReg);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::NEQ_MACRO) {
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg1 = MI.getOperand(1).getReg();
    auto SrcReg2 = MI.getOperand(2).getReg();
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg2)
        .addUse(SrcReg1);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
        .addUse(SrcReg1)
        .addUse(SrcReg2);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETF), DstReg);
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::ULT_MACRO ||
             MI.getOpcode() == MtG::UGT_MACRO ||
             MI.getOpcode() == MtG::ULE_MACRO ||
             MI.getOpcode() == MtG::UGE_MACRO) {
    // Unsigned comparisons: FLess is already unsigned, no bias needed.
    unsigned Op = MI.getOpcode();
    auto DstReg = MI.getOperand(0).getReg();
    auto SrcReg1 = MI.getOperand(1).getReg();
    auto SrcReg2 = MI.getOperand(2).getReg();

    if (Op == MtG::ULT_MACRO) {
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
          .addUse(SrcReg2).addUse(SrcReg1);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETF), DstReg);
    } else if (Op == MtG::UGT_MACRO) {
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
          .addUse(SrcReg1).addUse(SrcReg2);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETF), DstReg);
    } else if (Op == MtG::ULE_MACRO) {
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
          .addUse(SrcReg1).addUse(SrcReg2);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETNF), DstReg);
    } else { // UGE_MACRO
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::FLESS))
          .addUse(SrcReg2).addUse(SrcReg1);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::SETNF), DstReg);
    }
    MI.eraseFromParent();
    return true;
  } else if (MI.getOpcode() == MtG::RET_PSEUDO) {
    for (int i = 0; i < 4; ++i)
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::NUMBUILD))
          .addImm(0)
          .addImm(0);
    BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(MtG::RETURN));
    MI.eraseFromParent();
    return true;
  }

  return false;
}

bool MtGInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Stop at the first non-terminator instruction.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't easily be handled.
    if (!I->isBranch())
      return true;

    unsigned Opc = I->getOpcode();

    // Handle unconditional branch.
    if (Opc == MtG::BR_PSEUDO) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // Delete anything after an unconditional branch.
      MBB.erase(std::next(I), MBB.end());
      Cond.clear();
      FBB = nullptr;

      // Delete a branch that is a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      TBB = I->getOperand(0).getMBB();
      continue;
    }

    // Handle conditional branch.
    if (Opc == MtG::BRCOND_PSEUDO) {
      // Only handle the case where this is the first (and only) conditional
      // branch seen so far.
      if (!Cond.empty())
        return true;

      FBB = TBB;
      TBB = I->getOperand(2).getMBB();
      Cond.push_back(I->getOperand(0)); // Cond register
      Cond.push_back(I->getOperand(1)); // Polarity immediate
      continue;
    }

    // Unknown branch type.
    return true;
  }

  return false;
}

unsigned MtGInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != MtG::BR_PSEUDO &&
        I->getOpcode() != MtG::BRCOND_PSEUDO)
      break;
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

unsigned MtGInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 0 || Cond.size() == 2) &&
         "MtG branch conditions have 0 or 2 components!");
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(MtG::BR_PSEUDO)).addMBB(TBB);
    return 1;
  }

  unsigned Count = 0;
  BuildMI(&MBB, DL, get(MtG::BRCOND_PSEUDO))
      .addReg(Cond[0].getReg())
      .addImm(Cond[1].getImm())
      .addMBB(TBB);
  ++Count;

  if (FBB) {
    BuildMI(&MBB, DL, get(MtG::BR_PSEUDO)).addMBB(FBB);
    ++Count;
  }
  return Count;
}

bool MtGInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 2 && "Invalid branch condition!");
  Cond[1].setImm(Cond[1].getImm() ^ 1);
  return false;
}

bool MtGInstrInfo::isRegisterLiveAfter(const MachineInstr &MI,
                                       Register Reg) const {
  const MachineBasicBlock *MBB = MI.getParent();
  const MachineFunction *MF = MBB->getParent();

  // Analyze within the same basic block
  MachineBasicBlock::const_iterator NextI = std::next(MI.getIterator());
  for (auto I = NextI; I != MBB->end(); ++I) {
    // Check if the register is read (used)
    if (I->readsRegister(Reg, &getRegisterInfo())) {
      LLVM_DEBUG({
        dbgs() << "Register " << printReg(Reg, &getRegisterInfo())
               << " is live after instruction: " << *I << "\n";
        dbgs() << "In function: " << MF->getName() << "\n";
        dbgs() << "In basic block: " << MBB->getName() << "\n";
        dbgs() << "In instruction: ";
        MI.dump();
        MF->dump();
      });
      return true;
    }
    // Check if the register is redefined (killed)
    if (I->definesRegister(Reg, &getRegisterInfo())) {
      return false;
    }
  }

  // Check across basic block boundaries
  // If the register is live-in to any successor block, it's live
  for (MachineBasicBlock *Succ : MBB->successors()) {
    if (Succ->isLiveIn(Reg)) {
      LLVM_DEBUG({
        dbgs() << "Register " << printReg(Reg, &getRegisterInfo())
               << " is live after instruction: " << *Succ << "\n";
        dbgs() << "In function: " << MF->getName() << "\n";
        dbgs() << "In basic block: " << MBB->getName() << "\n";
        dbgs() << "In instruction: ";
        MI.dump();
        MF->dump();
      });
      return true;
    }
  }

  return false;
}

void MtGInstrInfo::adjustStackPtr(unsigned SP, int64_t Amount,
                                  MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  assert(isInt<32>(Amount));
  BuildMI(MBB, I, DL, get(MtG::ADD_IMM_MACRO), SP).addUse(SP).addImm(Amount);
}
