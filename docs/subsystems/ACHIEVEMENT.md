# Subsystem — Achievement

## Purpose

Let a game say "the player has done this" once, in its own vocabulary, and have
that recorded and shown — on every platform, whether or not the platform has any
such concept of its own.

This engine originally delegated the whole idea to the platform, on the
assumption that a console that has achievements is the right place to keep them.
Exactly one platform here has them, and it will not register a set for a title it
did not sign; the other two have nothing at all. Delegating therefore produced a
feature that worked nowhere. The record is now the engine's, and a platform that
has its own concept is somewhere to *also* send it.

## Contract

**Achievements are identified by enumerated value, never by name or index.**
Identifiers are generated from the declaration the whole build reads, so the set
a game can name and the set the engine knows are the same set by construction. A
game cannot unlock an achievement that was never declared.

**The declaration is platform-neutral and singular.** One description of the set
— identifiers, names, descriptions, grades, hidden flags, icons — is read by the
engine for display and by each platform's packaging for its own container. A
platform's packaging consumes that declaration; it does not own it. Two
descriptions of one set drift, and the drift is only discovered on the platform
that was not being tested.

**The engine holds the record.** Unlock state lives in the title's writable
storage and is the authority. This is a deliberate reversal: the previous
contract asked the platform every time, which is correct only where the platform
will answer, and on the platforms here it mostly will not.

**A platform with its own achievements is mirrored to, best effort.** An unlock
is forwarded where the platform has somewhere to put it. **A refusal to mirror is
not a refusal to unlock** — the engine's record stands, the game sees success,
and the refusal is reported once. This is what keeps a console feature that is
unavailable, degraded or discontinued from taking the game's achievements with
it.

**Unlocking is idempotent and one-way.** Unlocking an already-unlocked
achievement succeeds and changes nothing. There is no lock operation: the engine
does not take a player's achievement away, and a subsystem that could would
invite exactly that.

**Unlocking reports, and a refusal is logged once.** A caller that ignores the
result is not wrong — most games should. The common refusal is structural and
holds for the whole session, and a game may unlock on every frame a condition is
met, so the first refusal explains itself and later ones are silent. An unknown
identifier is the exception: that is a programming error, and each occurrence is
a distinct bug.

**Query is always safe.** Asking whether an achievement is unlocked answers false
rather than failing when the subsystem is not loaded, when storage is
unavailable, or when the identifier is out of range.

**What a title declares and what has been earned are separate facts.** The count
comes from the declaration and does not change because storage failed. Collapsing
the two makes a title that declares nothing indistinguishable from one whose
record could not be read, and those need opposite fixes.

**Unlocks outlive the session and the runtime.** The record survives a restart,
and survives the engine clearing its runtime state between scenes — it is not
engine state, it is the player's.

**The record is a fixed-size binary file in a location the platform supplies.**
Its layout is a format, specified in
[../formats/ACHIEVEMENT_RECORD.md](../formats/ACHIEVEMENT_RECORD.md), and is
identical on every platform. *Where* it lives is a platform question — a console
answers it differently from a desktop, and one of them may not have anywhere to
put it at all — and each platform's spec states its own answer.

The description of the set is text and the record of what was earned is binary.
That split is deliberate: the description is authored, diffed and reviewed by
people and is read once at build time, while the record is written by the engine
on a console during play and never read by anything else.

**The engine presents the unlock where the platform will not.** A player who
earns something is told, by the engine, through its own interface. Presentation
is not a platform courtesy that may or may not arrive.

**The game asks for the achievements screen; the engine does not bind it to an
input.** A browsable list of the set — earned and unearned, with names,
descriptions and tiers — is opened on request and closed when the player leaves
it. The engine binds no chord and reserves no button for it, because where that
screen belongs is a question about a game's menus, and a key the engine claimed
would be one the game could not use. The debug testbed reaches it the same way
any game does.

## Depends on

- **Filesystem** — the title's writable location, for the record. This is a new
  dependency and the reason the subsystem is no longer free-standing.
- **UI** — *optional*. Used to show an unlock and to draw the achievements
  screen. Absent, unlocks are still recorded and the screen cannot be opened.
- **Platform** — *optional*. The achievement contract, which a platform supplies
  only if it has one. A platform without achievements offers nothing at all
  rather than an implementation that always fails; the difference is what lets
  the subsystem distinguish "nowhere to mirror to" from "somewhere that refused".

