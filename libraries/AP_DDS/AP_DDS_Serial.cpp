#include "AP_DDS_Client.h"

#if AP_DDS_ENABLED

#include <AP_SerialManager/AP_SerialManager.h>

#include <errno.h>

#if AP_DDS_SERIAL_TLS_ENABLED
#include <GCS_MAVLink/GCS.h>
#include "AP_DDS_DTLS_x509.h"

/*
  wolfSSL I/O callbacks, bridging TLS records to the open serial.port.

  [YYIL] New. TLS, not DTLS -- unlike AP_DDS_UDP.cpp's transport, serial is a continuous framed
  byte stream (see ddsSerialInit()'s uxr_set_custom_transport_callbacks(..., true, ...)), not a
  datagram transport, so TLS (designed for reliable ordered streams) is the correct protocol here,
  not DTLS. Same wolfSSL library, same certs (AP_DDS_DTLS_x509.h), same dtls_enable parameter --
  just a different wolfSSL_CTX_new() method and different underlying I/O.
 */
int AP_DDS_Client::serial_tls_send_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    (void)ssl;
    auto *dds = (AP_DDS_Client *)ctx;
    const size_t ret = dds->serial.port->write((const uint8_t*)buf, (size_t)sz);
    if (ret == 0) {
        return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }
    return (int)ret;
}

int AP_DDS_Client::serial_tls_recv_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    (void)ssl;
    auto *dds = (AP_DDS_Client *)ctx;
    const uint32_t tstart = AP_HAL::millis();
    while (AP_HAL::millis() - tstart < dds->serial.io_timeout_ms &&
           dds->serial.port->available() < (uint32_t)sz) {
        hal.scheduler->delay_microseconds(100);
    }
    const ssize_t ret = dds->serial.port->read((uint8_t*)buf, (uint16_t)sz);
    if (ret <= 0) {
        return WOLFSSL_CBIO_ERR_WANT_READ;
    }
    return (int)ret;
}

// [YYIL] 2026-07-18: see AP_DDS_wolfssl_ap_filesystem.cpp's header comment / AP_DDS_UDP.cpp's
// matching declaration -- without registering these, wolfSSL_Malloc() has no custom callback and
// WOLFSSL_NO_MALLOC (user_settings.h) makes it unconditionally return NULL. wolfssl_ap_realloc is
// needed for the same reason AP_DDS_UDP.cpp's DTLS path needs it -- TLS fragment reassembly can
// hit wolfSSL_Realloc() too.
#if defined(WOLFSSL_USER_SETTINGS)
extern "C" {
void* wolfssl_ap_malloc(size_t size);
void wolfssl_ap_free(void* ptr);
void* wolfssl_ap_realloc(void* ptr, size_t new_size);
}
#endif

/*
  run the (blocking) TLS handshake on an already-open serial.port. Safe to block here: this
  runs on the dedicated "DDS" thread, not the main vehicle scheduler loop.
 */
bool AP_DDS_Client::serial_tls_handshake()
{
    static bool wolfssl_initialized = false;
    if (!wolfssl_initialized) {
        wolfSSL_Init();
#if defined(WOLFSSL_USER_SETTINGS)
        if (wolfSSL_SetAllocators(wolfssl_ap_malloc, wolfssl_ap_free, wolfssl_ap_realloc) != 0) {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS allocator registration failed", msg_prefix);
            return false;
        }
#endif
        wolfssl_initialized = true;
    }

    serial.tls_ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
    if (serial.tls_ctx == nullptr) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS CTX_new failed", msg_prefix);
        return false;
    }

    // X.509 mutual TLS (ECC): verify the Agent's certificate against our CA, and present our own
    // certificate/key so the Agent can verify us too (same cert bundle as the UDP/DTLS path --
    // uploaded by ap_cert_provisioner onto AP_Filesystem, see AP_DDS_DTLS_x509.h).
    if (wolfSSL_CTX_load_verify_locations(serial.tls_ctx, AP_DDS_DTLS_CA_CERT_PATH, nullptr) != WOLFSSL_SUCCESS) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS CA cert load failed", msg_prefix);
        wolfSSL_CTX_free(serial.tls_ctx);
        serial.tls_ctx = nullptr;
        return false;
    }
    wolfSSL_CTX_set_verify(serial.tls_ctx, WOLFSSL_VERIFY_PEER, nullptr);

    if (wolfSSL_CTX_use_certificate_file(serial.tls_ctx, AP_DDS_DTLS_CLIENT_CERT_PATH, WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_use_PrivateKey_file(serial.tls_ctx, AP_DDS_DTLS_CLIENT_KEY_PATH, WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS client cert/key load failed", msg_prefix);
        wolfSSL_CTX_free(serial.tls_ctx);
        serial.tls_ctx = nullptr;
        return false;
    }

    wolfSSL_CTX_SetIOSend(serial.tls_ctx, serial_tls_send_cb);
    wolfSSL_CTX_SetIORecv(serial.tls_ctx, serial_tls_recv_cb);

    serial.tls = wolfSSL_new(serial.tls_ctx);
    if (serial.tls == nullptr) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS wolfSSL_new failed", msg_prefix);
        wolfSSL_CTX_free(serial.tls_ctx);
        serial.tls_ctx = nullptr;
        return false;
    }
    wolfSSL_SetIOReadCtx(serial.tls, this);
    wolfSSL_SetIOWriteCtx(serial.tls, this);

    constexpr uint32_t max_handshake_wait_ms = 10000;
    constexpr uint32_t per_read_timeout_ms = 500; // fixed -- no DTLS-style retransmit timer to derive it from
    const uint32_t deadline_ms = AP_HAL::millis() + max_handshake_wait_ms;
    int ret;
    int err;
    do {
        serial.io_timeout_ms = per_read_timeout_ms;
        ret = wolfSSL_connect(serial.tls);
        err = wolfSSL_get_error(serial.tls, ret);
    } while (ret != WOLFSSL_SUCCESS &&
             (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) &&
             AP_HAL::millis() < deadline_ms);

    if (ret != WOLFSSL_SUCCESS) {
        // [YYIL] see AP_DDS_UDP.cpp's dtls_handshake() for why this distinction (timeout vs a
        // real wolfSSL error code) is kept as a permanent log, not just for this session.
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS handshake timed out", msg_prefix);
        } else {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS handshake error %d", msg_prefix, err);
        }
        wolfSSL_free(serial.tls);
        wolfSSL_CTX_free(serial.tls_ctx);
        serial.tls = nullptr;
        serial.tls_ctx = nullptr;
        return false;
    }

    return true;
}
#endif // AP_DDS_SERIAL_TLS_ENABLED

