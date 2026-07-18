// [YYIL] New file. wolfSSL embedded build configuration for AP_DDS's DTLS(UDP)/TLS(Serial)
// transports on real (non-SITL) flight controller hardware -- ChibiOS/ARM Cortex-M, no libc
// filesystem/sockets/threading assumptions. Included automatically via WOLFSSL_USER_SETTINGS
// (set in libraries/AP_DDS/wscript), bypassing wolfSSL's own configure/cmake entirely, matching
// wolfSSL's own documented bare-metal/RTOS integration pattern (see IDE/GCC-ARM/Header/
// user_settings.h upstream for the template this is based on).
//
// SITL keeps using the system's pkg-config-detected wolfssl (via apt), unaffected by this file --
// this header is only pulled in for the vendored/ChibiOS build (see AP_DDS_config.h). Some
// additional defines may be needed once this actually goes through a full compile -- this is a
// first-pass config, not a final one; missing-symbol/undefined-behavior errors from the real
// build are expected to drive a few more additions here.
#pragma once

// size_t needs to be visible before wc_port.h's function declarations (e.g. wc_FileLoad) that
// use it regardless of which filesystem backend gets selected below, and before struct iovec
// below.
#include <stddef.h>

// wolfSSL's optional wolfSSL_writev() helper (unused here -- AP_DDS's I/O is plain send/recv
// callbacks, never scatter/gather) is still declared+defined even under WOLFSSL_CHIBIOS, which
// skips <sys/uio.h> (not available on ChibiOS/newlib) -- without a real struct iovec, its
// prototype (ssl.h) and definition (ssl.c) each get their own distinct implicitly-scoped struct,
// which conflict. Provide the standard POSIX layout directly so both sides agree.
//
// __linux__ only fires for the native SITL test build (temporarily forced to also use this
// vendored path, see wscript) -- there, a real <sys/uio.h> with a real struct iovec genuinely
// exists (unlike ChibiOS/newlib) and something else in the build already pulls it in, so our own
// hand-rolled struct collides with it regardless of include-guard naming. Just use the real one
// there instead of redeclaring it.
#if defined(__linux__)
#include <sys/uio.h>
#else
#ifndef _SYS_UIO_H
#define _SYS_UIO_H
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#endif
#endif

// SEEK_SET/SEEK_CUR/SEEK_END are used directly (not via wolfSSL's own XSEEK_SET/XSEEK_END) by
// some wolfSSL sources, e.g. ssl_misc.c. Normally these come from <stdio.h>, but
// libraries/AP_HAL_ChibiOS/hwdef/common/stdio.h shadows the toolchain's real newlib stdio.h with
// ArduPilot's own minimal console-only shim that doesn't define them (no file I/O concept there)
// -- define them here directly instead, matching the standard POSIX values.
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

// wolfSSL's legacy `#define RNG WC_RNG` (backward-compat alias, random.h) collides with
// ChibiOS/STM32's own RNG peripheral register macro -- wolfSSL's own documented flag for exactly
// this situation.
#define NO_OLD_RNGNAME

// wolfSSL's own recognized RTOS target -- this is checked directly in many places throughout the
// codebase (not just wc_port.h's XTIME chain) to skip POSIX-assuming code, e.g. ssl.h's
// sys/uio.h include for wolfSSL_writev(). ChibiOS/newlib has no BSD sockets/POSIX I/O headers.
#define WOLFSSL_CHIBIOS

// Custom filesystem backend: certs are loaded from AP_Filesystem (SD card, uploaded by
// ap_cert_provisioner via MAVLink FTP -- see that tool's own header comment), not baked into the
// firmware image (per-device unique certs/keys, common firmware across devices). This adds a new
// #elif branch to wc_port.h's existing filesystem-backend chain (EBSNET/LSR_FS/MQX/etc already
// live there) -- see the matching patch in modules/wolfssl/wolfssl/wolfcrypt/wc_port.h and the
// XFOPEN/XFREAD/XFSEEK/XFCLOSE implementations in AP_DDS_wolfssl_ap_filesystem.cpp.
#define WOLFSSL_AP_FILESYSTEM

