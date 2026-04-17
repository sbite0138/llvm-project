; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Switch statements lower to if-else chains (no jump tables).

define i32 @decode(i32 %op) nounwind {
; CHECK-LABEL: decode:
; CHECK-NOT: JumpTable
; CHECK: Return
entry:
  %masked = and i32 %op, 127
  switch i32 %masked, label %default [
    i32 55, label %lui
    i32 23, label %auipc
    i32 111, label %jal
    i32 103, label %jalr
    i32 99, label %branch
    i32 3, label %load
    i32 35, label %store
    i32 19, label %opimm
    i32 51, label %opreg
  ]
lui:     ret i32 1
auipc:   ret i32 2
jal:     ret i32 3
jalr:    ret i32 4
branch:  ret i32 5
load:    ret i32 6
store:   ret i32 7
opimm:   ret i32 8
opreg:   ret i32 9
default: ret i32 0
}
