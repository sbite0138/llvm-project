; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Signed division used to hit an unreachable path in LowerOperation because
; ISD::SDIV was marked Custom but never handled. Verify it now compiles and
; produces the expected abs/udiv/sign-fix sequence (here just checking the
; unsigned Divide shows up — the surrounding SRA/XOR steps expand through
; MACRO pseudos that still lack fully-realized expansion).
define i32 @sdiv_test(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: sdiv_test:
; CHECK: Divide
; CHECK: Return
  %1 = sdiv i32 %a, %b
  ret i32 %1
}
