# Backlog — security review findings

Working notes for future sessions, not a commitment made in the change that
added them. Findings from a full-codebase security pass, kept here until each
is either fixed (at which point it moves to
[fixed_issues/issues.json](../fixed_issues/issues.json)) or dismissed with a
reason recorded inline.

**Status.** Every finding from both passes was fixed on 2026-09-16 and moved to
[fixed_issues/issues.json](../fixed_issues/issues.json) as `EX-0005` through
`EX-0014`. Each entry there carries the symptom, the root cause, what changed
and how to check it has not come back; each also names the spec its fix
corrected, since several of these findings contradicted a guarantee a spec was
stating. Nothing from either pass is outstanding.

**Verification pass, 2026-09-16.** Every fix was re-read against the source
after the fact, check by check against its entry's `fix_summary`. All ten hold.
The pass found one latent gap the fixes had walked past — the far-field chunk
was published with only its span checked, and the cooker could emit clusters
short of frames — fixed as `EX-0015`; corrected the build-script relay, whose
quoting protected the path but not the command substitution around it, so a
checkout path with a space still failed (folded into `EX-0014`); and brought
two declarations in the resource header that still described LRU eviction into
line with `EX-0010`. It also added the "what a reader must establish" sections
`EX-0007` owed the model and texture format specs. The rules that would have
prevented each of these are now
[guidelines/SECURE_CODING.md](../guidelines/SECURE_CODING.md), and the
procedure this pass followed is
[guidelines/SECURITY_REVIEW.md](../guidelines/SECURITY_REVIEW.md); the next
pass starts from those and from this file.

What is left below is the audit record: what was examined and found sound, so a
later pass starts from there rather than re-deriving it, and the trust boundary
the whole exercise was scoped against.

## Scope and trust boundary

The engine's only user-writable inputs are the achievement record and the
action overlay, and both loaders are tight: magic, version, entry-count cap,
digest and checksum are all checked before anything is copied. Neither was
involved in any finding.

The real exposure was the cooked asset path: the archive, level core, sector,
model and texture parsers trusted their own on-disc headers. A corrupt or
modified archive on disc, a loose-file dev build, or a repacked ISO turned into
out-of-bounds reads — and on PS2, DMA from arbitrary addresses. On console
targets this is a robustness and modding concern rather than a remote one,
which is why the fixes refuse and report rather than sanitise.

The second pass added two more classes, both since closed: lifetime races
between the IO worker and the main thread's teardown paths, and places where
the code wrote live state before a spec said validation was complete.

## Checked, no finding

Recorded so the next pass can skip it rather than re-derive it.

- **Action overlay and achievement record** loaders: magic, version, entry
  count, map digest, checksum, action/binding indices and `sourceCount` are
  all validated before copy. Source *codes* inside an overlay are not validated
  at load, but every reader bounds them — button bits, key and mouse indices
  against their `Count` in each platform's input backend, stick and trigger
  likewise — so a crafted code reads as "not pressed".
- **Renderer texture uploads** (all eight backends): every size derives from
  `width * height` after the resource manager has capped both against the
  platform's `MaxTextureWidth/Height` and the TIM2 parser has bounded the
  level-0 span; on a 64-bit host the arithmetic cannot wrap. The 32-bit case
  was `EX-0007`, not a second defect.
- **Arena slot layout** can only overrun its block if the block base or a
  preceding arena size is not a multiple of the slot alignment. Every platform
  allocates the block aligned and every arena size is a multiple of 16 KB, so
  it holds today — by convention, not by a check in the segmented arena
  initialiser. Worth turning into a check if a platform is ever added with a
  different arena granularity.
- **Command line, Win32 `WM_CHAR` channel, PSP/Vita dialog text conversion, UI
  text-entry snapshots**: all copies are bounded, and control bytes are
  filtered at the platform boundary as `Platform.h` documents.
- **Format strings**: no logging or formatting call takes a non-literal format;
  `game::Log` wraps its argument in `"%s"`.
- **wgpu-native fetch** is pinned by version *and* SHA-256, so the
  configure-time download is sound.
- **ECS generator** rejects non-identifier names before emitting C++;
  packaging tools invoke every subprocess as an argument vector, never a shell
  string.
- **Win32 `LoadLibraryA("dbghelp.dll")` / `("opengl32.dll")`** resolve from the
  application directory before System32. That directory already holds the
  implicitly linked `wgpu_native.dll`, so an attacker who can plant a DLL there
  controls the process regardless; not a separate finding.
- **Testbed asset browser** reads: every one goes through the archive span
  read, which bounds the request against the located entry, into a fixed-size
  local, so a corrupt offset yields a failed read and a placeholder rather than
  an overrun. Its grid and entity walks are capped by their own display limits.
- **Zero-dimension textures**: the TIM2 parser accepts a zero width or height,
  and every backend refuses to upload one, which fails the load the same way a
  refused upload does. No backend divides by a dimension before that check.

## Known gaps a later pass should weigh

Neither is a finding today; both are places where the current design accepts a
cost knowingly, and a later pass should confirm the trade is still the right
one rather than re-discovering it.

- **The per-platform log file bypasses the IO file-access semaphore.** The PSP
  and Vita console backends open and write `engine.log` through the vendor IO
  calls directly, outside the lock every other file access takes. That is what
  keeps logging usable from inside the archive and IO code paths that hold the
  lock — a logging call that took it would deadlock — but it does mean two
  threads can write the log concurrently.
- **Public resource handles still carry no generation.** They no longer need
  one, because nothing reuses a slot without an explicit unload (`EX-0010`).
  A stale handle used after its owner unloaded it is now an ordinary
  use-after-free-shaped programming error rather than a silent alias, and a
  generation on the public handle would turn it into a detected one. That is a
  larger change than this pass took on: the handle is a bare `int32_t` stored
  in cooked material records and level material tables.
- **OS package installs in CI are unpinned.** The build container is pinned by
  digest and the Python tools by exact version, but the `apk add` and
  `apt-get install` steps inside the jobs take whatever the distribution's
  repository serves that day. Pinning them is possible in principle and
  impractical here: Alpine drops superseded package versions from its
  repositories within weeks, so a pinned install starts failing on its own.
  The exposure is bounded by the digest-pinned base image and the read-only
  token those jobs run with; revisit if a job ever gains write scope.
