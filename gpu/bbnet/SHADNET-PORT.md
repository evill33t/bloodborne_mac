# The Hunter's Dream client for macOS

Only `thd/` is retained from the inherited online integration. The unused shadPS4
shadNet client, server probes, networking libraries, glue and compatibility headers
have been removed. The Mac adapter at `gpu/metal/bbhost/network.cpp` supplies the
symbol resolver and runtime integration; `network_compat/` supplies its minimal
headers. Offline remains the default.

`thd/` adapts [droogie/bbhost](https://github.com/droogie/bbhost/tree/566184a74f071166299bb656c4af479e5bbe2771)
commit `566184a74f071166299bb656c4af479e5bbe2771`: `src/hle/net.cpp`,
`src/hle/np_matching2.cpp`, `src/hle/np_signaling.cpp`, the NP parts of
`src/hle/system.cpp`, and `src/net/{http,account,session,stun}.cpp`. These portions
are GPL-3.0-or-later; original copyright notices remain with the code.

The upstream Windows integration was assembled in
[Awakenchan/BloodBorneButPc, now LanternEngine](https://github.com/Awakenchan/LanternEngine).
The shared type header retains its shadPS4 GPL-2.0-or-later notices.

Local macOS adaptations use Darwin sockets, the Mac account/Keychain adapter,
libcurl and the runtime's guest-call bridge. Windows socket branches have been
removed. This source cleanup does not establish live authentication, summons or
cross-client compatibility; those still need end-to-end verification.
