#!/usr/bin/env bash
# Bundle everything needed to continue the wolfSSL DTLS (X.509 mutual TLS)
# AP_DDS PoC development on another PC: the source changes as a patch, the new files,
# the build script, and the hard-to-set-up build dependencies (a wolfSSL
# build with DTLS support, and ArduPilot's own microxrceddsgen build) --
# so the receiving machine only needs to extract, apply the patch, and run
# the build script. No network access required on the new PC.
#
# This script itself does not modify the working tree: it uses
# `git add -N` (intent-to-add) scoped only to the new PoC files so they
# show up in the generated patch, then reverts that intent-to-add state
# when done.
#
# Env vars (all optional):
#   WOLFSSL_PREFIX   -- wolfSSL install prefix to bundle (default: $HOME/.local,
#                        must contain include/wolfssl and lib/libwolfssl*)
#   MICROXRCEDDSGEN_DIR -- built ardupilot/Micro-XRCE-DDS-Gen checkout to bundle
#                        (default: ../Micro-XRCE-DDS-Gen relative to this repo,
#                        falls back to $HOME/Micro-XRCE-DDS-Gen)
#   RELAY_SRC        -- optional path to the external dtls_xrce_relay.c test
#                        harness to include for convenience
#
# Usage:
#   Tools/scripts/package_dev_handoff.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

WOLFSSL_PREFIX="${WOLFSSL_PREFIX:-$HOME/.local}"
MICROXRCEDDSGEN_DIR="${MICROXRCEDDSGEN_DIR:-}"
if [ -z "$MICROXRCEDDSGEN_DIR" ]; then
    if [ -d "$REPO_ROOT/../Micro-XRCE-DDS-Gen" ]; then
        MICROXRCEDDSGEN_DIR="$REPO_ROOT/../Micro-XRCE-DDS-Gen"
    else
        MICROXRCEDDSGEN_DIR="$HOME/Micro-XRCE-DDS-Gen"
    fi
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
STAGE_DIR="build/dds_dtls_dev_package"
ZIP_PATH="build/dds_dtls_dev_package_${STAMP}.zip"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/deps"

# ---- 1. source patch (modified files + new files, nothing else in the repo) ----
CHANGED_FILES=(
    Tools/scripts/build_options.py
    Tools/ardupilotwaf/cxx_checks.py
    Tools/ardupilotwaf/boards.py
    libraries/AP_DDS/AP_DDS_Client.cpp
    libraries/AP_DDS/AP_DDS_Client.h
    libraries/AP_DDS/AP_DDS_UDP.cpp
    libraries/AP_DDS/AP_DDS_config.h
)
NEW_FILES=(
    libraries/AP_DDS/AP_DDS_DTLS_x509.h
    Tools/scripts/build_dds_dtls_sitl.sh
    Tools/scripts/package_dev_handoff.sh
)

git add -N -- "${NEW_FILES[@]}"
git diff -- "${CHANGED_FILES[@]}" "${NEW_FILES[@]}" > "$STAGE_DIR/ardupilot_dtls_changes.patch"
git reset -- "${NEW_FILES[@]}" >/dev/null

PATCH_LINES=$(wc -l < "$STAGE_DIR/ardupilot_dtls_changes.patch")
if [ "$PATCH_LINES" -eq 0 ]; then
    echo "WARNING: generated patch is empty -- working tree matches HEAD for these files?" >&2
fi

# ---- 2. bundled build dependencies ----
if [ -d "$WOLFSSL_PREFIX/include/wolfssl" ]; then
    mkdir -p "$STAGE_DIR/deps/wolfssl/include" "$STAGE_DIR/deps/wolfssl/lib/pkgconfig"
    cp -a "$WOLFSSL_PREFIX/include/wolfssl" "$STAGE_DIR/deps/wolfssl/include/"
    cp -a "$WOLFSSL_PREFIX"/lib/libwolfssl* "$STAGE_DIR/deps/wolfssl/lib/" 2>/dev/null || true
    # make the .pc file relocatable instead of pointing at this machine's $HOME
    sed "s#^prefix=.*#prefix=\${pcfiledir}/../..#" \
        "$WOLFSSL_PREFIX/lib/pkgconfig/wolfssl.pc" > "$STAGE_DIR/deps/wolfssl/lib/pkgconfig/wolfssl.pc"
    WOLFSSL_BUNDLED=1
else
    echo "WARNING: $WOLFSSL_PREFIX/include/wolfssl not found -- wolfSSL build NOT bundled." >&2
    WOLFSSL_BUNDLED=0
fi

