#!/usr/bin/env bash
# Fill deploy/docker/stage from a local build so the image has binaries, configs, and OpenSSL.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
OPENSSL="${OPENSSL:-/opt/openssl-4.0.0}"
STAGE="$ROOT/deploy/docker/stage"

rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/etc" "$STAGE/openssl"

cp "$BUILD/src/server/authserver/authserver" \
   "$BUILD/src/server/worldserver/worldserver" \
   "$BUILD/src/server/clustergateway/clustergateway" \
   "$STAGE/bin/"
chmod 755 "$STAGE/bin/"*

cp "$ROOT/src/server/authserver/authserver.conf.dist" \
   "$ROOT/src/server/worldserver/worldserver.conf.dist" \
   "$ROOT/src/server/clustergateway/clustergateway.conf.dist" \
   "$STAGE/etc/"

cp -a "$OPENSSL/." "$STAGE/openssl/"
echo "Staged image contents in $STAGE"
