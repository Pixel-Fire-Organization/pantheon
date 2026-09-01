# PS2 — the VU1 macro preprocessor (`masp`)

The repo carries a working build of `masp` at `tools/ps2/masp`, and the PS2 build
uses it in preference to the one in the toolchain. This explains why.

## The toolchain's own build crashes on every input

`masp` is the second of the VU1 preprocessing steps: the renderer library's VU1
sources pass through it for macro expansion before assembly. The copy shipped in
the PS2 toolchain **segmentation faults on any input at all**, including an empty
file, so the renderer library cannot be built and no PS2 binary can be linked.

It is not input-dependent and not a bad argument. The program starts, and
`--help` and `--version` both work — which is what makes the fault look like a
parsing problem when it is not.

## What is actually wrong

The program bundles its own `memmove`, which is written as:

```c
memmove (s1, s2, n) { bcopy (s2, s1, n); return s1; }
```

A modern compiler recognises `bcopy(src, dst, n)` as `memmove(dst, src, n)` and
rewrites it — inside the definition of `memmove` itself. The result calls itself
unconditionally:

```
<memmove>:  endbr64 / sub $0x8,%rsp / call <memmove>
```

That is unbounded recursion, so the first copy of any kind overflows the stack.
`--help` and `--version` survive only because neither copies memory.

This is a build fault, not a source fault: the same source compiles correctly
when the compiler is told not to make that substitution.

## The fix

Rebuilt from the toolchain's own source with `-fno-builtin`, which stops the
`bcopy` to `memmove` rewrite. Nothing in the source was changed.

The binary is **x86-64 Linux**, matching the only host configuration this project
builds from. A different host needs its own build; the build falls back to the
toolchain's `masp` when `tools/ps2/masp` is absent, which fails in the way
described above rather than silently producing something wrong.

## Provenance and licence

`masp` is part of openvcl, distributed under the GNU General Public License.
Redistribution is therefore permitted, unlike the shader compiler this project
deliberately does not carry. The corresponding source is the openvcl tree
installed with the PS2 toolchain; the only difference from an ordinary build of
it is the compiler flag above.

This is a workaround for a defect in someone else's build, kept here because a
broken toolchain component should not stop the project from building. The proper
home for the fix is upstream.