// No RTOS threading needed -- the whole XRCE session (and thus any TLS/DTLS handshake) already
// runs on AP_DDS_Client's own dedicated thread.
#define SINGLE_THREADED

// [YYIL] 2026-07-18: wolfSSL's own C sources (asn.c etc) call through the XTIME/XGMTIME macros
// below without ever seeing wolfssl_ap_time()/wolfssl_ap_gmtime()'s real C++ prototypes (those
// only live in AP_DDS_wolfssl_ap_filesystem.cpp). Plain-C implicit-int-return rules then kick in
// at those call sites: wolfssl_ap_gmtime()'s real `struct tm*` (64-bit) return got truncated to
// 32 bits and sign-extended back into a garbage pointer, crashing ValidateGmtime() with e.g.
// localTime=0xfffffffff6ffc790 instead of the real 0x00007ffff6ffc790 -- confirmed via gdb.
// wolfSSL's usual fix for this (TIME_OVERRIDES, see wc_port.h) doesn't work here: its own forward
// declaration is written assuming XTIME/XGMTIME are plain function names, and the macro-expansion
// of our wrapper macros inside that declaration produces invalid syntax (confirmed by trying it --
// "expected declaration specifiers... before '(' token" in every wolfSSL C source). Declaring the
// prototypes here instead sidesteps that: every wolfSSL source (C and C++) includes user_settings.h
// via settings.h before ever calling XTIME/XGMTIME, and extern "C" linkage matches the definitions'
// linkage in AP_DDS_wolfssl_ap_filesystem.cpp exactly.
#ifdef __cplusplus
extern "C" {
#endif
time_t wolfssl_ap_time(time_t* tl);
struct tm* wolfssl_ap_gmtime(const time_t* timer, struct tm* result);
#ifdef __cplusplus
}
#endif

// No POSIX time.h -- AP_DDS_wolfssl_ap_filesystem.cpp provides a custom XTIME (only used for
// certificate notBefore/notAfter validity checks), backed by AP::rtc()'s GPS/system-synced UTC
// clock when available, falling back to a fixed recent-enough constant before that's set (same
// graceful-degradation idea as wolfSSL's own TEST_BEFORE_DATE fallback, just a newer date so real
// certs' notBefore checks don't spuriously fail before GPS lock).
#define XTIME(tl) wolfssl_ap_time(tl)

// gmtime() is deliberately blacklisted project-wide (Tools/ardupilotwaf/chibios.py: "these
// functions use global state that is not thread safe" -- a real hazard, not a formality, since
// AP_DDS's TLS handshake runs on its own thread alongside everything else). Provide a reentrant
// XGMTIME (matches the signature wolfSSL itself documents for a custom override: struct tm*
// XGMTIME(const time_t*, struct tm*)) instead of letting it fall through to the default
// #define XGMTIME(c, t) gmtime((c)).
#define XGMTIME(c, t) wolfssl_ap_gmtime((c), (t))

// [YYIL] 2026-07-18: required alongside the custom XGMTIME above. wc_ValidateDateWithTime()
// (asn.c) only stack-allocates the "struct tm" it passes as XGMTIME's 2nd arg when NEED_TMP_TIME
// is defined; otherwise it passes NULL, relying on the default XGMTIME(c,t)->gmtime(c) ignoring
// t and using its own static internal storage. Our wolfssl_ap_gmtime() has no such static storage
// (deliberately, per the comment above -- a static buffer would be the same non-thread-safe
// global state gmtime() is blacklisted for) and requires a real caller buffer, so without this,
// every notBefore/notAfter check silently got NULL, ValidateGmtime() rejected it, and
// wolfSSL_CTX_load_verify_locations() failed immediately -- confirmed via gdb, easy to mistake
// for a cert/clock problem since the symptom (handshake never starts) looks identical to the
// WOLFSSL_NO_MALLOC issue described in AP_DDS_wolfssl_ap_filesystem.cpp's header comment.
#define NEED_TMP_TIME

