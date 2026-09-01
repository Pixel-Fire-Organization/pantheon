# Guidelines

Process documents. Read the relevant one **before** starting, not while
reviewing.

| Adding | Read |
|---|---|
| An engine subsystem — memory, io, archive, resource, level, sector, input, debug, renderer | [NEW_SYSTEM.md](NEW_SYSTEM.md) |
| A build target — a console, a desktop OS, a variant of either | [NEW_PLATFORM.md](NEW_PLATFORM.md) |
| A standalone example under `examples/` | [NEW_EXAMPLE.md](NEW_EXAMPLE.md) |

| Writing or checking | Read |
|---|---|
| Code that reads bytes off disc, completes later, runs on the IO worker, hands a buffer to an OS API, or drives a build or CI step | [SECURE_CODING.md](SECURE_CODING.md) |
| Whether the tree is free of that class of defect — after such a change, and as a periodic full pass | [SECURITY_REVIEW.md](SECURITY_REVIEW.md) |
| Code on the frame path — the loop, geometry staging, a backend's frame, the interface, IO dispatch, sector streaming, or a platform's clock, threads or console | [PERFORMANCE.md](PERFORMANCE.md) |
| Whether the tree obeys those rules and every platform spec's *Performance* section matches the platform as built — after such a change, as a periodic full pass, and before and after a platform is added | [PERFORMANCE_REVIEW.md](PERFORMANCE_REVIEW.md) |

The security and performance pairs are different in kind from the rest: in
each, one document is a set of rules a change obeys and the other a procedure
a pass follows. Both pairs exist because every rule in them was broken here
first — `docs/backlog/security_findings.md` and `EX-0005` onwards in
`docs/fixed_issues/issues.json` are the security record;
`docs/backlog/performance_findings.md` is the performance record, and the
platform specs' *Performance* sections carry the baselines it measures against.

The `NEW_*` guidelines follow the same four steps, for the same reason:

1. **Spec first.** Write the contract before the code. It is cheaper to move a
   dependency line than a dependency.
2. **Decompose the spec** into components and tasks, each traceable to a line of
   the spec. Anything with no spec line behind it is scope creep or a gap.
3. **Evaluate off-the-shelf components**, and record the decision — including
   the decision to write your own. This engine's constraints rule most libraries
   out; say which one applied.
4. **Implement per platform first, then engine-wide.** Anything built against a
   single platform encodes that platform's assumptions into its contract, and
   they are expensive to remove later.

Each guideline ends with a definition of done. Work it — the checklists exist
because every item on them has been missed at least once.
