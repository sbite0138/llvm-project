//===-- MtGAttributes.h - MtG Attributes ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===//
///
/// \file
/// This file contains enumerations for MtG ELF build attributes as
/// defined in the MtG ELF psABI specification.
///
/// MtG ELF psABI specification
///
/// https://www.ti.com/lit/pdf/slaa534
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_MtGATTRIBUTES_H
#define LLVM_SUPPORT_MtGATTRIBUTES_H

#include "llvm/Support/ELFAttributes.h"

namespace llvm {
namespace MtGAttrs {

const TagNameMap &getMtGAttributeTags();

enum AttrType : unsigned {
  // Attribute types in ELF/.MtG.attributes.
  TagISA = 4,
  TagCodeModel = 6,
  TagDataModel = 8,
  TagEnumSize = 10
};

enum ISA { ISAMtG = 1, ISAMtGX = 2 };
enum CodeModel { CMSmall = 1, CMLarge = 2 };
enum DataModel { DMSmall = 1, DMLarge = 2, DMRestricted = 3 };
enum EnumSize { ESSmall = 1, ESInteger = 2, ESDontCare = 3 };

} // namespace MtGAttrs
} // namespace llvm

#endif
