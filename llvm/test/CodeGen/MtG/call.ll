; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test function call lowering under the stack-only ABI: the argument is
; stored on the stack (not in a register), the call site emits 2 NumBuild
; placeholders followed by CallFwd (the ursa assembler flips to CallBwdR
; if the target turns out to sit at a lower PC), and the Return also has
; its Z' placeholder pair.

declare i32 @external_func(i32)

define i32 @call_test(i32 %a) nounwind {
; CHECK-LABEL: call_test:
; CHECK: NumBuild	 #0, #0
; CHECK-NEXT: NumBuild	 #0, #0
; CHECK-NEXT: CallFwd	 external_func
; CHECK: Return
  %1 = call i32 @external_func(i32 %a)
  ret i32 %1
}
