; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Negation via 2's-complement trick: multiply by (2^32 - 1). Must not leave
; a NEG_MACRO pseudo in the final asm, and SUB should also emit a real
; Mult (since SUB_MACRO's expansion depends on NEG_MACRO working).

define i32 @neg(i32 %a) nounwind {
; CHECK-LABEL: neg:
; CHECK: NumBuild
; CHECK: Mult
; CHECK-NOT: NEG_MACRO
; CHECK: Return
  %r = sub i32 0, %a
  ret i32 %r
}

define i32 @sub(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: sub:
; CHECK: Mult
; CHECK: Add
; CHECK-NOT: NEG_MACRO
; CHECK: Return
  %r = sub i32 %a, %b
  ret i32 %r
}
