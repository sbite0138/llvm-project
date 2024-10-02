//===-- MtGAttributeParser.cpp - MtG Attribute Parser ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MtGAttributeParser.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::MtGAttrs;

constexpr std::array<MtGAttributeParser::DisplayHandler, 4>
    MtGAttributeParser::DisplayRoutines{
        {{MtGAttrs::TagISA, &MtGAttributeParser::parseISA},
         {MtGAttrs::TagCodeModel, &MtGAttributeParser::parseCodeModel},
         {MtGAttrs::TagDataModel, &MtGAttributeParser::parseDataModel},
         {MtGAttrs::TagEnumSize, &MtGAttributeParser::parseEnumSize}}};

Error MtGAttributeParser::parseISA(AttrType Tag) {
  static const char *const StringVals[] = {"None", "MtG", "MtGX"};
  return parseStringAttribute("ISA", Tag, ArrayRef(StringVals));
}

Error MtGAttributeParser::parseCodeModel(AttrType Tag) {
  static const char *const StringVals[] = {"None", "Small", "Large"};
  return parseStringAttribute("Code Model", Tag, ArrayRef(StringVals));
}

Error MtGAttributeParser::parseDataModel(AttrType Tag) {
  static const char *const StringVals[] = {"None", "Small", "Large",
                                           "Restricted"};
  return parseStringAttribute("Data Model", Tag, ArrayRef(StringVals));
}

Error MtGAttributeParser::parseEnumSize(AttrType Tag) {
  static const char *const StringVals[] = {"None", "Small", "Integer",
                                           "Don't Care"};
  return parseStringAttribute("Enum Size", Tag, ArrayRef(StringVals));
}

Error MtGAttributeParser::handler(uint64_t Tag, bool &Handled) {
  Handled = false;
  for (const DisplayHandler &Disp : DisplayRoutines) {
    if (uint64_t(Disp.Attribute) != Tag)
      continue;
    if (Error E = (this->*Disp.Routine)(static_cast<AttrType>(Tag)))
      return E;
    Handled = true;
    break;
  }
  return Error::success();
}
