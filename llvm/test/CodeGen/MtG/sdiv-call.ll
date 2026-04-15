; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Regression test for an infinite-loop / wrong-answer bug that used to
; affect sdiv when the caller had enough register pressure to force a
; byte-wise spill inside the sdiv's 32-iteration bit-decomposition loop.
;
; The bit-decomposition loop ends each iteration with
;   FIsZero r1 ; JumpBwdNF .LBB<N>_1
; where $r1 is the loop counter computed by a preceding SetNF. Between
; SetNF and the branch, the backend may emit 4+ byte-wise spills. Before
; the fix, ADD_MACRO and STOREBYTEWISE_MACRO carried overly broad
; implicit-defs of $r1 (inherited from a worst-case Defs = [R0..R7] tblgen
; block). That made the PEI LivePhys walk decide $r1 was dead at the
; spill, so it picked $r1 as the scratch IterAddrReg and clobbered the
; loop counter — the loop never exited.
;
; The IR below mirrors the C reproducer:
;
;   int divide(int a, int b) { return a / b; }
;   void _start(void) {
;     int q = divide(10, 3);  // 3
;     __mtg_output('0' + q);
;     __mtg_output(10);
;   }
;
; What we assert: the asm compiles cleanly (no asserts / infinite
; expansion) and the shape is the expected "Divide under an sdiv sign-fix
; skeleton, then return" for `divide`, plus a call + two Outputs in
; `_start`. A pure compile-check cannot prove the loop terminates at
; runtime — see llvm/lib/Target/MtG/TESTING.md for the corresponding
; ursa end-to-end check (running the built .s should print "3\n").

target datalayout = "e-p:32:32-i32:32:32-i64:32:32-f64:32:32-n32-S32"
target triple = "mtg"

define dso_local i32 @divide(i32 noundef %a, i32 noundef %b) nounwind {
; CHECK-LABEL: divide:
; CHECK: Divide
; CHECK: Return
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %div = sdiv i32 %0, %1
  ret i32 %div
}

declare dso_local void @__mtg_output(i32 noundef)

define dso_local void @_start() nounwind {
; CHECK-LABEL: _start:
; CHECK: CallFwd{{.*}}divide
; CHECK: Output
; CHECK: Output
; CHECK: Return
entry:
  %q = alloca i32, align 4
  %call = call i32 @divide(i32 noundef 10, i32 noundef 3)
  store i32 %call, ptr %q, align 4
  %0 = load i32, ptr %q, align 4
  %add = add nsw i32 48, %0
  call void @__mtg_output(i32 noundef %add)
  call void @__mtg_output(i32 noundef 10)
  ret void
}
