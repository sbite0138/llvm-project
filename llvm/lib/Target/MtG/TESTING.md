# MtG backend testing

The MtG backend is exercised by three complementary test layers:

1. **`llvm-lit` CodeGen tests** under `llvm/test/CodeGen/MtG/` that pin down
   the asm output for each lowering path (regression tests against `llc`).
2. **`llvm-lit` clang frontend tests** under `clang/test/CodeGen/MtG/`,
   `clang/test/Driver/mtg-*.c`, and `clang/test/Preprocessor/mtg-*.c` that
   lock in the clang-side contract (data layout, preprocessor defines,
   driver flags, `mtg.h` guard).
3. **End-to-end runs through the ursa simulator** that execute compiled
   programs and check their actual output (semantic tests).

Layers 1 and 2 feed off `llc` / `clang` built from this tree; layer 3 pipes
their output into the Python simulator under `~/llvm-project/ursa/`.

## Build

The backend lives under `llvm/lib/Target/MtG/`, the clang frontend pieces
under `clang/lib/Basic/Targets/MtG.{h,cpp}` +
`clang/lib/Driver/ToolChains/MtG.{h,cpp}`, and the user-facing builtin
header at `clang/lib/Headers/mtg.h`. Configure CMake so that `MtG` is in the
targets-to-build list (the default LLVM build does this on this branch).
Then build both tools plus the resource header:

```sh
cd ~/llvm-project/build
ninja -j4 clang llc mtg-resource-headers
```

Use `-j4` (or smaller); larger parallelism can OOM the machine while
building heavy CodeGen libraries.

## Layer 1: `llvm-lit` regression tests

### Location

- Test files: `llvm/test/CodeGen/MtG/*.ll`
- Lit config: `llvm/test/CodeGen/MtG/lit.local.cfg` (skips the suite when
  the MtG target isn't built)

### Running

The whole suite:

```sh
~/llvm-project/build/bin/llvm-lit llvm/test/CodeGen/MtG/
```

A single test:

```sh
~/llvm-project/build/bin/llvm-lit -v llvm/test/CodeGen/MtG/branch.ll
```

`-v` prints `PASS:` / `FAIL:` lines and, for failures, the FileCheck diff.

### What each test asserts

Each `.ll` declares an expected asm pattern with `; CHECK:` directives that
FileCheck verifies against `llc -mtriple=mtg`'s stdout. The pattern is
deliberately loose — usually one or two key mnemonics per test plus a
`CHECK-NOT:` for the pseudo it should *not* leave in the output. Examples:

| File | Covers |
|------|--------|
| `arith.ll` | add / sub / mul / srem |
| `bitwise.ll` | and / or / xor (asserts no `*_MACRO` leaks) |
| `branch.ll` | if-then-else, counted loop (with non-zero NumBuild offsets), nested |
| `call.ll` | external function call |
| `frame.ll` | multi-arg function (stack passing) |
| `input.ll` | `__mtg_input_a` / `__mtg_input_b` builtins |
| `logic.ll` | bitwise via icmp + zext |
| `neg.ll` | unary negation, sub via NEG path |
| `output.ll` | `__mtg_output` builtin |
| `ret.ll` | return constant / arg |
| `sdiv.ll` | signed division (custom lowering) |
| `select.ll` | branchless select |
| `shift.ll` | shl / lshr / ashr immediates |
| `frame-emergency-slot.ll` | prologue/epilogue reserves 4 extra bytes for scavenger emergency slot |
| `call.ll` | `CALL_PSEUDO` lowers to 2 NumBuild placeholders + `CallFwd target` |
| `call-stack-arg.ll` | stack-only ABI stores arg in caller's frame, not a register |

### Updating tests after intentional asm changes

Whenever a CodeGen change shifts the asm in a way the existing CHECK lines
don't tolerate, update the `; CHECK:` directives by hand — keep them loose
enough to survive register-allocator and scheduler shuffles, but strict
enough that the lowering's intent is still captured (e.g. specific mnemonic
sequences, `CHECK-NOT:` for leaking pseudos).

## Layer 2: clang frontend lit tests

### Location

