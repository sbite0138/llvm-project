; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test function calls and calling convention.

declare i32 @external_func(i32)

define i32 @call_test(i32 %a) nounwind {
; CHECK-LABEL: call_test:
; CHECK: Call $external_func
; CHECK: Return
  %1 = call i32 @external_func(i32 %a)
  ret i32 %1
}
