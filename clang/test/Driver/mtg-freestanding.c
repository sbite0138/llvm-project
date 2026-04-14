/// MtG is a bare-metal target with no libc. The MtG toolchain injects
/// -ffreestanding so idiomatic C source with -nostdlib compiles as-is.
// REQUIRES: mtg-registered-target
// RUN: %clang -### %s --target=mtg -c 2>&1 | FileCheck %s
// CHECK: "-cc1" "-triple" "mtg"
// CHECK-SAME: "-ffreestanding"
