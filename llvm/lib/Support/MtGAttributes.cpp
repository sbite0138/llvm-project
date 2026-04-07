//===-- MtGAttributes.cpp - MtG Attributes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MtGAttributes.h"

using namespace llvm;
using namespace llvm::MtGAttrs;

static constexpr TagNameItem TagData[] = {{TagISA, "Tag_ISA"},
                                          {TagCodeModel, "Tag_Code_Model"},
                                          {TagDataModel, "Tag_Data_Model"},
                                          {TagEnumSize, "Tag_Enum_Size"}};

constexpr TagNameMap MtGAttributeTags{TagData};
const TagNameMap &llvm::MtGAttrs::getMtGAttributeTags() {
  return MtGAttributeTags;
}
