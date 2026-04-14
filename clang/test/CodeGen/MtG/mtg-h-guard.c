// Verify that mtg.h refuses non-MtG targets via #error.
//
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -internal-isystem %S/../../../lib/Headers \
// RUN:   -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s

#include <mtg.h>

// CHECK: mtg.h is only valid for the MtG target
