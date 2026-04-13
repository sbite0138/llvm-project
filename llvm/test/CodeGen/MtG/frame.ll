; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test stack frame setup and multiple arguments (stack passing).

define i32 @multi_arg(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: multi_arg:
; CHECK: Add
; CHECK: Return
  %1 = add i32 %a, %b
  %2 = add i32 %1, %c
  ret i32 %2
}
