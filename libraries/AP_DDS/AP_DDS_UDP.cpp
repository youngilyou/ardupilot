#include "AP_DDS_Client.h"

#if AP_DDS_UDP_ENABLED

#include <errno.h>

#if AP_DDS_DTLS_ENABLED
#include <GCS_MAVLink/GCS.h>
#include "AP_DDS_DTLS_x509.h"

/*
  wolfSSL I/O callbacks, bridging DTLS records to the connected UDP socket
 */
int AP_DDS_Client::dtls_send_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    (void)ssl;
    auto *dds = (AP_DDS_Client *)ctx;
    const ssize_t ret = dds->udp.socket->send(buf, sz);
    if (ret <= 0) {
        return (errno == EWOULDBLOCK || errno == EAGAIN) ? WOLFSSL_CBIO_ERR_WANT_WRITE : WOLFSSL_CBIO_ERR_GENERAL;
    }
    return (int)ret;
}

int AP_DDS_Client::dtls_recv_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    (void)ssl;
    auto *dds = (AP_DDS_Client *)ctx;
    const ssize_t ret = dds->udp.socket->recv(buf, sz, dds->udp.io_timeout_ms);
    if (ret <= 0) {
        return WOLFSSL_CBIO_ERR_WANT_READ;
    }
    return (int)ret;
}

// [YYIL] 2026-07-18: see AP_DDS_wolfssl_ap_filesystem.cpp's header comment -- without this,
// wolfSSL_Malloc() has no custom callback and WOLFSSL_NO_MALLOC (user_settings.h) makes it
// unconditionally return NULL, so wolfSSL_CTX_new() (needs to allocate the method struct) always
// fails and the handshake never even starts. Only declared here (not called directly otherwise),
// matching the other wolfssl_ap_* shims' forward-declaration pattern. wolfssl_ap_realloc is
// needed too -- without it, DTLS fragment reassembly (a large Certificate spanning multiple
// fragments) hits the same WOLFSSL_NO_MALLOC dead branch in wolfSSL_Realloc(), silently stalling
// the handshake instead of failing outright (confirmed via wolfSSL_SetLoggingCb() trace).
#if defined(WOLFSSL_USER_SETTINGS)
extern "C" {
void* wolfssl_ap_malloc(size_t size);
void wolfssl_ap_free(void* ptr);
void* wolfssl_ap_realloc(void* ptr, size_t new_size);
}
#endif

/*
  run the (blocking) DTLS handshake on an already-connected udp.socket.
  Safe to block here: this runs on the dedicated "DDS" thread, not the
  main vehicle scheduler loop.
 */
bool AP_DDS_Client::dtls_handshake()
{
    static bool wolfssl_initialized = false;
    if (!wolfssl_initialized) {
        wolfSSL_Init();
#if defined(WOLFSSL_USER_SETTINGS)
        if (wolfSSL_SetAllocators(wolfssl_ap_malloc, wolfssl_ap_free, wolfssl_ap_realloc) != 0) {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS allocator registration failed", msg_prefix);
            return false;
        }
#endif
        wolfssl_initialized = true;
    }

    udp.tls_ctx = wolfSSL_CTX_new(wolfDTLSv1_2_client_method());
    if (udp.tls_ctx == nullptr) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS CTX_new failed", msg_prefix);
        return false;
    }

    // X.509 mutual TLS (ECC): verify the Agent's certificate against our
    // CA, and present our own certificate/key so the Agent can verify us
    // too (matches dtls_custom_agent's client_ca_pem_path requirement).
    if (wolfSSL_CTX_load_verify_locations(udp.tls_ctx, AP_DDS_DTLS_CA_CERT_PATH, nullptr) != WOLFSSL_SUCCESS) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS CA cert load failed", msg_prefix);
        wolfSSL_CTX_free(udp.tls_ctx);
        udp.tls_ctx = nullptr;
        return false;
    }
    wolfSSL_CTX_set_verify(udp.tls_ctx, WOLFSSL_VERIFY_PEER, nullptr);

    if (wolfSSL_CTX_use_certificate_file(udp.tls_ctx, AP_DDS_DTLS_CLIENT_CERT_PATH, WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_use_PrivateKey_file(udp.tls_ctx, AP_DDS_DTLS_CLIENT_KEY_PATH, WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS client cert/key load failed", msg_prefix);
        wolfSSL_CTX_free(udp.tls_ctx);
        udp.tls_ctx = nullptr;
        return false;
    }

    wolfSSL_CTX_SetIOSend(udp.tls_ctx, dtls_send_cb);
    wolfSSL_CTX_SetIORecv(udp.tls_ctx, dtls_recv_cb);

    udp.tls = wolfSSL_new(udp.tls_ctx);
    if (udp.tls == nullptr) {
        GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS wolfSSL_new failed", msg_prefix);
        wolfSSL_CTX_free(udp.tls_ctx);
        udp.tls_ctx = nullptr;
        return false;
    }
    wolfSSL_SetIOReadCtx(udp.tls, this);
    wolfSSL_SetIOWriteCtx(udp.tls, this);

    // Bounded by wall-clock time, not attempt count: wolfSSL's DTLS
    // retransmit backoff grows per attempt, so a flat attempt cap can take
    // an impractically long time to give up on an unresponsive/wrong peer.
    constexpr uint32_t max_handshake_wait_ms = 10000;
    const uint32_t deadline_ms = AP_HAL::millis() + max_handshake_wait_ms;
    int ret;
    int err;
    do {
        const uint32_t now_ms = AP_HAL::millis();
        const uint32_t remaining_ms = (now_ms < deadline_ms) ? (deadline_ms - now_ms) : 0;
        const int timeout_s = wolfSSL_dtls_get_current_timeout(udp.tls);
        const uint32_t wolfssl_timeout_ms = (timeout_s > 0) ? (uint32_t)timeout_s * 1000U : 100U;
        udp.io_timeout_ms = (wolfssl_timeout_ms < remaining_ms) ? wolfssl_timeout_ms : remaining_ms;
        ret = wolfSSL_connect(udp.tls);
        err = wolfSSL_get_error(udp.tls, ret);
    } while (ret != WOLFSSL_SUCCESS &&
             (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) &&
             AP_HAL::millis() < deadline_ms);

    if (ret != WOLFSSL_SUCCESS) {
        // [YYIL] Kept permanently (not just for this session's diagnosis) -- distinguishing a
        // plain timeout (still WANT_READ/WRITE when the deadline hit -- no response, or one that
        // never fully arrived) from a real wolfSSL error code is the single most useful piece of
        // information for diagnosing a future handshake failure in the field, and costs nothing
        // once the handshake already failed.
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS handshake timed out", msg_prefix);
        } else {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS handshake error %d", msg_prefix, err);
        }
        wolfSSL_free(udp.tls);
        wolfSSL_CTX_free(udp.tls_ctx);
        udp.tls = nullptr;
        udp.tls_ctx = nullptr;
        return false;
    }

    return true;
}
#endif // AP_DDS_DTLS_ENABLED

