; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Under the stack-only ABI, the argument to the callee is written to the
; caller's frame (at the outgoing-args slot, just above the emergency slot)
; rather than placed in an argument register. The call site emits 4 NumBuild
; placeholders and a CallFwd with the bare target symbol.

declare void @sink(i32)

define void @caller(i32 %v) nounwind {
; CHECK-LABEL: caller:
; CHECK: Store
; CHECK: NumBuild
; CHECK: CallFwd	 sink
; CHECK: Return
  call void @sink(i32 %v)
  ret void
}
