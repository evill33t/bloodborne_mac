# Paleblood analysis and verification reference

Source: [Yharnam-Hunters/Paleblood](https://github.com/Yharnam-Hunters/Paleblood),
commit `af97b90bb1e88853adfc04b8da2b3d7506516565` (2026-10-15).
License: **GPL-2.0-or-later**, retained in [LICENSE](LICENSE).

- `functions.csv`: upstream `symbols/functions.csv`, unchanged. These 32 tracked
  function extents are an analysis aid, not a complete function catalog. Upstream
  verification labels describe upstream evidence, not validation of this port.
- `scalar.h`: upstream `game/engine/scalar.h`, unchanged.
- `limiter.cpp`: adapted from `game/frame_timing/frame_limiter.cpp`. The harness
  injects its clock, wait-mode and notification destination instead of resolving
  guest globals/imports. It runs stock behavior only, with the port's existing
  `SprjFlipper` field names. The original arithmetic is retained.

Only the CPU verification target links the reference limiter. It installs no
game hooks. Production timing remains bbhost's; graphics remain direct Metal.
The Mac patch analyzer follows upstream `tools/patch_overlap.py`'s address/range
mapping approach, with this port's actual timing ownership and conflict rules.

The target's unrelocated loaded-image SHA-256 is
`071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a`.
Full-file hashes can differ between SELF conversion tools. No game bytes or
recorded inputs are included here.
