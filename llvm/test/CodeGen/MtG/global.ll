; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Global variable lowering. The address is materialised into a register
; via NumBuildAddr (a meta-instruction the ursa assembler resolves to a
; concrete sequence of NumBuild digit pairs once it knows the symbol's
; runtime address); the value is then loaded byte-wise from that address.
;
; This pins down the compiler's contract: the global symbol must be
; emitted into the .data section with a `.long` initialiser, and any
; code reference must produce a `NumBuildAddr <sym>` line followed by
; a Move into the destination register.

@g = dso_local global i32 42, align 4

define i32 @load_g() nounwind {
entry:
  %v = load i32, ptr @g, align 4
  ret i32 %v
}

; CHECK-LABEL: load_g:
; CHECK: NumBuildAddr	 g
; CHECK: Move	 r{{[0-9]+}}, r0

; Data section is emitted with the initialiser intact.
; CHECK: g:
; CHECK: .long	42
