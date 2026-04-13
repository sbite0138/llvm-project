; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test bitwise logic operations.

define i32 @and_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: and_test:
; CHECK: Mult
; CHECK: Return
  %1 = and i32 %a, %b
  ret i32 %1
}

define i32 @or_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: or_test:
; CHECK: Return
  %1 = or i32 %a, %b
  ret i32 %1
}

define i32 @xor_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: xor_test:
; CHECK: Return
  %1 = xor i32 %a, %b
  ret i32 %1
}
