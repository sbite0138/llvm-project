; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; MtGFrameLowering reserves 4 extra bytes at *SP for the scavenger's
; emergency-spill slot, and eliminateFrameIndex compensates for that when
; turning frame indices into SP-relative addresses. Without the fix, loads
; and stores of user stack slots end up 4 bytes below their real home,
; which ursa reports as "Memory read from uninitialized address".
;
; Escape the pointer via an external sink so the alloca can't be SROA'd.
; The frame then contains a single 4-byte local, a 4-byte outgoing-arg
; slot for the @sink call, and the 4-byte emergency slot, so the prologue
; subtracts 12 and the epilogue adds 12 back.

declare void @sink(ptr)

define i32 @alloca_one_i32(i32 %v) nounwind {
entry:
  %slot = alloca i32, align 4
  store i32 %v, ptr %slot, align 4
  call void @sink(ptr %slot)
  %1 = load i32, ptr %slot, align 4
  ret i32 %1
}

; Prologue: SP -= (4 local + 4 outgoing-arg + 8 emergency) = 16.
; The NumBuild sequence encodes -16 (= 2^32 - 16) then adds to SP.
; CHECK-LABEL: alloca_one_i32:
; CHECK: NumBuild
; CHECK: Add	 r2, r0
; Epilogue: SP += 12, just before Return.
; CHECK: Add	 r2, r0
; CHECK: Return
