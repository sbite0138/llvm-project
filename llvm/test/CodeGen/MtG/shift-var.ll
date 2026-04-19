; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Variable (register) shift operations.

; shl %a, %b  →  loop: peel bit31 via SubCond(a, 2^31), then double.
; Avoiding a 2^32 vreg wrap mask keeps the constant spill-safe.
define i32 @shl_var(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: shl_var:
; CHECK: FIsZero
; CHECK: SetF
; CHECK: SubCond
; CHECK: Add
; CHECK: Sub1Cond
; CHECK: Return
  %r = shl i32 %a, %b
  ret i32 %r
}

; lshr %a, %b  →  loop: halve %a, %b times.
define i32 @lshr_var(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: lshr_var:
; CHECK: FIsZero
; CHECK: SetF
; CHECK: Halve
; CHECK: Sub1Cond
; CHECK: Return
  %r = lshr i32 %a, %b
  ret i32 %r
}

; ashr %a, %b  →  save sign; loop: halve + restore MSB, %b times.
define i32 @ashr_var(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: ashr_var:
; CHECK: FLess
; CHECK: SetF
; CHECK: Mult
; CHECK: FIsZero
; CHECK: Halve
; CHECK: Add
; CHECK: Sub1Cond
; CHECK: Return
  %r = ashr i32 %a, %b
  ret i32 %r
}