if [ -d "$MICROXRCEDDSGEN_DIR/scripts" ] && [ -d "$MICROXRCEDDSGEN_DIR/share" ]; then
    mkdir -p "$STAGE_DIR/deps/Micro-XRCE-DDS-Gen"
    cp -a "$MICROXRCEDDSGEN_DIR/scripts" "$STAGE_DIR/deps/Micro-XRCE-DDS-Gen/"
    cp -a "$MICROXRCEDDSGEN_DIR/share" "$STAGE_DIR/deps/Micro-XRCE-DDS-Gen/"
    GEN_BUNDLED=1
else
    echo "WARNING: built microxrceddsgen not found under $MICROXRCEDDSGEN_DIR -- NOT bundled." >&2
    GEN_BUNDLED=0
fi

if [ -n "$RELAY_SRC" ] && [ -f "$RELAY_SRC" ]; then
    mkdir -p "$STAGE_DIR/relay"
    cp "$RELAY_SRC" "$STAGE_DIR/relay/dtls_xrce_relay.c"
    RELAY_NOTE="relay/dtls_xrce_relay.c included -- build with:
  gcc -O2 -o dtls_xrce_relay dtls_xrce_relay.c \$(pkg-config --cflags --libs wolfssl)"
else
    RELAY_NOTE="Test relay (dtls_xrce_relay.c) not included. It stands in for DTLS
termination in front of a plain Micro XRCE-DDS Agent; set RELAY_SRC to its
path when running this script to include it."
fi

# ---- 3. README ----
cat > "$STAGE_DIR/README.md" <<EOF
# AP_DDS wolfSSL DTLS (X.509 mutual TLS) -- dev handoff package

Built: ${STAMP}

## Setup on the new PC

1. Clone/checkout ArduPilot at the same commit this patch was generated
   from, then from the repo root:

       git apply /path/to/ardupilot_dtls_changes.patch

2. Install the bundled build dependencies (avoids rebuilding wolfSSL /
   Micro-XRCE-DDS-Gen from source):

$(if [ "$WOLFSSL_BUNDLED" = 1 ]; then echo "       cp -r deps/wolfssl/* \$HOME/.local/          # or any prefix; adjust PKG_CONFIG_PATH below to match
       export PKG_CONFIG_PATH=\$HOME/.local/lib/pkgconfig:\$PKG_CONFIG_PATH"; else echo "       (wolfSSL was NOT bundled in this package -- build it yourself,
        see the header comment in Tools/scripts/build_dds_dtls_sitl.sh)"; fi)

$(if [ "$GEN_BUNDLED" = 1 ]; then echo "       cp -r deps/Micro-XRCE-DDS-Gen \$HOME/
       export PATH=\$HOME/Micro-XRCE-DDS-Gen/scripts:\$PATH"; else echo "       (microxrceddsgen was NOT bundled in this package -- build it yourself,
        see the header comment in Tools/scripts/build_dds_dtls_sitl.sh)"; fi)

3. Provision certificates (ECC/P-256, not RSA -- see AP_DDS_DTLS_x509.h)
   at the paths it expects (default: certs/ca.crt, certs/client.crt,
   certs/client.key relative to the SITL binary's working directory),
   or override the paths with -D compiler defines.

4. Build:

       Tools/scripts/build_dds_dtls_sitl.sh copter

${RELAY_NOTE}

## What changed

See ardupilot_dtls_changes.patch for the full diff. Summary:
  - libraries/AP_DDS/AP_DDS_UDP.cpp    -- DTLS handshake + wolfSSL_read/write
  - libraries/AP_DDS/AP_DDS_Client.h   -- wolfSSL session members, DTLS_ENABLE param
  - libraries/AP_DDS/AP_DDS_config.h   -- AP_DDS_DTLS_ENABLED flag (SITL-only)
  - libraries/AP_DDS/AP_DDS_Client.cpp -- DDS_DTLS_ENABLE var_info entry
  - libraries/AP_DDS/AP_DDS_DTLS_x509.h -- CA/client cert/key path constants (new file)
  - Tools/ardupilotwaf/cxx_checks.py   -- check_wolfssl() pkg-config check
  - Tools/ardupilotwaf/boards.py       -- wires the wolfSSL check into SITL configure
  - Tools/scripts/build_options.py     -- registers --enable-DDS-DTLS
  - Tools/scripts/build_dds_dtls_sitl.sh -- build script (new file)

This is a SITL-only proof of concept -- no ChibiOS/firmware port yet.
EOF

(cd "$(dirname "$STAGE_DIR")" && zip -r "$(basename "$ZIP_PATH")" "$(basename "$STAGE_DIR")" >/dev/null)

echo "Package written to: $ZIP_PATH"
echo "  wolfSSL bundled:          $([ "$WOLFSSL_BUNDLED" = 1 ] && echo yes || echo NO)"
echo "  microxrceddsgen bundled:  $([ "$GEN_BUNDLED" = 1 ] && echo yes || echo NO)"
echo "  patch lines:              $PATCH_LINES"
