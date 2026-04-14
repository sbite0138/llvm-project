; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; The MtG hardware exposes two input channels (Alice and Bob) via the
; `2 Y 0` and `2 Y 1` instruction encodings. We expose them as the
; `__mtg_input_a` and `__mtg_input_b` builtins, lowered directly to the
; AInput / BInput hardware instructions instead of going through a real
; call sequence.
declare i32 @__mtg_input_a()
declare i32 @__mtg_input_b()
declare void @__mtg_output(i32)

define void @echo_a() nounwind {
; CHECK-LABEL: echo_a:
; CHECK: AInput
; CHECK-NOT: Call $__mtg_input_a
; CHECK: Output
; CHECK: Return
  %v = call i32 @__mtg_input_a()
  call void @__mtg_output(i32 %v)
  ret void
}

define void @sum_ab() nounwind {
; CHECK-LABEL: sum_ab:
; CHECK: AInput
; CHECK: BInput
; CHECK-NOT: Call $__mtg_input_
; CHECK: Output
; CHECK: Return
  %a = call i32 @__mtg_input_a()
  %b = call i32 @__mtg_input_b()
  %sum = add i32 %a, %b
  call void @__mtg_output(i32 %sum)
  ret void
}
