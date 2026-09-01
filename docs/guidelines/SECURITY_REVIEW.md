# Guideline — security review

How to establish that the tree is free of the classes of defect
[SECURE_CODING.md](SECURE_CODING.md) describes. That guideline is the set of
rules a change obeys; this one is the procedure a pass follows to prove the
tree obeys them — whether the pass reviews one change or sweeps everything.

The full-codebase pass of 2026-09-16 and its verification are the model.
[backlog/security_findings.md](../backlog/security_findings.md) is its record:
the trust boundary it was scoped against, what it examined and found sound,
and what it accepted as a known gap. `EX-0005` through `EX-0015` in
[fixed_issues/issues.json](../fixed_issues/issues.json) are what it fixed,
each with a regression check. **A pass starts from that record**, so it spends
its time on what has changed rather than re-deriving what is already
established.

---

## 1. When to run one

**After any change to:**

- a reader of on-disc bytes — archive, asset, texture, model, font, theme,
  level core, sector, achievement record, action overlay — or the cooker that
  writes one
- anything that completes later: a load context, a callback, a generation
- the IO worker, any table it reads, or any shutdown or reset path
- a platform file, path or storage operation
- `tools/**`, a workflow, the composite action, or a requirements file

Review the change against section 3, and run the regression checks in
section 6 for every issues-log entry whose `files_changed` overlaps it.

**Periodically as a full pass, and always before a platform is added.** A new
platform is a new `size_t` width, a new alignment rule and a new set of OS
APIs, and each of those is a place a previously sound check can stop holding.
Run sections 2 through 7 in full.

## 2. Enumerate the surface

A pass that reviews what it remembers misses what it forgot. Enumerate
mechanically, from the tree, every time. These commands are the starting list;
walk every hit against section 3. They use ripgrep; `grep -rnE` takes the same
patterns.

Every raw read site — where bytes enter from disc:

```
rg -n "FileRead\(|Engine_Archive_ReadSync\(|Engine_IO_ReadAsync\(" engine/src
```

Every view cast over a buffer — where a pointer into those bytes is created:

```
rg -n "reinterpret_cast<(const )?[A-Za-z_0-9]+\*>\(" engine/src
```

Every product or sum over a file-chosen number — the wrap candidates:

```
rg -n "\* sizeof\(|static_cast<uint64_t>\(" engine/src/resources engine/src/level engine/src/graphics engine/src/ui
```

Every unbounded string function call. The expected result is no hits:

```
rg -n "\b(strcpy|strcat|sprintf|vsprintf|gets|scanf)\(" engine game examples
```

Every callback that indexes a table by a number it was handed earlier:

```
rg -n "userData|Context\*" engine/src/resources engine/src/core
```

Every place the IO worker touches state, and every lock guarding it:

```
rg -n "Engine_IO_AcquireFileAccess|Engine_IO_ReleaseFileAccess|SemaphoreWait|SemaphoreSignal" engine/src
```

Every shutdown and reset path, in order:

```
rg -n "Shutdown\(\)|Drain\(\)|Release\(\)|ResetRuntimeState" engine/src/core/EngineCore.cpp
```

Every OS buffer in platform code:

```
rg -n "char [a-zA-Z_]+\[(MAX_PATH|IO_FILE_MAX_PATH|[0-9]+)\]" engine/src/platform
```

Every dynamic-evaluation and shell construct in the tools. The expected result
is no hits outside a comment or docstring, and the tests are deliberately
included — the original finding was in one:

```
rg -n "\b(eval|exec)\(|shell=True|os\.(system|popen)\(" tools
```

Every fetch a workflow makes, with its pin:

```
rg -n "uses:|container:|image:|pip install|apk add|apt-get install" .github
```

## 3. Walk each hit against the checklist

For each read site and each view cast, answer from the code of the enclosing
reader, not from memory:

1. What is the size of the blob, and is every offset and length compared
   against it before the view is published?
2. Is each `offset + length` and `count * width` computed in `uint64_t` before
   the comparison? Would it still pass on a 32-bit `size_t` with the field at
   its maximum?
3. Is each offset a struct is cast at checked for the alignment of that struct?
4. Is each value checked at the width it is stored at, not the width it is
   read at?
5. Is each string terminated by the reader, and each string offset bounded
   against its table, before the first string function runs?
6. Is each count checked against its ceiling before anything is sized from
   it, and does the cooker enforce the same ceiling?
7. Does a failure refuse the whole blob and log the field, the value and the
   bound? Is anything clamped?
