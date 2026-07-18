// [YYIL] New file. C-linkage shims wiring wolfSSL's embedded porting hooks (see
// wolfssl_user_settings.h) to ArduPilot's own APIs, for the vendored/ChibiOS wolfSSL build only
// (SITL keeps using the system's pkg-config wolfssl + its own libc/filesystem, unaffected). Four
// hooks:
//   - CUSTOM_RAND_GENERATE_SEED -> hal.util->get_random_vals() (STM32 hardware TRNG when present)
//   - XTIME                     -> AP_RTC's GPS/system-synced UTC clock, for cert validity checks
//   - XFOPEN/XFREAD/XFSEEK/XFCLOSE (WOLFSSL_AP_FILESYSTEM, see the matching wc_port.h patch)
//                               -> AP_Filesystem, so certs uploaded by ap_cert_provisioner (via
//                                  MAVLink FTP, see that tool's own header comment) can be read
//                                  without baking per-device secrets into the firmware image
//   - wolfSSL_SetAllocators(wolfssl_ap_malloc, wolfssl_ap_free, nullptr) -> plain malloc()/free()
//                               -> [YYIL] 2026-07-18: WOLFSSL_NO_MALLOC (see user_settings.h) is
//                                  still needed to suppress wolfSSL_Realloc()'s realloc() call
//                                  (blacklisted project-wide, no safe replacement exists -- see
//                                  Tools/ardupilotwaf/chibios.py's wraplist), but it ALSO disables
//                                  wolfSSL_Malloc()'s plain malloc() fallback whenever no custom
//                                  allocator callback is registered -- confirmed via gdb: with no
//                                  callback, wolfSSL_Malloc() unconditionally returns NULL, so
//                                  wolfSSL_CTX_new() (needs to allocate the method struct) always
//                                  fails immediately, the whole DTLS/TLS handshake never starts,
//                                  and the "DDS" thread's main_loop() returns/exits right after
//                                  creation -- this is why it looked like the thread "never
//                                  existed" when checked moments later. plain malloc()/free() ARE
//                                  safe here even on real ChibiOS hardware: AP_HAL_ChibiOS's own
//                                  hwdef/common/malloc.c already REPLACES newlib's blacklisted
//                                  malloc/calloc/free with ArduPilot's own safe implementation
//                                  (only realloc() has no such replacement), so registering plain
//                                  malloc()/free() as wolfSSL's allocator callbacks is exactly as
//                                  safe as any other ArduPilot code calling malloc() directly.
#include "AP_DDS_config.h"

// WOLFSSL_USER_SETTINGS is only defined for the vendored/ChibiOS wolfSSL build (see
// libraries/AP_DDS/wscript) -- SITL uses the system's pkg-config wolfssl instead, with its own
// default RNG/time/filesystem handling, and never needs (or declares prototypes for) these hooks.
#if AP_DDS_ENABLED && defined(WOLFSSL_USER_SETTINGS)

#include <AP_HAL/AP_HAL.h>
#include <AP_RTC/AP_RTC.h>
#include <AP_Filesystem/AP_Filesystem.h>

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

extern const AP_HAL::HAL& hal;

