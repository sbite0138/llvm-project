; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test return values and immediate loading.

define i32 @ret_zero() nounwind {
; CHECK-LABEL: ret_zero:
; CHECK: Return
  ret i32 0
}

define i32 @ret_const() nounwind {
; CHECK-LABEL: ret_const:
; CHECK: NumBuild
; CHECK: Return
  ret i32 42
}

define i32 @ret_arg(i32 %a) nounwind {
; CHECK-LABEL: ret_arg:
; CHECK: Return
  ret i32 %a
}