/*
  open connection on UDP
 */
bool AP_DDS_Client::udp_transport_open(uxrCustomTransport *t)
{
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
    auto *sock = NEW_NOTHROW SocketAPM(true);
    if (sock == nullptr) {
        return false;
    }
    if (!sock->connect(dds->udp.ip.get_str(), dds->udp.port.get())) {
        return false;
    }
    dds->udp.socket = sock;

#if AP_DDS_DTLS_ENABLED
    if (dds->dtls_enable) {
        if (!dds->dtls_handshake()) {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "%s DTLS handshake failed", msg_prefix);
            delete dds->udp.socket;
            dds->udp.socket = nullptr;
            return false;
        }
        GCS_SEND_TEXT(MAV_SEVERITY_INFO, "%s DTLS handshake complete", msg_prefix);
    }
#endif

    return true;
}

/*
  close UDP connection
 */
bool AP_DDS_Client::udp_transport_close(uxrCustomTransport *t)
{
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
#if AP_DDS_DTLS_ENABLED
    if (dds->udp.tls != nullptr) {
        wolfSSL_shutdown(dds->udp.tls);
        wolfSSL_free(dds->udp.tls);
        dds->udp.tls = nullptr;
    }
    if (dds->udp.tls_ctx != nullptr) {
        wolfSSL_CTX_free(dds->udp.tls_ctx);
        dds->udp.tls_ctx = nullptr;
    }
#endif
    delete dds->udp.socket;
    dds->udp.socket = nullptr;
    return true;
}

/*
  write on UDP
 */
size_t AP_DDS_Client::udp_transport_write(uxrCustomTransport *t, const uint8_t* buf, size_t len, uint8_t* error)
{
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
    if (dds->udp.socket == nullptr) {
        *error = EINVAL;
        return 0;
    }

#if AP_DDS_DTLS_ENABLED
    if (dds->udp.tls != nullptr) {
        const int ret = wolfSSL_write(dds->udp.tls, buf, (int)len);
        if (ret <= 0) {
            *error = EIO;
            return 0;
        }
        return (size_t)ret;
    }
#endif

    const ssize_t ret = dds->udp.socket->send(buf, len);
    if (ret <= 0) {
        *error = errno;
        return 0;
    }
    return ret;
}

/*
  read from UDP
 */
size_t AP_DDS_Client::udp_transport_read(uxrCustomTransport *t, uint8_t* buf, size_t len, int timeout_ms, uint8_t* error)
{
    AP_DDS_Client *dds = (AP_DDS_Client *)t->args;
    if (dds->udp.socket == nullptr) {
        *error = EINVAL;
        return 0;
    }

#if AP_DDS_DTLS_ENABLED
    if (dds->udp.tls != nullptr) {
        const uint32_t deadline_ms = AP_HAL::millis() + (uint32_t)(timeout_ms > 0 ? timeout_ms : 0);
        int ret;
        int err;
        do {
            const uint32_t now_ms = AP_HAL::millis();
            const uint32_t remaining_ms = (now_ms < deadline_ms) ? (deadline_ms - now_ms) : 0;
            dds->udp.io_timeout_ms = (remaining_ms < 100) ? remaining_ms : 100;
            ret = wolfSSL_read(dds->udp.tls, buf, (int)len);
            err = wolfSSL_get_error(dds->udp.tls, ret);
        } while (ret <= 0 && err == WOLFSSL_ERROR_WANT_READ && AP_HAL::millis() < deadline_ms);

        if (ret <= 0) {
            *error = EAGAIN;
            return 0;
        }
        return (size_t)ret;
    }
#endif

    const ssize_t ret = dds->udp.socket->recv(buf, len, timeout_ms);
    if (ret <= 0) {
        *error = errno;
        return 0;
    }
    return ret;
}

/*
  initialise UDP connection
 */
bool AP_DDS_Client::ddsUdpInit()
{
    // setup a non-framed transport for UDP
    uxr_set_custom_transport_callbacks(&udp.transport, false,
                                       udp_transport_open,
                                       udp_transport_close,
                                       udp_transport_write,
                                       udp_transport_read);

    if (!uxr_init_custom_transport(&udp.transport, (void*)this)) {
        return false;
    }
    comm = &udp.transport.comm;
    return true;
}
#endif // AP_DDS_UDP_ENABLED
