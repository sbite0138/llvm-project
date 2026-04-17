; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; i64 division/remainder operations use runtime libcalls.

define i64 @sdiv64(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: sdiv64:
; CHECK: CallFwd	 __divdi3
; CHECK: Return
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @udiv64(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: udiv64:
; CHECK: CallFwd	 __udivdi3
; CHECK: Return
  %r = udiv i64 %a, %b
  ret i64 %r
}

define i64 @srem64(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: srem64:
; CHECK: CallFwd	 __moddi3
; CHECK: Return
  %r = srem i64 %a, %b
  ret i64 %r
}

define i64 @urem64(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: urem64:
; CHECK: CallFwd	 __umoddi3
; CHECK: Return
  %r = urem i64 %a, %b
  ret i64 %r
}
