// Verify that mtg.h declares the three MtG hardware builtins and that calls
// to them produce direct extern declarations (the backend recognizes these
// by name in MtGTargetLowering::LowerCall).
//
// REQUIRES: mtg-registered-target
// RUN: %clang_cc1 -triple mtg -internal-isystem %S/../../../lib/Headers \
// RUN:   -emit-llvm -o - %s | FileCheck %s

#include <mtg.h>

void entry(void) {
  int a = __mtg_input_a();
  int b = __mtg_input_b();
  __mtg_output(a + b);
}

// CHECK: declare {{(dso_local )?}}i32 @__mtg_input_a()
// CHECK: declare {{(dso_local )?}}i32 @__mtg_input_b()
// CHECK: declare {{(dso_local )?}}void @__mtg_output(i32 {{.*}})
