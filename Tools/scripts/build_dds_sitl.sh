#!/usr/bin/env bash
# Configure and build SITL (default: ArduCopter) with AP_DDS enabled, no DTLS
# (plain UDP transport, for use with plain Micro-XRCE-DDS-Agent on port 8888
# instead of dtls_custom_agent on port 9999).
#
# IMPORTANT: this only actually builds on checkouts where the DTLS/wolfSSL code
# has been stripped out of libraries/AP_DDS entirely (the "public" branch).
# On branches that still have it ("master"/ardupilotDDS), libraries/AP_DDS/
# wscript unconditionally vendors wolfSSL for the sitl board regardless of
# whether --enable-DDS-DTLS is passed (see project memory
# project_dds_dtls_tls_coding_status) - but wscript's configure() only sets up
# wolfSSL's include path when --enable-DDS-DTLS IS passed, so a plain
# `--enable-DDS` configure on master fails with "wolfssl/ssl.h: No such file or
# directory" even though the .pc file is present. There is no working
# "plain build" on master - use build_dds_dtls_sitl.sh there always (the
# ardupilot_dds.parm's DDS_DTLS_ENABLE=0 controls actual runtime behavior,
# independent of which build you used).
#
# Requirement not covered by this script (kept out of the ArduPilot tree on
# purpose -- see Tools/ardupilotwaf/cxx_checks.py):
#
#   microxrceddsgen on PATH, built from ArduPilot's own fork (matches
#   .github/workflows/test_dds.yml):
#     git clone --depth 1 --recurse-submodules --branch v4.7.1 \
#         https://github.com/ardupilot/Micro-XRCE-DDS-Gen.git
#     (cd Micro-XRCE-DDS-Gen && ./gradlew assemble)
#     export PATH="$PWD/Micro-XRCE-DDS-Gen/scripts:$PATH"
#
# Usage:
#   Tools/scripts/build_dds_sitl.sh [waf-target]
#   (waf-target defaults to "copter")

set -e

TARGET="${1:-copter}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../.."

# Auto-add the known install location if it's not already on PATH, so this is a genuine
# one-click script rather than requiring the caller to export PATH first every time.
if ! command -v microxrceddsgen >/dev/null 2>&1; then
    for candidate in "$HOME/Micro-XRCE-DDS-Gen/scripts" "$HOME/tools/Micro-XRCE-DDS-Gen/scripts"; do
        if [ -x "${candidate}/microxrceddsgen" ]; then
            export PATH="${candidate}:$PATH"
            break
        fi
    done
fi

if ! command -v microxrceddsgen >/dev/null 2>&1; then
    echo "ERROR: microxrceddsgen not found on PATH." >&2
    echo "See the header of this script for how to build ArduPilot's fork." >&2
    exit 1
fi

set -x

python3 waf configure --board sitl --enable-DDS
python3 waf "$TARGET" -j"$(nproc)"

set +x

echo
echo "Build complete: build/sitl/bin/$([ "$TARGET" = "copter" ] && echo arducopter || echo "$TARGET")"