| File | Covers |
|------|--------|
| `clang/test/CodeGen/MtG/target-layout.c` | clang's data layout string matches `MtGTargetMachine::computeDataLayout` |
| `clang/test/CodeGen/MtG/builtins.c` | `mtg.h` declarations are plumbed through and produce the extern symbols `MtGTargetLowering::LowerCall` looks for |
| `clang/test/CodeGen/MtG/mtg-h-guard.c` | `mtg.h` errors out on non-MtG targets |
| `clang/test/Driver/mtg-freestanding.c` | driver injects `-ffreestanding` for `--target=mtg` |
| `clang/test/Preprocessor/mtg-predefines.c` | `MtG` / `__MtG__` / `__mtg__` macros |

Each file is gated with `// REQUIRES: mtg-registered-target`, so the whole
set is skipped automatically when MtG isn't in `LLVM_TARGETS_TO_BUILD`.

### Running

```sh
~/llvm-project/build/bin/llvm-lit -v \
    clang/test/CodeGen/MtG/ \
    clang/test/Driver/mtg-freestanding.c \
    clang/test/Preprocessor/mtg-predefines.c
```

If you change `clang/lib/Basic/Targets/MtG.*`, `clang/lib/Driver/ToolChains/MtG.*`,
or `clang/lib/Headers/mtg.h`, re-run these — they catch most regressions in
the clang → backend handshake.

## Layer 3: end-to-end runs via ursa

The lit tests confirm the asm *looks* right. The ursa simulator confirms it
*executes* right.

### What ursa is

`~/llvm-project/ursa/` is a small Python toolkit (assembler + simulator) for
the MtG ISA. It can read MtG asm, finalise jump offsets, simulate execution,
and print whatever the program outputs via `Output`. Its src directory:

```
~/llvm-project/ursa/src/
├── assembler.py   # parser, opcode map, label/jump fixup
├── simulator.py   # interpreter for the MtG instruction set
└── main.py        # entry point — load file, run, print output
```

