; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test comparison and select operations.

define i32 @icmp_slt_select(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: icmp_slt_select:
; CHECK: FLess
; CHECK: SetF
; CHECK: Return
  %cmp = icmp slt i32 %a, %b
  %result = select i1 %cmp, i32 1, i32 0
  ret i32 %result
}
