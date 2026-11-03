#ifndef OPENMW_VERSION_HPP
#define OPENMW_VERSION_HPP

#define TES3MP_VERSION "0.8.1"
#define TES3MP_PROTO_VERSION 806

// Stable ArenaMP/TES3MP network compatibility identity.
// This is intentionally independent from the Git HEAD used to compile the build,
// so rebuilding client/server does not silently change their handshake identity.
#define TES3MP_COMPAT_COMMIT_HASH "0f659371bcbaf9e7e6b94bd6bcb7a81970082234"

// Official/vanilla TES3MP 0.8.1 build identity used by older servers.
// Source version file:
//   OpenMW 0.47.0
//   commit 68954091c54d0596037c4fb54d2812313b7582a1
//   tag    000e8724cacaf0176f6220de111ca45098807e78
#define TES3MP_VANILLA_OPENMW_VERSION "0.47.0"
#define TES3MP_VANILLA_PROTO_VERSION 10
#define TES3MP_VANILLA_COMMIT_HASH "68954091c54d0596037c4fb54d2812313b7582a1"
#define TES3MP_VANILLA_TAG_HASH "000e8724cacaf0176f6220de111ca45098807e78"

#define TES3MP_DEFAULT_PASSW "blankpassword"
#define TES3MP_MASTERSERVER_PASSW "12345"


#endif //OPENMW_VERSION_HPP
