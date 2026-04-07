//===- MtG.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MtG is a 16-bit microcontroller RISC architecture. The instruction set
// has only 27 core instructions orthogonally augmented with a variety
// of addressing modes for source and destination operands. Entire address space
// of MtG is 64KB (the extended MtGX architecture is not considered here).
// A typical MtG MCU has several kilobytes of RAM and ROM, plenty
// of peripherals and is generally optimized for a low power consumption.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class MtG final : public TargetInfo {
public:
  MtG(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

MtG::MtG(Ctx &ctx) : TargetInfo(ctx) {
  // mov.b #0, r3
  trapInstr = {0x43, 0x43, 0x43, 0x43};
}

RelExpr MtG::getRelExpr(RelType type, const Symbol &s,
                        const uint8_t *loc) const {
  switch (type) {
  case R_MtG_10_PCREL:
  case R_MtG_16_PCREL:
  case R_MtG_16_PCREL_BYTE:
  case R_MtG_2X_PCREL:
  case R_MtG_RL_PCREL:
  case R_MtG_SYM_DIFF:
    return R_PC;
  default:
    return R_ABS;
  }
}

void MtG::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_MtG_8:
    checkIntUInt(loc, val, 8, rel);
    *loc = val;
    break;
  case R_MtG_16:
  case R_MtG_16_PCREL:
  case R_MtG_16_BYTE:
  case R_MtG_16_PCREL_BYTE:
    checkIntUInt(loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_MtG_32:
    checkIntUInt(loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_MtG_10_PCREL: {
    int16_t offset = ((int16_t)val >> 1) - 1;
    checkInt(loc, offset, 10, rel);
    write16le(loc, (read16le(loc) & 0xFC00) | (offset & 0x3FF));
    break;
  }
  default:
    error(getErrorLocation(loc) + "unrecognized relocation " +
          toString(rel.type));
  }
}

TargetInfo *elf::getMtGTargetInfo(Ctx &ctx) {
  static MtG target(ctx);
  return &target;
}