## Depended on by

- Game code, through the public game API.
- The debug testbed, which exercises unlock, query and the notification path.

Nothing in the engine depends on it, which is what keeps it optional.

## Lifecycle

Started after the filesystem, and after the UI where that is present. Start reads
the record from writable storage; a record that is absent is not an error, it is
a player who has earned nothing yet. Start also binds to the platform's own
achievement support where there is any, and a failure to bind is recorded as
"nowhere to mirror" rather than failing start.

Per-frame work exists only while an unlock is being shown. With nothing to show,
the subsystem does nothing each frame.

Shutdown flushes the record if it changed. The record is also written at the
moment of an unlock, so a title that loses power between unlock and shutdown
keeps what was earned.

## When not loaded

Every unlock returns false and every query returns false. The engine runs
normally, and so does the game: a title that unlocks as it goes plays identically
with the subsystem absent, minus the recording. This is the configuration a
headless or automated host wants.

## Failure modes

- **Record absent** — treated as nothing unlocked. Not reported as a fault; it is
  the state of every new player.
- **Record unreadable or malformed** — treated as nothing unlocked, reported
  once, and overwritten on the next unlock rather than left to fail repeatedly.
  A corrupt record must not cost the player the ability to earn anything further.
- **Record unwritable** — unlocks succeed for the session and are reported as
  successful, because they are true; a single report says they will not survive a
  restart. The alternative, refusing to unlock because a disc is full, punishes
  the player for the wrong thing.
- **Unknown identifier** — refused and logged, every time.
- **Platform mirror refuses** — logged once with whatever reason the platform
  gave, and the unlock stands regardless.
- **Nothing to show it with** — where the UI is absent the unlock is recorded
  silently. Not a failure and not reported.

## Limits

- **No progress, no tiers, no counters.** An achievement is unlocked or it is
  not.
- **No unlock removal.** Deliberate, as above.
- **The set is fixed at build time.** No runtime registration.
- **No online synchronisation, no accounts, no leaderboards.** The record is
  local to the console and to the title. A player moving between devices does not
  bring earned achievements with them.
- **No merge with a platform's own achievements.** Where mirroring works, the two
  records exist side by side; the engine's is not reconciled against the
  console's, and a console that later disagrees is not corrected.
- **Notification is bounded and lossy.** A fixed number of unlocks can be queued
  for display; beyond that they are recorded and not shown. Unlocking a hundred
  achievements in one frame is a test case, not a design target.
- **The achievements screen is a screen, not a system overlay.** It is drawn
  where the game asks for it, in the game's frame, and it does not suspend the
  game beneath it or capture input the game did not give it.
- **No timestamps.** The record stores whether an achievement was earned, not
  when. Ordering a list by recency would cost more storage than the entire record
  currently occupies.

## Off-the-shelf evaluation

Two candidates were evaluated on hardware and both rejected.

**The console's own trophy system** was the original implementation and does not
work for a title the platform holder did not sign. A set must be registered to
the *running title*; only a system dialog registers one; and that dialog refuses
every container that can be produced without the platform holder's tooling —
including a genuine signed retail container, which is what establishes that the
container was never the obstacle. The full record of what was tried and what each
attempt eliminated is in [../formats/TROPHY_PACK.md](../formats/TROPHY_PACK.md).
The path is also strategically weak independent of that: it depends on an online
service that is being wound down, so a feature built on it would break later even
if it worked now.

**A third-party homebrew trophy ecosystem** was evaluated as a substitute for
that one platform, and rejected on three counts, any one of which is
disqualifying. It carries **no licence**, so there is no grant to redistribute or
modify it — the same reason this project already refuses to vendor a
redistributed vendor toolchain. Its server component is **not published**, so it
cannot be self-hosted, and depending on someone else's server reproduces the
weakness that ruled out the console path. And it is a **standalone application**
rather than a library, defining achievements in its own companion app and its own
vocabulary, which would put a third party's naming inside this engine's
interface.

Both share a deeper problem: each covers **one** of this engine's platforms. A
subsystem that works on one platform and reports "unsupported" on the rest is the
situation this design exists to end.

An engine-owned record is therefore the decision. It is a bounded amount of work
because the parts already exist — the contract, the generated identifiers, an
interface layer and a writable location — and what it buys is the same behaviour
on every platform, with no licence, no server and nothing to be discontinued.
