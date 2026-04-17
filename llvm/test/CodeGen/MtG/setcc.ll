; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Signed comparisons use bias (ADD 0x80000000 + SubCond 2^32) before FLess.
; Unsigned comparisons use FLess directly.

define i32 @slt(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: slt:
; CHECK: SubCond
; CHECK: FLess
; CHECK: SetF
; CHECK: Return
  %r = icmp slt i32 %a, %b
  %ext = zext i1 %r to i32
  ret i32 %ext
}

define i32 @sgt(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: sgt:
; CHECK: SubCond
; CHECK: FLess
; CHECK: SetF
; CHECK: Return
  %r = icmp sgt i32 %a, %b
  %ext = zext i1 %r to i32
  ret i32 %ext
}

define i32 @sle(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: sle:
; CHECK: SubCond
; CHECK: FLess
; CHECK: SetNF
; CHECK: Return
  %r = icmp sle i32 %a, %b
  %ext = zext i1 %r to i32
  ret i32 %ext
}

define i32 @sge(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: sge:
; CHECK: SubCond
; CHECK: FLess
; CHECK: SetNF
; CHECK: Return
  %r = icmp sge i32 %a, %b
  %ext = zext i1 %r to i32
  ret i32 %ext
}

define i32 @ult(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: ult:
; CHECK-NOT: SubCond
; CHECK: FLess
; CHECK: SetF
; CHECK: Return
  %r = icmp ult i32 %a, %b
  %ext = zext i1 %r to i32
  ret i32 %ext
}

define i32 @ule(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: ule:
; CHECK-NOT: SubCond
; CHECK: FLess
; CHECK: SetNF
; CHECK: Return
  %r = icmp ule i32 %a, %b
  %ext = zext i1 %r to i32
  ret i32 %ext
}
