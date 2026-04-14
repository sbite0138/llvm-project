; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Under the stack-only ABI, the argument to the callee is written to the
; caller's frame (at the outgoing-args slot, just above the emergency slot)
; rather than placed in an argument register. The call site emits exactly
; 2 NumBuild placeholders and a CallFwd with the bare target symbol — the
; ursa assembler resolves direction + distance when it sees the label.

declare void @sink(i32)

define void @caller(i32 %v) nounwind {
; CHECK-LABEL: caller:
; The arg must be stored before the call.
; CHECK: Store
; The call site's 2 NumBuild placeholders start at #0, #0 and the mnemonic
; is CallFwd regardless of direction (ursa flips to CallBwdR if needed).
; CHECK: NumBuild	 #0, #0
; CHECK-NEXT: NumBuild	 #0, #0
; CHECK-NEXT: CallFwd	 sink
; CHECK: Return
  call void @sink(i32 %v)
  ret void
}
