# GPU queue ownership and user-event correction, 24 October 2026

Two source defects were isolated while visible gameplay was unavailable. They
are correctness changes; no FPS gain or link to the guest allocator failure is
established. Original game files and master saves are unchanged.

## Graphics flip retirement

The command processor kept one shared, unsynchronized pending flip label.
Graphics NOP markers wrote it, but every graphics or compute ring consumed and
cleared it after its own job. A compute job could therefore retire graphics
state before presentation, or replace it with an unrelated NOP payload.

Markers now belong to the graphics ring. Only graphics decoding can set them,
and only the graphics worker retires them. All spans and recursive indirect
buffers of the graphics job still share the existing last-marker-wins rule.
The `job.after` presentation callback returns before label retirement is queued;
the existing identity-checked label store precedes its interrupt. Checkpoint
drain checks the per-ring state; no pending-marker snapshot format is added.

The new CPU fixture gates a graphics job after multiple spans and a nested IB,
finishes a competing compute job with/without a marker, and checks no premature
callback or store. It then checks simulated flip-status, label and interrupt
ordering, label-only/interrupt-only cases, and no carry into the next job.
Native and Rosetta runs pass. Both private old-source negative controls
deterministically reproduce premature retirement and intentionally exit 86.
This fixture uses a deferred CPU renderer and creates no GPU device.

Evidence: `out/metal-resource-2026-10-24/flip-retirement-review.md` and adjacent
`flip-retirement-*` logs, recipe and negative-control inputs.

## User-event payload

The macOS bridge passed the trigger pointer as the event's `data`, leaving
`udata` empty. Its old test asserted that discrepancy. The vendored syscall
used `udata`, but updated registration and readiness in separate lock sections
and could create an event on trigger.

The correction adds one locked `TriggerUser`: find an existing user event,
set its opaque `udata`, retain zero user-event `data`, then publish readiness.
Both entry paths use it. Triggering an absent/deleted event returns ENOENT;
the macOS handle bridge returns EBADF for a missing queue. Other filters keep
their existing data and registered payload behavior. This does not change the
queue's existing EV_CLEAR-style delivery policy.

The payload placement agrees with the primary
[shadPS4 TriggerUser implementation](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/kernel/equeue.h)
and its
[user-event syscall and accessor](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/kernel/equeue.cpp),
reviewed on 24 October. These sources corroborate the ABI distinction; they
do not establish a causal relationship to this port's observed failure.

The existing native/Rosetta integration fixture now checks opaque payload
delivery, nullptr/latest coalesced payload, a bounded asynchronous wait,
unregistered and removed events, invalid handles and independent graphics
filter payloads. Both architectures pass.

## Combined validation and remaining limits

Combined native game SHA-256:
`afa8860e602ebdaeb44189330a8718a659b3d2210aba48aa3dfaaccc6518b68a`.
Shader precompiler SHA-256 remains
`40f64638e3aded09d35767fcfe55e71faa5aac73f188401e7ed61cb25bd21a12`.
Frozen binaries and hashes are in `queue-ssd-runtime/` and
`queue-ssd-runtime-identity.json` under the evidence root.

| Validation | Result |
| --- | --- |
| Native combined build | Pass |
| Native flip, event/integration, msync, HTILE and foliage contracts/shader | 6/6 pass |
| Rosetta flip and event/integration | 2/2 pass |
| Native Metal consumed foliage and indexed/bounded texture coherence | 3/3 pass with device access |
| Retained original HTILE GPU shader and consuming read leases | Pass |
| Source/save inventory and executable hashes | Unchanged |
| Visible gameplay with these queue corrections | Pending: Mac locked |
| Matched native 1080p FPS comparison | Not performed for this binary |

Logs are `queue-native-{build,cpu-tests,gpu-tests-unrestricted,htile-gpu}.log`
and `queue-rosetta-{build,cpu-tests}.log`. The initial sandbox-only GPU attempt
skips; the subsequent device-enabled attempt executes the three tests.

Separately, compute-ring acknowledgement currently precedes consumption of
queued raw command spans. An owned top-level command snapshot before publishing
the read pointer is a follow-up design, not implemented here. Source provenance,
wrap order, mapping lifetime, failed admission and read-pointer identity need
independent tests. Queue deletion/listener lifetime is another audit lead.
Neither lead has a measured gameplay impact yet. Broad submit coalescing and
speculative arena enlargement remain unimplemented.
