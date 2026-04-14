; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Immediate shift operations.

; shl $a, imm  →  $a * 2^imm    (Mult by 2^imm, with NumBuild to load it)
define i32 @shl_test(i32 %a) nounwind {
; CHECK-LABEL: shl_test:
; CHECK: NumBuild
; CHECK: Mult
; CHECK-NOT: SHL_MACRO
; CHECK: Return
  %1 = shl i32 %a, 2
  ret i32 %1
}

; lshr $a, imm  →  $a / 2^imm (unsigned, via Divide + Move from R6).
define i32 @lshr_test(i32 %a) nounwind {
; CHECK-LABEL: lshr_test:
; CHECK: NumBuild
; CHECK: Divide
; CHECK-NOT: SHR_MACRO
; CHECK: Return
  %1 = lshr i32 %a, 3
  ret i32 %1
}

; ashr $a, imm  →  sign-aware right shift.
; Expansion: FLess + SetF (sign), Mult (mask), Divide (LSR), Add (combine).
define i32 @ashr_test(i32 %a) nounwind {
; CHECK-LABEL: ashr_test:
; CHECK: FLess
; CHECK: SetF
; CHECK: Divide
; CHECK: Add
; CHECK-NOT: ASHR_MACRO
; CHECK: Return
  %1 = ashr i32 %a, 3
  ret i32 %1
}