8. Is the view published only after every check — including views nothing
   consumes yet?

For each callback and each later completion:

9. Does it carry a generation of its target, and compare state and generation
   before writing?
10. Does every release path advance the generation?

For each worker-touched table and each teardown path:

11. Is every read of the table under the lock that writes it, for the whole
    operation? Is state re-read inside the lock?
12. Does every shutdown and reset path drain, stop the worker, wait for its
    acknowledgement, and only then release?

For each OS buffer:

13. Is its size the one the API documents?

For each tool and workflow hit:

14. Is every subprocess an argument vector? Is every unavoidable shell string
    quoted, including its substitutions? Is anything `eval`ed?
15. Is every action pinned by SHA, every container by digest, every package by
    version? Does every workflow have a least-privilege `permissions` block?
    Does any `run:` expand an event field?

A "no" to any question is a finding. Record it before fixing it (section 8).

## 4. Check each format against its spec

Every `docs/formats/<NAME>.md` has a "what a reader must establish" section.
For each format:

- Every bullet in the section has a check in the reader that implements it.
- Every check in the reader has a bullet in the section.
- Every refusal has a failure-mode entry in the owning subsystem spec.

A bullet without a check is a guarantee the spec makes and the code does not
keep; a check without a bullet is a guarantee the next port of the reader will
not know to keep. Both are findings. The 2026-09-16 pass found several of the
first kind: specs stating a validation the code did not perform.

## 5. Check the cooker refuses what the runtime refuses

For every ceiling and layout rule the runtime enforces — material count,
meshes per sector, chunk alignment, atlas count, frames per cluster — confirm
the tool that writes the format raises on the same condition. Content the
runtime refuses must not be producible by the build:

```
python3 -m pytest tools/tests -q
```

The suite asserts struct-size parity between the C headers and the Python
packers and the on-disc invariants of a cooked level. A ceiling added to one
side and not the other fails here.

## 6. Run the regression checks

Every entry from `EX-0005` onwards in the issues log carries a
`regression_check` naming the scene, the platform, the corruption to apply to
a *copy* of a cooked file, and the message to expect. Run every one whose
`files_changed` overlaps the change under review; run all of them in a full
pass.

**Run them on a 32-bit target as well as on Win32.** The width of `size_t` is
the whole point of half the checks, and a 64-bit host passes arithmetic that
wraps on the console. PS2 under the emulator (`tools/ps2/emu_capture.py`
captures the console log) or PSP is sufficient; the log line the entry names
must appear, and the process must not crash.

**Corrupt copies, never the cooked tree.** Edit a copy of the archive or asset
with a hex editor or a short script and point the build at it. The corruption
must be the one the entry names — one chunk offset shifted by one byte, one
vertex count set to a wrapping value, one header size set odd — so a pass
proves the specific check and not a coincidental one.

## 7. Run the static analysis the CI runs

The lint workflow runs cppcheck over the compile database with `--enable=all
--inconclusive`. On a change to a reader or a platform file, run it locally
from the same container image the workflow pins and read every new warning in
the files touched. The SARIF filter in the workflow drops locationless
results, so run it raw locally to see them all.

## 8. Record the outcome

Every finding goes to one of two places, in the same change:

- **Fixed** → an entry in `docs/fixed_issues/issues.json` (schema in
  `tools/schemas/fixed_issues.schema.json`) with the symptom, the root cause,
  what changed, the files, and a regression check that names a corruption and
  an expected message. The spec it corrected is updated in the same change.
- **Dismissed or accepted** → a paragraph in
  `docs/backlog/security_findings.md`: under "Checked, no finding" with what
  was examined and why it is sound, or under "Known gaps" with the trade being
  accepted and the condition that would reopen it.

Nothing is left in a review comment, a commit message or a chat transcript.
The next pass reads the backlog and the issues log; it does not read those.

## Definition of done

A pass is complete when:

- [ ] Every command in section 2 was run and every hit walked against section 3
- [ ] The "must establish" list of every format spec matches its reader both
      ways
- [ ] Every runtime ceiling has a cooker-side refusal, and the tool tests pass
- [ ] Every applicable regression check was run, on a 32-bit target as well as
      Win32, against a corrupted copy, and produced the named message
- [ ] Static analysis was run on the touched files and every new warning read
- [ ] Every finding is either an issues-log entry with its spec updated, or a
      backlog paragraph with its reason
- [ ] The status section of the backlog names the pass, its date and its scope