// realloc() is also blacklisted project-wide ("prevent accidental use" -- heap fragmentation on
// an embedded target). Only wolfSSL_Realloc() (an optional public convenience wrapper AP_DDS
// never calls) uses it, and gracefully no-ops under this flag instead of requiring a full custom
// allocator (unlike WOLFSSL_NO_MALLOC's usual implication, this specific call site just needs the
// flag to skip straight to "unavailable" -- see memory.c's wolfSSL_Realloc()).
#define WOLFSSL_NO_MALLOC

// No /dev/urandom -- AP_DDS_wolfssl_ap_filesystem.cpp seeds wolfSSL's DRBG from
// hal.util->get_random_vals() (STM32 hardware TRNG when present, see AP_HAL_ChibiOS/Util.cpp).
#define NO_DEV_RANDOM
#define CUSTOM_RAND_GENERATE_SEED wolfssl_ap_generate_seed

// Custom I/O only -- AP_DDS_UDP.cpp/AP_DDS_Serial.cpp provide their own send/recv callbacks
// (wolfSSL_CTX_SetIOSend/SetIORecv), no BSD sockets assumed. WOLFSSL_USER_IO alone swaps out the
// I/O *functions*; WOLFSSL_NO_SOCK is the separate flag that skips wolfio.h's own
// <sys/socket.h>/<netinet/in.h> includes and socket-specific type/macro definitions entirely
// (ChibiOS/newlib has no BSD sockets headers).
#define WOLFSSL_USER_IO
#define WOLFSSL_NO_SOCK

// TLS 1.2 (Serial) / DTLS 1.2 (UDP) with ECC (P-256) client certs only, matching the existing
// SITL PoC's X.509 setup (see AP_DDS_DTLS_x509.h) -- no RSA (larger footprint), no TLS 1.3 (not
// needed, adds code size for no benefit here).
#define WOLFSSL_DTLS
#define HAVE_ECC
#define HAVE_TLS_EXTENSIONS
#define HAVE_SUPPORTED_CURVES
#define NO_OLD_TLS
#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_PSK
#define NO_MD5
#define NO_RC4
#define NO_DES3

// Smaller/faster ECC math for Cortex-M -- see wolfSSL's Cortex-M single-precision backend.
#define WOLFSSL_SP
#define WOLFSSL_SP_MATH
#define WOLFSSL_SP_SMALL
// [YYIL] 2026-07-18: WOLFSSL_SP_MATH (as opposed to WOLFSSL_SP_MATH_ALL) only supports the
// specific operations that have a dedicated SP-accelerated implementation -- everything else
// (generic mp_mulmod/mp_sqrmod etc, used by e.g. wc_ecc_point_is_on_curve()'s curve-equation
// check) silently fails with WC_KEY_SIZE_E instead of falling back to a working generic path.
// WOLFSSL_HAVE_SP_ECC is the switch that actually routes ECC operations (point-on-curve,
// ECDSA verify, ECDHE) through sp_c32.c's dedicated P-256 routines (sp_ecc_is_point_256() etc,
// compiled in by default -- opt-out only via WOLFSSL_SP_NO_256) instead of the unsupported
// generic path -- confirmed via gdb: without it, ConfirmSignature() during the DTLS handshake's
// Certificate verification failed inside wc_ecc_is_point() -> _ecc_is_point(), not because the
// key/point data was wrong (byte-for-byte confirmed correct), but because that generic math
// path has no working implementation under narrow WOLFSSL_SP_MATH.
#define WOLFSSL_HAVE_SP_ECC

// No wolfCrypt test/benchmark drivers, no directory-based cert store (opendir/readdir not
// available) -- both irrelevant on a flight controller and just cost flash.
#define NO_WOLFSSL_DIR
#define NO_MAIN_DRIVER
#define NO_ERROR_STRINGS

// [YYIL] NOT using WOLFSSL_SMALL_STACK: it requires either real malloc (unavailable --
// WOLFSSL_NO_MALLOC above, since realloc() specifically is blacklisted project-wide) or
// WOLFSSL_STATIC_MEMORY (a separate fixed-pool allocator, more setup than justified here).
// ECC/TLS handshake math is stack-heavy without it -- give AP_DDS_Client's own thread a generous
// stack size instead (see where it's created, AP_DDS_Client::start()).