extern "C" {

// Prototypes for the wolfSSL porting hooks defined below (see user_settings.h's XTIME/XGMTIME/
// CUSTOM_RAND_GENERATE_SEED/XFOPEN etc. macros, which reference these by name). wolfSSL's own
// headers have no reason to declare ArduPilot-specific hook names, so without these forward
// declarations SITL's stricter -Werror=missing-declarations flags the definitions below.
int wolfssl_ap_generate_seed(unsigned char* output, unsigned int sz);
time_t wolfssl_ap_time(time_t* tl);
struct tm* wolfssl_ap_gmtime(const time_t* timer, struct tm* result);
void* wolfssl_ap_fopen(const char* name, const char* mode);
int wolfssl_ap_fclose(void* file);
size_t wolfssl_ap_fread(void* buf, size_t sz, size_t amt, void* file);
int wolfssl_ap_fseek(void* file, long offset, int whence);
long wolfssl_ap_ftell(void* file);
void* wolfssl_ap_malloc(size_t size);
void wolfssl_ap_free(void* ptr);
void* wolfssl_ap_realloc(void* ptr, size_t new_size);

int wolfssl_ap_generate_seed(unsigned char* output, unsigned int sz)
{
    if (output == nullptr) {
        return -1;
    }
    return hal.util->get_random_vals(output, sz) ? 0 : -1;
}

// [YYIL] Falls back to a fixed recent-enough constant before GPS/RTC lock, rather than wolfSSL's
// own default fallback (Jan 1 2000), so a real cert's notBefore date doesn't spuriously fail
// validation on a cold boot with no time source yet.
// [YYIL] 2026-07-18: this floor must stay AFTER every cert's notBefore or wc_ValidateDate()
// rejects the whole chain at load_verify_locations() time (confirmed via gdb: the previous
// 2025-01-01 floor predated dtls_certs' 2026-07-01 notBefore, so wolfSSL_CTX_load_verify_locations
// failed immediately, before any handshake packet was ever sent -- easy to mistake for the
// WOLFSSL_NO_MALLOC hang this file's header comment describes, but a separate bug). Bump this
// forward whenever certs are regenerated further out; there is no way to make a hardcoded floor
// permanently safe.
time_t wolfssl_ap_time(time_t* tl)
{
    uint64_t utc_usec;
    time_t now;
    if (AP::rtc().get_utc_usec(utc_usec)) {
        now = static_cast<time_t>(utc_usec / 1000000ULL);
    } else {
        now = static_cast<time_t>(1782950400); // 2026-07-02T00:00:00Z, a floor, not a real clock
    }
    if (tl != nullptr) {
        *tl = now;
    }
    return now;
}

// [YYIL] Reentrant replacement for gmtime() (blacklisted project-wide -- non-reentrant global
// state, a real hazard here since this runs on AP_DDS's own thread). Pure calendar math, no OS
// dependency -- standard "civil calendar from days since epoch" algorithm (Howard Hinnant's
// well-known public-domain formulation), correct for the proleptic Gregorian calendar covering
// any date a real X.509 cert would ever use.
struct tm* wolfssl_ap_gmtime(const time_t* timer, struct tm* result)
{
    if (timer == nullptr || result == nullptr) {
        return nullptr;
    }

    int64_t secs = static_cast<int64_t>(*timer);
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }

    result->tm_hour = static_cast<int>(rem / 3600);
    result->tm_min = static_cast<int>((rem % 3600) / 60);
    result->tm_sec = static_cast<int>(rem % 60);
    // 1970-01-01 was a Thursday (weekday 4, Sunday=0).
    result->tm_wday = static_cast<int>(((days % 7) + 7 + 4) % 7);

    // civil_from_days: days since 1970-01-01 -> (year, month, day), 1-indexed month/day.
    int64_t z = days + 719468; // shift epoch to 0000-03-01
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint64_t doe = static_cast<uint64_t>(z - era * 146097);          // [0, 146096]
    const uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);          // [0, 365]
    const uint64_t mp = (5 * doy + 2) / 153;                               // [0, 11]
    const uint64_t d = doy - (153 * mp + 2) / 5 + 1;                       // [1, 31]
    const uint64_t m = mp + (mp < 10 ? 3 : -9);                            // [1, 12]
    const int64_t year = y + (m <= 2 ? 1 : 0);

    result->tm_year = static_cast<int>(year - 1900);
    result->tm_mon = static_cast<int>(m - 1);
    result->tm_mday = static_cast<int>(d);

    // day-of-year: days from Jan 1 of `year` to this date.
    static const int cumulative_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    int yday = cumulative_days[m - 1] + static_cast<int>(d) - 1;
    if (leap && m > 2) {
        yday += 1;
    }
    result->tm_yday = yday;
    result->tm_isdst = 0;

    return result;
}

// WOLFSSL_AP_FILESYSTEM's XFILE is a plain fd (AP_Filesystem's open() return type), stored via a
// pointer-sized cast so it fits wolfSSL's opaque "XFILE" handle slot without needing a wrapper
// struct or heap allocation.
void* wolfssl_ap_fopen(const char* name, const char* mode)
{
    (void)mode; // certs are always opened read-only here
    const int fd = AP::FS().open(name, O_RDONLY);
    if (fd < 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd + 1)); // +1 so fd==0 isn't nullptr
}

int wolfssl_ap_fclose(void* file)
{
    if (file == nullptr) {
        return 0;
    }
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(file)) - 1;
    return AP::FS().close(fd);
}

