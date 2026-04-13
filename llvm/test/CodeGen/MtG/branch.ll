; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Basic if-then-else.  Should lower to FIsZero + a forward conditional jump
; plus an unconditional forward jump.
define i32 @if_then_else(i32 %a) nounwind {
; CHECK-LABEL: if_then_else:
; CHECK: FIsZero
; CHECK: JumpFwd
; CHECK: Return
entry:
  %cmp = icmp eq i32 %a, 0
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Simple counted loop. Requires a backward conditional branch for the loop
; back-edge.
define i32 @counted_loop(i32 %n) nounwind {
; CHECK-LABEL: counted_loop:
; CHECK: FIsZero
; CHECK: JumpBwd
; CHECK: Return
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %newsum, %loop ]
  %newsum = add i32 %sum, %i
  %inc = add i32 %i, 1
  %cmp = icmp ult i32 %inc, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %newsum
}

; Nested conditionals. Produces two FIsZero/Jump pairs.
define i32 @nested(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: nested:
; CHECK: FIsZero
; CHECK: JumpFwd
; CHECK: FIsZero
; CHECK: JumpFwd
; CHECK: Return
entry:
  %c1 = icmp eq i32 %a, 0
  br i1 %c1, label %a_zero, label %a_nz
a_zero:
  %c2 = icmp eq i32 %b, 0
  br i1 %c2, label %both_zero, label %only_a
both_zero:
  ret i32 0
only_a:
  ret i32 1
a_nz:
  ret i32 2
}

; Unconditional branch (often just a fall-through at this layout level,
; but keeps the pseudo pipeline honest for non-trivial CFGs).
define i32 @uncond_branch(i32 %a) nounwind {
; CHECK-LABEL: uncond_branch:
; CHECK: Return
entry:
  br label %next
next:
  ret i32 %a
}
