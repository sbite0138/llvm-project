; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test basic arithmetic operations.

define i32 @add_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: add_test:
; CHECK: Add
; CHECK: Return
  %1 = add i32 %a, %b
  ret i32 %1
}

define i32 @sub_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: sub_test:
; CHECK: Sub1Cond
; CHECK: Return
  %1 = sub i32 %a, %b
  ret i32 %1
}

define i32 @mul_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: mul_test:
; CHECK: Mult
; CHECK: Return
  %1 = mul i32 %a, %b
  ret i32 %1
}

define i32 @srem_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: srem_test:
; CHECK: Return
  %1 = srem i32 %a, %b
  ret i32 %1
}
