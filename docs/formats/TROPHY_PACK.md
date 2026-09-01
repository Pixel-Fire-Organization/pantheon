# Format — Trophy pack (`TROPHY.TRP`)

A flat single-file container: one header, a table of entries, and the raw
payloads. It carries the trophy configuration and the trophy icons that the
console trophy service reads.

**Big-endian**, unlike every other format in this project — it is a Sony console
format inherited from the PlayStation 3, not one this engine designed. Reading it
with the host byte order produces plausible-looking nonsense, so byte order is
the first thing to check when a pack will not parse.

This layout is a contract between `tools/vita_package.py` and the console trophy
service. Runtime behaviour built on it is in
[subsystems/ACHIEVEMENT.md](../subsystems/ACHIEVEMENT.md); how packs are produced
and installed is in [vita/PACKAGING.md](../vita/PACKAGING.md).

> **This engine does not write this format.** The container is packed by TRPWork,
> vendored as a submodule, which owns the offsets, the payload alignment and the
> digest. The build supplies an entry table naming the payloads and the payloads
> themselves. A pre-built pack remains a first-class alternative. This spec exists
> so what TRPWork produces can be checked, not so it can be reimplemented.

## Layout

```
+-----------------------------+  offset 0x00
| header (0x40 B)             |  magic, version, file size, entry count,
|                             |  entry size, dev flag, SHA-1
+-----------------------------+  offset 0x40
| entry x entryCount          |  { name[0x24], offset, size, reserved }, 0x40 B each
+-----------------------------+  first entry offset
| payload 0                   |  in entry order
| payload 1                   |
| ...                         |
+-----------------------------+
```

### Header

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0x00 | `magic` | 4 | Container identifier, `0xDCA24D00` |
| 0x04 | `version` | 4 | Format version; **`2`** for PS3 and Vita. `3` is PS4 |
| 0x08 | `fileSize` | 8 | Total size of the container, 64-bit |
| 0x10 | `entryCount` | 4 | Number of entries |
| 0x14 | `entrySize` | 4 | Size of one entry, `0x40`, which is also where the table starts |
| 0x18 | `devFlag` | 4 | Zero in every pack observed, produced or retail |
| 0x1C | `sha1` | 20 | Digest over the container, this field zeroed |
| 0x30 | padding | 16 | Zero, to `0x40` |

### Entry

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0x00 | `name` | 0x20 | NUL-padded file name, no directory component |
| 0x20 | `offset` | 8 | Payload start, from the beginning of the container |
| 0x28 | `size` | 8 | Payload length in bytes |
| 0x30 | `type` | 4 | `1` for the configuration files, `0` for the icons |
| 0x34 | reserved | 12 | Zero |
| 0x38 | reserved | 8 | Zero |

`entrySize` is `0x40`, and the header is also `0x40`, so the entry table begins
immediately after the header. **These two numbers being equal is a coincidence,
not a rule** — one is the size of the header and the other is the size of one
entry. Third-party tools conflate them and work by luck; a reader should take the
table as starting at `0x40` and stride by `entrySize`.

## Where it goes

**Under a directory named after the communication identifier**, not directly in
the trophy directory:

```
sce_sys/trophy/<NPWR#####_00>/TROPHY.TRP
```

A pack one level up is not found at all, and what the console reports for a pack
it never found is that the **set is not registered** — the same thing it reports
for a pack it read and rejected. The two are indistinguishable from the outside
and have nothing to do with each other, so this is the first thing to check.

## Contents

| Name | Required | Meaning |
|---|---|---|
| `TROPCONF.SFM` | Yes | The trophy set: identifiers, grades, hidden flags |
| `TROP.SFM` | Yes | Localised names and descriptions |
| `ICON0.PNG` | Yes | The trophy-set icon |
| `TROP000.PNG` … | One per trophy | Per-trophy icons, numbered to match identifiers |

Trophy identifiers are dense and start at zero. A gap is a packaging error, not a
runtime condition — nothing at runtime can recover from an icon that is not
there.

The set icon is named `ICON0.PNG` inside the container whatever it is called on
disc, and is the set's own icon rather than one of the per-trophy ones. A set
that does not declare one is given the store-front icon, because the file is
required and an approximate icon is better than an absent one.

### The configuration files

Both are XML. `TROPCONF.SFM` is the set itself and `TROP.SFM` is the same set
with the display strings filled in; a reader may cross-check them, so **each
carries the communication identifier and the set version** rather than leaving
either to the other.

**Trophy identifiers are three digits** — `000`, not `0` — matching the icon
file names. This is settled by reading a configuration off a console that had
registered it, which is the only authority worth having here: the format is
undocumented, and reasoning about which form "must" be right produced the wrong
answer.

The root element carries a version and the platforms the set is valid for, the
parental level names the licence area it applies to, and every trophy declares
the platinum it contributes to. **A trophy that contributes names the platinum's
identifier; the platinum itself uses `-1`**, as does every trophy in a set that
has no platinum.

A configuration carries a signature as an XML comment ahead of the root element,
**320 hexadecimal characters**, before any XML declaration — there is none.
Nothing here can produce a genuine one, and a placeholder of the right length and
alphabet is written instead.

Its shape matters separately from its authenticity. A pack with no signature
element at all, one with a placeholder of the right length in the wrong alphabet,
and one with well-formed hexadecimal are each refused *differently*, which is how
the field's length and encoding were established.

### `TRPPARAM.INI` is not part of the pack

A **registered** set carries one, alongside the extracted configuration and
icons. A pack does not: the console writes it when it installs the set. It is
recorded here because finding it on a console and putting it in the container is
an easy mistake, and it was made here first.

Four keys, CRLF line endings and a byte order mark:

```
TROPSYSVER=1.0
NPCOMMID=<the communication identifier>
TROPAPPVER=1.0
LANG=1
```

