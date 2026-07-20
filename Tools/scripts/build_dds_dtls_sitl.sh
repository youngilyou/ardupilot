#!/usr/bin/env bash
# Configure and build SITL (default: ArduCopter) with AP_DDS + wolfSSL DTLS
# (X.509 mutual TLS, ECC) enabled. This is the SITL-only wolfSSL DTLS PoC
# for the AP_DDS UDP transport -- see libraries/AP_DDS/AP_DDS_UDP.cpp and
# AP_DDS_DTLS_x509.h.
#
# Requirements not covered by this script (kept out of the ArduPilot tree
# on purpose -- see Tools/ardupilotwaf/cxx_checks.py:check_wolfssl):
#
#   1. microxrceddsgen on PATH, built from ArduPilot's own fork (matches
#      .github/workflows/test_dds.yml):
#        git clone --depth 1 --recurse-submodules --branch v4.7.1 \
#            https://github.com/ardupilot/Micro-XRCE-DDS-Gen.git
#        (cd Micro-XRCE-DDS-Gen && ./gradlew assemble)
#        export PATH="$PWD/Micro-XRCE-DDS-Gen/scripts:$PATH"
#
#   2. wolfSSL built with DTLS support and discoverable via pkg-config
#      (ECC is on by default in current wolfSSL; only --disable-ecc turns
#      it off, and ECC is required for the X.509 certs this build uses):
#        git clone --depth 1 https://github.com/wolfSSL/wolfssl.git
#        (cd wolfssl && ./autogen.sh && \
#         ./configure --prefix="$HOME/.local" --enable-dtls && \
#         make -j"$(nproc)" && make install)
#        export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"
#
# Usage:
#   Tools/scripts/build_dds_dtls_sitl.sh [waf-target]
#   (waf-target defaults to "copter")

set -e

TARGET="${1:-copter}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../.."

# Auto-add the known install locations if not already set, so this is a genuine one-click
# script rather than requiring the caller to export PATH/PKG_CONFIG_PATH first every time.
if ! command -v microxrceddsgen >/dev/null 2>&1; then
    for candidate in "$HOME/Micro-XRCE-DDS-Gen/scripts" "$HOME/tools/Micro-XRCE-DDS-Gen/scripts"; do
        if [ -x "${candidate}/microxrceddsgen" ]; then
            export PATH="${candidate}:$PATH"
            break
        fi
    done
fi
if ! pkg-config --exists wolfssl 2>/dev/null && [ -f "$HOME/.local/lib/pkgconfig/wolfssl.pc" ]; then
    export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
fi

if ! command -v microxrceddsgen >/dev/null 2>&1; then
    echo "ERROR: microxrceddsgen not found on PATH." >&2
    echo "See the header of this script for how to build ArduPilot's fork." >&2
    exit 1
fi

if ! pkg-config --exists wolfssl 2>/dev/null; then
    echo "ERROR: wolfSSL not found via pkg-config." >&2
    echo "See the header of this script for how to build it with --enable-dtls." >&2
    echo "(If it's installed under a non-standard prefix, set PKG_CONFIG_PATH.)" >&2
    exit 1
fi

set -x

python3 waf configure --board sitl --enable-DDS --enable-DDS-DTLS
python3 waf "$TARGET" -j"$(nproc)"

set +x

echo
echo "Build complete: build/sitl/bin/$([ "$TARGET" = "copter" ] && echo arducopter || echo "$TARGET")"
