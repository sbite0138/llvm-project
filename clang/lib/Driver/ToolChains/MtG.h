//===--- MtG.h - MtG ToolChain Implementations ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Minimal toolchain for the MtG (Magic: The Gathering Turing Machine)
// experimental backend. There is no real linker / loader for the MtG ISA
// — the simulator (ursa) consumes asm directly — so this toolchain is
// freestanding and not expected to drive a link step. Users should pass
// -S to clang to stop at .s output.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MTG_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MTG_H

#include "Gnu.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace toolchains {

class LLVM_LIBRARY_VISIBILITY MtGToolChain : public Generic_ELF {
public:
  MtGToolChain(const Driver &D, const llvm::Triple &Triple,
               const llvm::opt::ArgList &Args)
      : Generic_ELF(D, Triple, Args) {}

  // No real ABI for hosted execution; default to freestanding.
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return true; }
  bool SupportsProfiling() const override { return false; }
  bool HasNativeLLVMSupport() const override { return true; }

  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind) const override;

  // No C++ standard library available.
  void addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args) const override {
  }
  void
  addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                           llvm::opt::ArgStringList &CC1Args) const override {}
};

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MTG_H
