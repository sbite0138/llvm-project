# MtG backend testing

The MtG backend is exercised by two complementary test layers:

1. **`llvm-lit` CodeGen tests** that pin down the asm output for each lowering
   path (regression tests).
2. **End-to-end runs through the ursa simulator** that execute compiled
   programs and check their actual output (semantic tests).

Both feed off the same `llc` binary built from this tree.

## Build

The backend lives under `llvm/lib/Target/MtG/`. Configure CMake so that `MtG`
is in the targets-to-build list (the default LLVM build does this on this
branch). Then build `llc`:

```sh
cd ~/llvm-project/build
ninja -j4 llc
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

### Updating tests after intentional asm changes

Whenever a CodeGen change shifts the asm in a way the existing CHECK lines
don't tolerate, update the `; CHECK:` directives by hand — keep them loose
enough to survive register-allocator and scheduler shuffles, but strict
enough that the lowering's intent is still captured (e.g. specific mnemonic
sequences, `CHECK-NOT:` for leaking pseudos).

## Layer 2: end-to-end runs via ursa

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

```sh
# 1. Compile a .ll to MtG asm.
~/llvm-project/build/bin/llc -mtriple=mtg < program.ll 2>/dev/null > program.s

# 2. Run it on ursa.
cd ~/llvm-project/ursa/src
python3 main.py /tmp/program.s
```

`main.py` prints a couple of housekeeping lines and then:

```
Output:
<characters output by the program via __mtg_output>
```

### Writing a runnable program

ursa starts execution at the top of the asm (no `_start` lookup), so the
test's entry point must be the first label. By convention I use `_start`
returning `void`:

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
- `Call` to a real function (not `__mtg_output`/`__mtg_input_*`) isn't
  modelled. End-to-end tests must inline everything into a single function.

## Common workflow

When changing the backend:

```sh
# Build
cd ~/llvm-project/build
ninja -j4 llc

# Lit regression
~/llvm-project/build/bin/llvm-lit llvm/test/CodeGen/MtG/

# Pick a representative end-to-end program and run it.
~/llvm-project/build/bin/llc -mtriple=mtg < /tmp/loop_test.ll \
    2>/dev/null > /tmp/loop_test.s
cd ~/llvm-project/ursa/src
python3 main.py /tmp/loop_test.s
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
