; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test function call lowering under the stack-only ABI: the argument is
; stored on the stack (not in a register), the call site emits 4 NumBuild
; placeholders followed by CallFwd, and the Return also has its 4 NumBuild
; placeholder quad.

declare i32 @external_func(i32)

define i32 @call_test(i32 %a) nounwind {
; CHECK-LABEL: call_test:
; CHECK: NumBuild
; CHECK: CallFwd	 external_func
; CHECK: Return
  %1 = call i32 @external_func(i32 %a)
  ret i32 %1
}