ursa lives in its own git repo (`branch main`) — it's not part of the
LLVM tree. Treat it as an *aid*, not authoritative; the spec of record is
`~/mtgemu-claude/forge-details.md`. When ursa and the spec disagree
(`Divide`'s `flag` bit is one known case), trust the spec and consider
patching ursa.

### Running an end-to-end test

Starting from C source (preferred; exercises the full clang → llc path):

```sh
# 1. C → MtG asm in one step.
~/llvm-project/build/bin/clang --target=mtg -O0 -S program.c -o program.s

# 2. Run it on ursa.
python3 ~/llvm-project/ursa/src/main.py program.s
```

Or starting from hand-written IR (useful for pinning down a specific
lowering without clang in the loop):

```sh
~/llvm-project/build/bin/llc -mtriple=mtg < program.ll 2>/dev/null > program.s
python3 ~/llvm-project/ursa/src/main.py program.s
```

`main.py` prints a couple of housekeeping lines and then:

```
Output:
<characters output by the program via __mtg_output>
```

### Writing a runnable program

ursa starts execution at the top of the asm (no `_start` lookup), so the
test's entry point must be the first label. By convention I use `_start`
returning `void`.

From C via `mtg.h`:

```c
#include <mtg.h>

void _start(void) {
  __mtg_output(72);  // H
}
```

Or directly in IR:

```llvm
declare void @__mtg_output(i32)

define void @_start() nounwind {
  call void @__mtg_output(i32 72)   ; H
  ret void
}
```

Inputs (via `__mtg_input_a` / `__mtg_input_b`) prompt on stdin:

```sh
echo -e "65\n1" | python3 main.py /tmp/sum.s   # ⇒ Alice=65, Bob=1, Output: B
```

### Calling helper functions

The stack-only calling convention makes multi-function programs Just Work
once `_start` is defined:

```c
void __mtg_output(int);
int add_one(int x) { return x + 1; }
void _start(void) {
  __mtg_output(add_one(5) + 48);  // prints '6'
  __mtg_output(10);
}
```

ursa picks up `_start` as the entry, the `CallFwd` in `_start` is flipped
to `CallBwdR` by ursa's assembler (since `add_one` is laid out earlier),
and the distance NumBuilds get filled in automatically.

### Avoiding constant folding

LLVM aggressively folds constant expressions, so a literal `or i32 5, 32`
becomes `or i32 0, 37` and never exercises `OR_MACRO`. To force runtime
evaluation, gate values through something the IR optimiser can't see
through — `urem` against another constant works, as does a `phi` from a
loop:

```llvm
%runtime = urem i32 70, 100      ; constfold-resistant 70
```

### Caveats around ursa's behaviour

- `Divide`'s `flag` is set per ursa as `flag = (quot != 0)`; the MtG spec
  says `flag = (remainder > 0)`. Don't lean on this in tests.
- ursa uses Python unbounded ints, so 32-bit overflow doesn't wrap. The
  backend's `*_MACRO` expansions feed values through `REM_MACRO` against
  `1<<32` to simulate the wrap, which makes most arithmetic agree.
- `CallFwd` / `CallBwdR` / `Return` are modelled via a simple software
  return-address stack (see `simulator.py`). This matches the practical
  semantics ("pop PC, jump back") rather than the hardware spec's
  `PC += max(0, S - 3·Z')` formula — sufficient for simulation but not a
  bit-for-bit reproduction of the MtG stack mechanics.
- Execution starts at the `_start` label if defined; otherwise at
  instruction 0. This means function helpers may be emitted in any order.

### How inter-function Call distances are resolved

MtG Call is PC-relative, so the compiler needs to know the distance from
each call site to its callee. Rather than computing this in LLVM (which
would need a module-level MachineFunction pass — awkward in the legacy
PassManager), we follow the same approach RISC-V uses: LLVM emits a
placeholder and defers resolution to the assembler.

Concretely:
- `MtGExpandBranchPseudo` expands `CALL_PSEUDO` to two `NumBuild 0,0`
  placeholders followed by a `CallFwd target` mnemonic (always `CallFwd`
  regardless of actual direction).
- `ursa/src/assembler.py::fixup_jumps` detects `CallFwd` / `CallBwd` /
  `CallBwdR`, computes the target distance, flips `CallFwd → CallBwdR`
  when the target lies behind the call site (self-recursion-safe variant),
  and patches the two preceding NumBuilds with the magnitude. This mirrors
  exactly what ursa already does for Jumps.

Intra-function displacements (Jumps, Return's Z') are still resolved in
LLVM (`MtGBranchSelector`), because they don't cross function boundaries.

## Common workflow

When changing the backend or the clang frontend pieces:

```sh
# Build
cd ~/llvm-project/build
ninja -j4 clang llc mtg-resource-headers

# Lit regression (backend + clang)
~/llvm-project/build/bin/llvm-lit llvm/test/CodeGen/MtG/
~/llvm-project/build/bin/llvm-lit clang/test/CodeGen/MtG/ \
    clang/test/Driver/mtg-freestanding.c \
    clang/test/Preprocessor/mtg-predefines.c

# Pick a representative end-to-end program and run it.
~/llvm-project/build/bin/clang --target=mtg -O0 -S /tmp/loop_test.c \
    -o /tmp/loop_test.s
python3 ~/llvm-project/ursa/src/main.py /tmp/loop_test.s
```

Triggering the **emergency-spill scavenge path** in `eliminateFrameIndex`
deserves its own check after touching anything around register pressure or
the byte-wise spill macros. Force it on by temporarily editing
`MtGRegisterInfo.cpp`:

```cpp
// In the FI scavenge loop, change
if (LivePhys.available(MF.getRegInfo(), R)) {
// to
if (false && LivePhys.available(MF.getRegInfo(), R)) {
```

then rebuild and rerun the lit + ursa programs. All should still produce
correct output (the emergency code path is the only thing that runs).
Revert the edit when done.

## Inspecting compiled output

A few `llc` invocations that come up often:

```sh
# Final asm, no debug noise
llc -mtriple=mtg < foo.ll 2>/dev/null

# MachineIR after a specific pass — handy for debugging spill / scavenger
# behaviour
llc -mtriple=mtg -stop-after=finalize-isel < foo.ll 2>/dev/null
llc -mtriple=mtg -stop-after=postrapseudos < foo.ll 2>/dev/null
llc -mtriple=mtg -stop-after=block-placement < foo.ll 2>/dev/null

# Pass list
llc -mtriple=mtg -debug-pass=Arguments < foo.ll 2>&1 | head -1

# Spot pseudos that escaped expansion (usually a bug)
llc -mtriple=mtg < foo.ll 2>/dev/null | grep -E '_MACRO|_PSEUDO'
```
