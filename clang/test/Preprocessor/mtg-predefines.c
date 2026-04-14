// REQUIRES: mtg-registered-target
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=mtg < /dev/null \
// RUN:   | FileCheck -match-full-lines %s

// CHECK-DAG: #define MtG 1
// CHECK-DAG: #define __MtG__ 1
// CHECK-DAG: #define __mtg__ 1