size_t wolfssl_ap_fread(void* buf, size_t sz, size_t amt, void* file)
{
    if (file == nullptr || buf == nullptr) {
        return 0;
    }
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(file)) - 1;
    const int32_t requested = static_cast<int32_t>(sz * amt);
    const int32_t got = AP::FS().read(fd, buf, requested);
    return (got > 0) ? static_cast<size_t>(got) : 0;
}

int wolfssl_ap_fseek(void* file, long offset, int whence)
{
    if (file == nullptr) {
        return -1;
    }
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(file)) - 1;
    return (AP::FS().lseek(fd, static_cast<int32_t>(offset), whence) >= 0) ? 0 : -1;
}

long wolfssl_ap_ftell(void* file)
{
    if (file == nullptr) {
        return -1;
    }
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(file)) - 1;
    return static_cast<long>(AP::FS().lseek(fd, 0, SEEK_CUR));
}

// [YYIL] 2026-07-18: malloc()/free() alone weren't enough -- DTLS fragment reassembly
// (DtlsMsgSet(), asn.c) needs wolfSSL_Realloc() to grow its buffer as later fragments of a large
// handshake message (e.g. a multi-fragment Certificate) arrive, and with no realloc callback
// registered it hit WOLFSSL_NO_MALLOC's "No realloc available" dead branch (memory.c) -- silently
// dropping the reassembly and stalling the handshake (confirmed via wolfSSL_SetLoggingCb() trace:
// the client kept re-sending its ClientHello+cookie forever, never advancing past the Certificate).
// A real realloc() is still blacklisted project-wide (Tools/ardupilotwaf/chibios.py, no safe
// replacement) and stays suppressed by WOLFSSL_NO_MALLOC -- this instead builds realloc's
// semantics out of the same plain malloc()/free() already proven safe here, via a small header
// prepended to every allocation recording its size (needed since our realloc callback isn't
// given the old size, unlike libc realloc()/malloc_usable_size()). malloc/free/realloc all share
// this one format, so mixing calls between them (as wolfSSL does internally) stays consistent --
// magic is a cheap corruption/double-free/wrong-pointer canary for development, checked on every
// free/realloc.
namespace {
constexpr uint32_t WOLFSSL_AP_ALLOC_MAGIC = 0x59594c31; // "YYL1"
struct WolfAllocHeader {
    uint32_t magic;
    size_t size;
};
} // namespace

void* wolfssl_ap_malloc(size_t size)
{
    if (size == 0 || size > SIZE_MAX - sizeof(WolfAllocHeader)) {
        return nullptr;
    }
    auto *header = static_cast<WolfAllocHeader*>(malloc(sizeof(WolfAllocHeader) + size));
    if (header == nullptr) {
        return nullptr;
    }
    header->magic = WOLFSSL_AP_ALLOC_MAGIC;
    header->size = size;
    return header + 1;
}

void wolfssl_ap_free(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }
    auto *header = static_cast<WolfAllocHeader*>(ptr) - 1;
    if (header->magic != WOLFSSL_AP_ALLOC_MAGIC) {
        // Corruption, double-free, or a pointer that didn't come from wolfssl_ap_malloc() --
        // AP_InternalError-worthy, but this is wolfSSL's own C allocator hook (no AP_HAL/logging
        // guarantees at this layer), so just refuse to act on it rather than freeing garbage.
        return;
    }
    header->magic = 0; // catch a double-free on the next call instead of silently corrupting
    free(header);
}

void* wolfssl_ap_realloc(void* ptr, size_t new_size)
{
    if (ptr == nullptr) {
        return wolfssl_ap_malloc(new_size);
    }
    if (new_size == 0) {
        wolfssl_ap_free(ptr);
        return nullptr;
    }

    auto *old_header = static_cast<WolfAllocHeader*>(ptr) - 1;
    if (old_header->magic != WOLFSSL_AP_ALLOC_MAGIC) {
        return nullptr;
    }
    const size_t old_size = old_header->size;

    void *new_ptr = wolfssl_ap_malloc(new_size);
    if (new_ptr == nullptr) {
        return nullptr; // old block is untouched, matching realloc()'s contract
    }
    memcpy(new_ptr, ptr, (old_size < new_size) ? old_size : new_size);
    wolfssl_ap_free(ptr);
    return new_ptr;
}

} // extern "C"

#endif // AP_DDS_ENABLED
