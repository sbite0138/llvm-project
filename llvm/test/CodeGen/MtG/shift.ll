; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test shift operations with immediate operands.

define i32 @shl_test(i32 %a) nounwind {
; CHECK-LABEL: shl_test:
; CHECK: Mult
; CHECK: Return
  %1 = shl i32 %a, 2
  ret i32 %1
}
