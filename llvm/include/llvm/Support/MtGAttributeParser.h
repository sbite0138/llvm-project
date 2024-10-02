//===-- MtGAttributeParser.h - MtG Attribute Parser -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains support routines for parsing MtG ELF build attributes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MtGATTRIBUTEPARSER_H
#define LLVM_SUPPORT_MtGATTRIBUTEPARSER_H

#include "llvm/Support/ELFAttributeParser.h"
#include "llvm/Support/MtGAttributes.h"

namespace llvm {
class MtGAttributeParser : public ELFAttributeParser {
  struct DisplayHandler {
    MtGAttrs::AttrType Attribute;
    Error (MtGAttributeParser::*Routine)(MtGAttrs::AttrType);
  };
  static const std::array<DisplayHandler, 4> DisplayRoutines;

  Error parseISA(MtGAttrs::AttrType Tag);
  Error parseCodeModel(MtGAttrs::AttrType Tag);
  Error parseDataModel(MtGAttrs::AttrType Tag);
  Error parseEnumSize(MtGAttrs::AttrType Tag);

  Error handler(uint64_t Tag, bool &Handled) override;

public:
  MtGAttributeParser(ScopedPrinter *SW)
      : ELFAttributeParser(SW, MtGAttrs::getMtGAttributeTags(), "mspabi") {}
  MtGAttributeParser()
      : ELFAttributeParser(MtGAttrs::getMtGAttributeTags(), "mspabi") {}
};
} // namespace llvm

#endif
