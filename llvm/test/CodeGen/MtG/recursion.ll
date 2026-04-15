; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Self-recursion: the call site inside `recur` resolves to a backward
; target, but LLVM still emits `CallFwd recur` — ursa's fixup_jumps flips
; it to CallBwdR when the final layout puts the callee at a lower PC. We
; check for exactly that placeholder here (direction is resolved at
; assemble time, not in LLVM).

define i32 @recur(i32 %n) nounwind {
entry:
  %cmp = icmp eq i32 %n, 0
  br i1 %cmp, label %base, label %step
base:
  ret i32 0
step:
  %n1 = sub i32 %n, 1
  %r = call i32 @recur(i32 %n1)
  %r1 = add i32 1, %r
  ret i32 %r1
}

; CHECK-LABEL: recur:
; The self-call must be emitted as a `CallFwd recur` placeholder (ursa's
; fixup_jumps flips the direction at assemble time). Check for the full
; pattern by anchoring on CallFwd and looking back at the preceding two
; NumBuild #0, #0 placeholders.
; CHECK: CallFwd{{.*}}recur
; CHECK: Return