/*
  open connection on a serial port
 */
bool AP_DDS_Client::serial_transport_open(uxrCustomTransport *t)
{
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
    AP_SerialManager *serial_manager = AP_SerialManager::get_singleton();
    auto *dds_port = serial_manager->find_serial(AP_SerialManager::SerialProtocol_DDS_XRCE, 0);
    if (dds_port == nullptr) {
        return false;
    }
    // ensure we own the UART
    dds_port->begin(0);
    dds->serial.port = dds_port;

#if AP_DDS_SERIAL_TLS_ENABLED
    if (dds->dtls_enable) {
        if (!dds->serial_tls_handshake()) {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s TLS handshake failed", msg_prefix);
            dds->serial.port = nullptr;
            return false;
        }
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "%s TLS handshake complete", msg_prefix);
    }
#endif

    return true;
}

/*
  close serial transport
 */
bool AP_DDS_Client::serial_transport_close(uxrCustomTransport *t)
{
#if AP_DDS_SERIAL_TLS_ENABLED
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
    if (dds->serial.tls != nullptr) {
        wolfSSL_shutdown(dds->serial.tls);
        wolfSSL_free(dds->serial.tls);
        dds->serial.tls = nullptr;
    }
    if (dds->serial.tls_ctx != nullptr) {
        wolfSSL_CTX_free(dds->serial.tls_ctx);
        dds->serial.tls_ctx = nullptr;
    }
#endif
    // we don't actually close the UART
    return true;
}

/*
  write on serial transport
 */
size_t AP_DDS_Client::serial_transport_write(uxrCustomTransport *t, const uint8_t* buf, size_t len, uint8_t* error)
{
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
    if (dds->serial.port == nullptr) {
        *error = EINVAL;
        return 0;
    }

#if AP_DDS_SERIAL_TLS_ENABLED
    if (dds->serial.tls != nullptr) {
        const int ret = wolfSSL_write(dds->serial.tls, (const char*)buf, (int)len);
        if (ret <= 0) {
            *error = EIO;
            return 0;
        }
        return (size_t)ret;
    }
#endif

    ssize_t bytes_written = dds->serial.port->write(buf, len);
    if (bytes_written <= 0) {
        *error = 1;
        return 0;
    }
    //! @todo populate the error code correctly
    *error = 0;
    return bytes_written;
}

/*
  read from a serial transport
 */
size_t AP_DDS_Client::serial_transport_read(uxrCustomTransport *t, uint8_t* buf, size_t len, int timeout_ms, uint8_t* error)
{
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
    if (dds->serial.port == nullptr) {
        *error = EINVAL;
        return 0;
    }

#if AP_DDS_SERIAL_TLS_ENABLED
    if (dds->serial.tls != nullptr) {
        const uint32_t deadline_ms = AP_HAL::millis() + (uint32_t)(timeout_ms > 0 ? timeout_ms : 0);
        int ret;
        int err;
        do {
            const uint32_t now_ms = AP_HAL::millis();
            const uint32_t remaining_ms = (now_ms < deadline_ms) ? (deadline_ms - now_ms) : 0;
            dds->serial.io_timeout_ms = (remaining_ms < 100) ? remaining_ms : 100;
            ret = wolfSSL_read(dds->serial.tls, (char*)buf, (int)len);
            err = wolfSSL_get_error(dds->serial.tls, ret);
        } while (ret <= 0 && err == WOLFSSL_ERROR_WANT_READ && AP_HAL::millis() < deadline_ms);

        if (ret <= 0) {
            *error = EAGAIN;
            return 0;
        }
        return (size_t)ret;
    }
#endif

    const uint32_t tstart = AP_HAL::millis();
    while (AP_HAL::millis() - tstart < uint32_t(timeout_ms) &&
           dds->serial.port->available() < len) {
        hal.scheduler->delay_microseconds(100); // TODO select or poll this is limiting speed (100us)
    }
    ssize_t bytes_read = dds->serial.port->read(buf, len);
    if (bytes_read <= 0) {
        *error = 1;
        return 0;
    }
    //! @todo Add error reporting
    *error = 0;
    return bytes_read;
}

/*
  initialise serial connection
 */
bool AP_DDS_Client::ddsSerialInit()
{
    // setup a framed transport for serial
    uxr_set_custom_transport_callbacks(&serial.transport, true,
                                       serial_transport_open,
                                       serial_transport_close,
                                       serial_transport_write,
                                       serial_transport_read);

    if (!uxr_init_custom_transport(&serial.transport, (void*)this)) {
        return false;
    }
    comm = &serial.transport.comm;
    return true;
}
#endif // AP_DDS_ENABLED