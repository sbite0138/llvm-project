; RUN: llc -mtriple=mtg < %s 2>/dev/null | FileCheck %s

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; Vararg callee: va_start stores the varargs frame pointer.
define i32 @va_func(i32 %fixed, ...) nounwind {
; CHECK-LABEL: va_func:
; CHECK: Store
; CHECK: Return
entry:
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %p = load ptr, ptr %ap
  %val = load i32, ptr %p
  call void @llvm.va_end(ptr %ap)
  %r = add i32 %fixed, %val
  ret i32 %r
}

; Vararg caller: all args passed on stack (same as non-vararg).
define i32 @va_caller() nounwind {
; CHECK-LABEL: va_caller:
; CHECK: CallFwd va_func
; CHECK: Return
  %r = call i32 (i32, ...) @va_func(i32 10, i32 20)
  ret i32 %r
}
