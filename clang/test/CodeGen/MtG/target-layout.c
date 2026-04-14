// Verify that clang's MtG TargetInfo emits the same data layout string that
// MtGTargetMachine::computeDataLayout uses. If these drift, llc will refuse
// the IR with a "data layout alignment ... mismatch" error.
//
// REQUIRES: mtg-registered-target
// RUN: %clang_cc1 -triple mtg -emit-llvm -o - %s | FileCheck %s

// CHECK: target datalayout = "e-p:32:32-i32:32:32-i64:32:32-f64:32:32-n32-S32"
// CHECK: target triple = "mtg"

int x;
