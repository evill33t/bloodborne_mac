// SPDX-License-Identifier: GPL-3.0-or-later
// bbhost's release public key and Ed25519 check; Mac builds are locally built plugins.
#include "host/updater.h"
#include "monocypher-ed25519.h"
namespace updater {
const std::uint8_t* release_key() {
    static constexpr std::uint8_t key[]{0xce,0x83,0x68,0x4e,0xe9,0x4e,0xc4,0xb9,0xc9,0x1c,0xf6,0x24,0x7a,0x23,0xd0,0x8f,
        0x50,0x1c,0xdd,0x7a,0x19,0x73,0x3c,0x83,0x48,0xca,0x3e,0xd6,0xae,0xa7,0xad,0x62};
    return key;
}
bool signature_ok(const std::string& bytes,const std::string& sig,const std::uint8_t key[32]) {
    return sig.size()==64 && crypto_ed25519_check(reinterpret_cast<const std::uint8_t*>(sig.data()),key,
        reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size())==0;
}
}
