//===--- MtG.cpp - MtG ToolChain Implementations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MtG.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

void MtGToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                         ArgStringList &CC1Args,
                                         Action::OffloadKind) const {
  // The MtG runtime is bare-metal: no libc, no startup files, no hosted
  // assumptions. Default the frontend to freestanding so that idiomatic
  // C source with -nostdlib doesn't need to repeat -ffreestanding.
  if (!DriverArgs.hasArg(clang::options::OPT_ffreestanding))
    CC1Args.push_back("-ffreestanding");
}
