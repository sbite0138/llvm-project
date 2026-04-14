; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; A call to the pseudo builtin `__mtg_output(i32)` lowers directly to the
; MtG hardware Output instruction (no real call sequence is emitted, no
; external `Call $__mtg_output` appears).
declare void @__mtg_output(i32)

; Constant arguments are materialized via NumBuild/Move into R8 and then
; emitted.
define void @emit_const() nounwind {
; CHECK-LABEL: emit_const:
; CHECK: NumBuild
; CHECK: Move	 r8,
; CHECK-NEXT: Output	 r8
; CHECK-NOT: Call $__mtg_output
; CHECK: Return
  call void @__mtg_output(i32 72)
  ret void
}

; The value argument is honored (it ends up in R8 per the MtG calling
; convention regardless of the original register).
define void @emit_var(i32 %v) nounwind {
; CHECK-LABEL: emit_var:
; CHECK: Output	 r8
; CHECK-NOT: Call $__mtg_output
; CHECK: Return
  call void @__mtg_output(i32 %v)
  ret void
}

; Multiple emissions work back-to-back. The register allocator may pick
; different source registers for each, so just assert two Output
; instructions appear in order.
define void @emit_two(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: emit_two:
; CHECK: Output	 r{{[0-9]+}}
; CHECK: Output	 r{{[0-9]+}}
; CHECK-NOT: Call $__mtg_output
; CHECK: Return
  call void @__mtg_output(i32 %a)
  call void @__mtg_output(i32 %b)
  ret void
}
