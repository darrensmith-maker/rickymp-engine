#!/usr/bin/env bash
set -euo pipefail

BOOST_PREFIX="$(brew --prefix boost)"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/arenamp-simpleweb.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

cat > "$WORK_DIR/simpleweb-smoke.cpp" <<'CPP'
#include "apps/master/SimpleWeb/http_server.hpp"

int main()
{
    SimpleWeb::Server<SimpleWeb::HTTP> server;
    server.config.port = 0;
    return 0;
}
CPP

"${CXX:-c++}" \
  -std=gnu++17 \
  -arch arm64 \
  -mmacosx-version-min="${MACOSX_DEPLOYMENT_TARGET:-15}" \
  -I"${GITHUB_WORKSPACE:-$PWD}" \
  -I"$BOOST_PREFIX/include" \
  -fsyntax-only \
  "$WORK_DIR/simpleweb-smoke.cpp"

echo "ArenaMP macOS: SimpleWeb headers compile with modern Boost.Asio."
