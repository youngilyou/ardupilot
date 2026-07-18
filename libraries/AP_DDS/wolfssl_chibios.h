// [YYIL] New file. wolfssl/wolfcrypt/settings.h's WOLFSSL_CHIBIOS block does
// `#include "wolfssl_chibios.h"`, expecting the file ChibiOS's own upstream distribution
// normally bundles alongside wolfSSL. AP_HAL_ChibiOS (ArduPilot's fork/subset) doesn't carry that
// file, and none of its usual content is needed here anyway -- every actual porting hook
// (RNG seed, time, AP_Filesystem-backed cert I/O, custom send/recv) is already provided
// explicitly in user_settings.h and AP_DDS_wolfssl_ap_filesystem.cpp. This file only exists so
// the #include resolves; WOLFSSL_CHIBIOS itself is set purely to satisfy the negative-checks
// elsewhere in wolfSSL (e.g. ssl.h's `#elif !defined(WOLFSSL_CHIBIOS) ... #include <sys/uio.h>`)
// that skip POSIX-only code paths ChibiOS/newlib doesn't have.
#pragma once
