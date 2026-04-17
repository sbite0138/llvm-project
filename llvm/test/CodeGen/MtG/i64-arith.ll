; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; i64 add — legalized to i32 pair add with carry.
define i64 @add64(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: add64:
; CHECK: Add
; CHECK: Return
  %r = add i64 %a, %b
  ret i64 %r
}

; i64 sub — legalized to i32 pair sub with borrow.
define i64 @sub64(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: sub64:
; CHECK: Return
  %r = sub i64 %a, %b
  ret i64 %r
}

; i64 mul — decomposed to i32 multiplies.
define i64 @mul64(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: mul64:
; CHECK: Mult
; CHECK: Return
  %r = mul i64 %a, %b
  ret i64 %r
}

; mulhs (signed multiply high) — Expand via i64 mul + shift.
define i32 @mulhs32(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: mulhs32:
; CHECK: Mult
; CHECK: Return
  %a64 = sext i32 %a to i64
  %b64 = sext i32 %b to i64
  %prod = mul i64 %a64, %b64
  %hi64 = lshr i64 %prod, 32
  %hi = trunc i64 %hi64 to i32
  ret i32 %hi
}

; i64 shl by constant.
define i64 @shl64(i64 %a) nounwind {
; CHECK-LABEL: shl64:
; CHECK: Return
  %r = shl i64 %a, 16
  ret i64 %r
}
