; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; AND / OR / XOR are expanded as a 32-iteration bit-decomposition loop:
;   for each bit b in 31..0:
;     extract bit_a from $a, bit_b from $b via SubCond + SetNF
;     combine the two single-bit values by op-specific arithmetic
;     accumulate combined * (1 << b) into the result
; The loop body uses SubCond / Halve / FIsZero, multiplies, additions,
; and a back-edge BRCOND_PSEUDO. The pseudos themselves must not appear.

define i32 @and_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: and_test:
; CHECK: SubCond
; CHECK: Mult
; CHECK: Halve
; CHECK: JumpBwd
; CHECK-NOT: AND_MACRO
; CHECK: Return
  %r = and i32 %a, %b
  ret i32 %r
}

define i32 @or_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: or_test:
; CHECK: SubCond
; CHECK: FIsZero
; CHECK: SetNF
; CHECK-NOT: OR_MACRO
; CHECK: Return
  %r = or i32 %a, %b
  ret i32 %r
}

define i32 @xor_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: xor_test:
; CHECK: SubCond
; CHECK: Halve
; CHECK: SetF
; CHECK-NOT: XOR_MACRO
; CHECK: Return
  %r = xor i32 %a, %b
  ret i32 %r
}
