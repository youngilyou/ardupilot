#pragma once

// wolfSSL DTLS X.509 (ECC) certificate paths for the AP_DDS UDP transport
// (SITL PoC). These are file paths loaded at runtime, not embedded
// content -- private keys must never be committed to git. Point these at
// real PEM files before enabling DDS_DTLS_ENABLE; the defaults below are
// placeholders that do not exist by default.
//
// Must be ECC (P-256) certificates, not RSA: RSA (or untuned ECC) runs
// 3-8x wolfSSL's PSK-only RAM footprint, tuned ECC only ~2x -- see the
// PSK-vs-X.509 memory analysis behind this decision. RSA support is
// intentionally not wired up here.
//
// client cert/key are this vehicle's own identity, presented to the Agent
// for mutual TLS (matches dtls_custom_agent's client_ca_pem_path
// requirement, see DDS-Router/thirdparty/Micro-XRCE-DDS-Agent/security/).

#ifndef AP_DDS_DTLS_CA_CERT_PATH
#define AP_DDS_DTLS_CA_CERT_PATH "certs/ca.crt"
#endif

#ifndef AP_DDS_DTLS_CLIENT_CERT_PATH
#define AP_DDS_DTLS_CLIENT_CERT_PATH "certs/client.crt"
#endif

#ifndef AP_DDS_DTLS_CLIENT_KEY_PATH
#define AP_DDS_DTLS_CLIENT_KEY_PATH "certs/client.key"
#endif
