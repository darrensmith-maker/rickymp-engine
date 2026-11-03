#!/usr/bin/env bash
set -euo pipefail

# The source archive must contain CrabNet/RakNet in extern/raknet.  Some older
# CI commits accidentally checked out an empty/missing directory because RakNet
# was not registered as a real git submodule.  Keep the vendored copy when it is
# present, otherwise fetch the same CrabNet repository used by the legacy CI.
if [[ ! -f extern/raknet/CMakeLists.txt || ! -f extern/raknet/include/raknet/RakPeer.h ]]; then
  echo "extern/raknet is missing or incomplete; fetching TES3MP/CrabNet..." >&2
  rm -rf extern/raknet
  git clone --depth 1 https://github.com/TES3MP/CrabNet.git extern/raknet
fi

if [[ ! -f extern/raknet/CMakeLists.txt || ! -f extern/raknet/include/raknet/RakPeer.h ]]; then
  echo "CrabNet/RakNet is still incomplete after preparation." >&2
  exit 1
fi
