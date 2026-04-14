; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Test comparison and select operations.
;
; select is expanded into the branchless arithmetic form
;   c = (cond != 0), nc = (cond == 0)
;   dst = c*tval + nc*fval
; via FIsZero + SetNF + SetF + two Mult + Add, so SELECT_MACRO must not
; appear in the final asm.

define i32 @icmp_slt_select(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: icmp_slt_select:
; CHECK: FLess
; CHECK: SetF
; CHECK: Return
  %cmp = icmp slt i32 %a, %b
  %result = select i1 %cmp, i32 1, i32 0
  ret i32 %result
}

; A non-trivial select over runtime values: exercises the arithmetic
; expansion (FIsZero → SetNF/SetF → Mult → Mult → Add).
define i32 @pick(i32 %cond, i32 %a, i32 %b) nounwind {
; CHECK-LABEL: pick:
; CHECK: FIsZero
; CHECK: SetNF
; CHECK: SetF
; CHECK: Mult
; CHECK: Mult
; CHECK-NOT: SELECT_MACRO
; CHECK: Return
  %c = icmp ne i32 %cond, 0
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}
