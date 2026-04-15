; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

; Struct lowering exercises two pieces that used to crash:
;
;   1. GlobalAddress with a non-zero Offset. Clang synthesises these
;      when indexing into a global aggregate (e.g. `&g.y` is
;      `@g + 4`). The backend materialises the base symbol via
;      NumBuildAddr and folds the constant offset in with an add;
;      ursa resolves the base at assemble time.
;
;   2. Sub-word memory traffic for `char` fields. `load i8` /
;      `truncstore i8` lower to the raw MtG Load / Store (one
;      simulator memory cell per byte), not the 4-cell
;      STOREBYTEWISE_MACRO / LOADBYTEWISE_MACRO used for i32.

; A struct literal in a global — `.long 3 / .long 5` in the data
; section, and loading `s.y` goes through NumBuildAddr with the
; non-zero 4-byte offset folded in.
%struct.Point = type { i32, i32 }
@s = dso_local global %struct.Point { i32 3, i32 5 }, align 4

define i32 @load_y() nounwind {
; CHECK-LABEL: load_y:
; CHECK: NumBuildAddr	 s
; CHECK: Return
entry:
  %y.addr = getelementptr inbounds %struct.Point, ptr @s, i32 0, i32 1
  %v = load i32, ptr %y.addr, align 4
  ret i32 %v
}

; i8 struct field stored to memory. Must compile to a raw `Store`
; (single-cell), never to STOREBYTEWISE_MACRO (which would splash
; 4 cells and corrupt the neighbouring i32 field).
%struct.Foo = type { i32, i8 }

define void @set_char(ptr %p, i8 %c) nounwind {
; CHECK-LABEL: set_char:
; CHECK: Store
; CHECK-NOT: STOREBYTEWISE_MACRO
; CHECK: Return
entry:
  %y.addr = getelementptr inbounds %struct.Foo, ptr %p, i32 0, i32 1
  store i8 %c, ptr %y.addr, align 1
  ret void
}

; Zero-extending i8 load (the usual shape when a `char` is read
; into an `int` context). Must lower to a raw `Load`; the
; simulator cell already holds the byte value in [0, 256).
define i32 @read_uchar(ptr %p) nounwind {
; CHECK-LABEL: read_uchar:
; CHECK: Load
; CHECK-NOT: LOADBYTEWISE_MACRO
; CHECK: Return
entry:
  %v = load i8, ptr %p, align 1
  %z = zext i8 %v to i32
  ret i32 %z
}

; Data-section check: the global struct is emitted field-by-field
; with `.long` entries so ursa's .data parser can image it into
; memory before execution.
; CHECK-LABEL: s:
; CHECK: .long	3
; CHECK: .long	5

; A struct containing a self-referential pointer — previously
; ursa's `.long <symbol>` parser rejected the symbol reference with
; "invalid literal for int()". Check the backend emits the symbol
; in the .data section; ursa's deferred data-fixup pass resolves
; it after parsing (covered end-to-end in TESTING.md's runnable
; programs, not by this FileCheck).
%struct.Node = type { i32, ptr }
@n3 = dso_local global %struct.Node { i32 3, ptr null }, align 4
@n2 = dso_local global %struct.Node { i32 2, ptr @n3 }, align 4
@n1 = dso_local global %struct.Node { i32 1, ptr @n2 }, align 4

; CHECK-LABEL: n2:
; CHECK: .long	2
; CHECK: .long	n3
; CHECK-LABEL: n1:
; CHECK: .long	1
; CHECK: .long	n2