## Encrypted and unencrypted packs

**Every pack read off a retail title is encrypted end to end.** Its first four
bytes differ from title to title, none of them the magic below, and the whole
file measures at the maximum eight bits of entropy per byte. Nothing in it can be
parsed with the layout above.

A pack this build produces is in the clear: the entry names are readable and the
configuration files are plain text. That is the intended arrangement, and the
flag at `0x18` is what declares it. **A pack that leaves the flag zero claims to
be the encrypted kind**, and a reader that believes it will decrypt what is
already plain and reject the result. The flag is therefore set deliberately, and
the digest recomputed after setting it, because the digest covers the header.

This is also the practical limit on authoring a set for an unsigned title: the
signature that binds a pack to a title can be disabled on the console, but no
tool here can produce the encrypted form, so an unsigned title's pack has to be
the unencrypted one.

## The magic value

`0xDCA24D00`, big-endian like the rest of the header. This is settled, and the
generator writes it without being told:

- A reader that **rejects** anything else — TRPWork reads the first four bytes
  big-endian and raises `Bad TRP Magic` on a mismatch. A reader that merely
  printed the field, as the extractors this document was first written against
  did, establishes nothing; one that refuses on a mismatch does.
- The PS3 and PS4 developer wikis document the same value for the same container,
  which is consistent with this being a format inherited rather than designed.

The value is a fact about the format, not a component: nothing third-party is
vendored or linked to obtain it, and the generator here remains the project's
own. Overriding it is possible for a reader that wants something else, but there
is no known reason to.

## Payload alignment

Payloads start on **sixteen-byte boundaries**, padded with zeros. This is not
visible from the layout above — it shows up only when a pack is repacked and the
offsets move — and it is one of the reasons the container is TRPWork's job. A
contiguous pack parses correctly and may still be wrong on hardware.

## Verified by round-trip

A generated pack has been round-tripped through TRPWork: extracted to its five
payloads and an entry table, repacked, and compared. Both the original and the
repack carry a self-consistent digest, and the only difference is the alignment
above. That exercises magic, version, count, table offset, entry names, offsets
and sizes against an independent implementation — one that refuses a bad magic
and a wrong version rather than printing them.

## Contents this build does not produce

A pack produced here holds the two configuration files, the set parameters, the
set icon and one icon per trophy. A retail set also carries group icons where it
uses groups, which nothing here does.

**A retail pack is encrypted.** Its container begins `0xED895CE2` rather than the
magic below, so it cannot be read with this layout and is no use as a reference
for what a pack contains. What *can* be read is the configuration a console
writes out when it registers a set, which is where everything above came from.

Two fields a retail configuration carries are also absent, for the same reason.
A set with a platinum links each contributing trophy to it, and a configuration
carries the communication signature that authenticates the set. Neither is
written, because the linkage semantics are unverified and no signature can
honestly be produced for an identifier that was declared rather than allocated.


## The set is installed by a system dialog

Registration is not a trophy-module call and not something installation does. The
running title opens a system dialog, which reads the pack out of the title and
installs the set. Two things about its parameter block have cost time and are
recorded here because neither is discoverable from the platform SDK, which ships
no header for the dialog at all:

- The block is **216 bytes**: a version, the common dialog block, the trophy
  context, an options word, and **128 bytes of reserved space**. A caller that
  reserves less passes a short object, and the dialog reads uninitialised memory
  past the end of it. This does not fail at the call — the dialog opens, reports
  success and runs — it fails later, as a system error with no relation to the
  cause.
- The context comes after the common dialog block, not before it, and the common
  dialog block carries the magic number every system dialog requires.

An independent reimplementation of the platform was the source for both, which is
worth recording as a method: where a vendor SDK ships no header, an emulator that
runs retail software is a better authority than a search result, because it is
checked against software that works.

## What a console does with a pack this build produces

**It refuses it, and the refusal is not about the pack.** This is recorded so the
next attempt starts from what was established rather than repeating it.

Tested on retail firmware 3.65 with the signature-check plugin loaded and
verified working:

| pack | outcome |
|---|---|
| this build's, no signature element | refused |
| the reference builder's, placeholder in the wrong alphabet | refused as corrupt |
| the reference builder's, well-formed hexadecimal | refused as corrupt |
| the same, marked as a development pack | refused as corrupt |
| **a genuine encrypted retail pack, under its own identifier** | **refused** |

A genuine pack failing is what settles it: no pack this build could author would
have worked, and the difference is the **title**, not the container.

What was ruled out, each by its own evidence:

- **The plugin.** With it removed, binding to the set fails earlier and
  differently. It is loaded and doing its job.
- **The engine.** A minimal standalone program reproduces the same three
  successes and the same refusal, with none of this engine present.
- **The network platform.** Bringing up the network, the platform base module and
  the platform *identity* — which neither implementation had done — all succeed,
  and change nothing.
- **Leftovers.** No trace of the set exists in either configuration store, the
  data store, the trophy database or the title's own directory.
- **The registration dialog.** It is drawn and serviced correctly, every frame,
  and refuses. Skipping it entirely produces the same refusal.

What remains is that a set must be **registered to the running title**, only the
system dialog registers one, and it accepts nothing this build can produce. The
error a title sees afterwards says only that *it* has no set — never whether one
exists on the console.

## Still unverified

**No genuine retail pack has been round-tripped**, because retail packs are
encrypted. The remaining unknowns are the meaning of the zero fields at `0x30`
and `0x38` in each entry, and how a set with a platinum links its trophies to
it.

**Nothing here has run on hardware.** Acceptance by the console trophy service is
unproven, and needs the player-installed plugin described in
[../vita/PACKAGING.md](../vita/PACKAGING.md) regardless.
