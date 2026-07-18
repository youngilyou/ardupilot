#pragma once

#include <AP_GPS/AP_GPS_config.h>
#include <AP_HAL/AP_HAL_Boards.h>
#include <AP_Networking/AP_Networking_Config.h>
#include <AP_VisualOdom/AP_VisualOdom_config.h>
#include <RC_Channel/RC_Channel_config.h>
#include <AP_RSSI/AP_RSSI_config.h>

#ifndef AP_DDS_ENABLED
#define AP_DDS_ENABLED 1
#endif

// UDP only on SITL for now
#ifndef AP_DDS_UDP_ENABLED
#define AP_DDS_UDP_ENABLED AP_DDS_ENABLED && AP_NETWORKING_ENABLED
#endif

// wolfSSL DTLS (X.509 mutual TLS, ECC) for the DDS UDP transport, and (separately) wolfSSL TLS
// for the DDS Serial transport (no Ethernet needed, so this is the only option on most real
// boards -- see AP_DDS_Serial.cpp). Two independent detections:
//   - HAVE_WOLFSSL: SITL only, via Tools/ardupilotwaf/cxx_checks.py:check_wolfssl's pkg-config
//     lookup against the host's own wolfssl-dev package.
//   - WOLFSSL_USER_SETTINGS: real (non-SITL) boards, wolfSSL built from the vendored
//     modules/wolfssl submodule per libraries/AP_DDS/wscript + user_settings.h (ChibiOS porting
//     hooks -- RNG/time/AP_Filesystem-backed cert loading -- in
//     AP_DDS_wolfssl_ap_filesystem.cpp). No longer a SITL-only PoC.
#ifndef AP_DDS_DTLS_ENABLED
#if defined(HAVE_WOLFSSL) || defined(WOLFSSL_USER_SETTINGS)
#define AP_DDS_DTLS_ENABLED AP_DDS_UDP_ENABLED
#else
#define AP_DDS_DTLS_ENABLED 0
#endif
#endif

// Serial+TLS doesn't need AP_NETWORKING_ENABLED/Ethernet (unlike UDP+DTLS above) -- only needs
// wolfSSL itself to be available.
#ifndef AP_DDS_SERIAL_TLS_ENABLED
#if defined(HAVE_WOLFSSL) || defined(WOLFSSL_USER_SETTINGS)
#define AP_DDS_SERIAL_TLS_ENABLED AP_DDS_ENABLED
#else
#define AP_DDS_SERIAL_TLS_ENABLED 0
#endif
#endif

#include <AP_VisualOdom/AP_VisualOdom_config.h>
#ifndef AP_DDS_VISUALODOM_ENABLED
#define AP_DDS_VISUALODOM_ENABLED HAL_VISUALODOM_ENABLED && AP_DDS_ENABLED
#endif

// Whether experimental interfaces are enabled.
#ifndef AP_DDS_EXPERIMENTAL_ENABLED
#define AP_DDS_EXPERIMENTAL_ENABLED 1
#endif

#ifndef AP_DDS_IMU_PUB_ENABLED
#define AP_DDS_IMU_PUB_ENABLED AP_DDS_EXPERIMENTAL_ENABLED
#endif

#ifndef AP_DDS_DELAY_IMU_TOPIC_MS
#define AP_DDS_DELAY_IMU_TOPIC_MS 5
#endif

#ifndef AP_DDS_TIME_PUB_ENABLED
#define AP_DDS_TIME_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_TIME_TOPIC_MS
#define AP_DDS_DELAY_TIME_TOPIC_MS 10
#endif

#ifndef AP_DDS_NAVSATFIX_PUB_ENABLED
#define AP_DDS_NAVSATFIX_PUB_ENABLED AP_GPS_ENABLED
#endif

#ifndef AP_DDS_STATIC_TF_PUB_ENABLED
#define AP_DDS_STATIC_TF_PUB_ENABLED AP_GPS_ENABLED
#endif

#ifndef AP_DDS_GPS_GLOBAL_ORIGIN_PUB_ENABLED
#define AP_DDS_GPS_GLOBAL_ORIGIN_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_GPS_GLOBAL_ORIGIN_TOPIC_MS
#define AP_DDS_DELAY_GPS_GLOBAL_ORIGIN_TOPIC_MS 1000
#endif

#ifndef AP_DDS_GEOPOSE_PUB_ENABLED
#define AP_DDS_GEOPOSE_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_GEO_POSE_TOPIC_MS
#define AP_DDS_DELAY_GEO_POSE_TOPIC_MS 33
#endif

// [YYIL] New. Raw MAVLink-over-filemsg passthrough on /vehicle_data/{from,to}_dds -- mirrors
// GCS_MAVLink's own byte stream over DDS (see AP_DDS_Client::publish_vehicle_data(), hooked from
// comm_send_buffer() in GCS_MAVLink.cpp) rather than the curated ROS2 sensor topics above.
#ifndef AP_DDS_VEHICLE_DATA_PUB_ENABLED
#define AP_DDS_VEHICLE_DATA_PUB_ENABLED AP_DDS_ENABLED
#endif

#ifndef AP_DDS_VEHICLE_DATA_SUB_ENABLED
#define AP_DDS_VEHICLE_DATA_SUB_ENABLED AP_DDS_ENABLED
#endif

// [YYIL] New. Separate topic from VEHICLE_DATA_SUB, deliberately -- MngData's Bin (dataflash log)
// download requests must never share a channel with real GCS commands, and must be dropped
// outright while armed (see AP_DDS_Client::on_topic()'s LOG_REQUEST_SUB case) so a log-download
// request can never compete with real-time control traffic mid-flight.
#ifndef AP_DDS_LOG_REQUEST_SUB_ENABLED
#define AP_DDS_LOG_REQUEST_SUB_ENABLED AP_DDS_ENABLED
#endif

#ifndef AP_DDS_LOCAL_POSE_PUB_ENABLED
#define AP_DDS_LOCAL_POSE_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_LOCAL_POSE_TOPIC_MS
#define AP_DDS_DELAY_LOCAL_POSE_TOPIC_MS 33
#endif

#ifndef AP_DDS_LOCAL_VEL_PUB_ENABLED
#define AP_DDS_LOCAL_VEL_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_LOCAL_VELOCITY_TOPIC_MS
#define AP_DDS_DELAY_LOCAL_VELOCITY_TOPIC_MS 33
#endif

#ifndef AP_DDS_AIRSPEED_PUB_ENABLED
#define AP_DDS_AIRSPEED_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_AIRSPEED_TOPIC_MS
#define AP_DDS_DELAY_AIRSPEED_TOPIC_MS 33
#endif

#ifndef AP_DDS_RC_PUB_ENABLED
#define AP_DDS_RC_PUB_ENABLED (AP_RSSI_ENABLED && AP_RC_CHANNEL_ENABLED)
#endif

#ifndef AP_DDS_DELAY_RC_TOPIC_MS
#define AP_DDS_DELAY_RC_TOPIC_MS 100
#endif

#ifndef AP_DDS_BATTERY_STATE_PUB_ENABLED
#define AP_DDS_BATTERY_STATE_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_BATTERY_STATE_TOPIC_MS
#define AP_DDS_DELAY_BATTERY_STATE_TOPIC_MS 1000
#endif

#ifndef AP_DDS_DELAY_STATUS_TOPIC_MS
#define AP_DDS_DELAY_STATUS_TOPIC_MS 100
#endif

#ifndef AP_DDS_CLOCK_PUB_ENABLED
#define AP_DDS_CLOCK_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_CLOCK_TOPIC_MS
#define AP_DDS_DELAY_CLOCK_TOPIC_MS 10
#endif

#ifndef AP_DDS_CLOCK_SUB_ENABLED
#define AP_DDS_CLOCK_SUB_ENABLED (CONFIG_HAL_BOARD == HAL_BOARD_SITL)
#endif

#ifndef AP_DDS_GOAL_PUB_ENABLED
#define AP_DDS_GOAL_PUB_ENABLED 1
#endif

#ifndef AP_DDS_DELAY_GOAL_TOPIC_MS
#define AP_DDS_DELAY_GOAL_TOPIC_MS  200
#endif
#ifndef AP_DDS_STATUS_PUB_ENABLED
#define AP_DDS_STATUS_PUB_ENABLED 1
#endif

#ifndef AP_DDS_JOY_SUB_ENABLED
#define AP_DDS_JOY_SUB_ENABLED 1
#endif

#ifndef AP_DDS_VEL_CTRL_ENABLED
#define AP_DDS_VEL_CTRL_ENABLED 1
#endif

#ifndef AP_DDS_GLOBAL_POS_CTRL_ENABLED
#define AP_DDS_GLOBAL_POS_CTRL_ENABLED 1
#endif

#ifndef AP_DDS_DYNAMIC_TF_SUB_ENABLED
#define AP_DDS_DYNAMIC_TF_SUB_ENABLED 1
#endif

#ifndef AP_DDS_ARM_SERVER_ENABLED
#define AP_DDS_ARM_SERVER_ENABLED 1
#endif

#ifndef AP_DDS_MODE_SWITCH_SERVER_ENABLED
#define AP_DDS_MODE_SWITCH_SERVER_ENABLED 1
#endif

#ifndef AP_DDS_VTOL_TAKEOFF_SERVER_ENABLED
#define AP_DDS_VTOL_TAKEOFF_SERVER_ENABLED 1
#endif

#ifndef AP_DDS_PARAMETER_SERVER_ENABLED
#define AP_DDS_PARAMETER_SERVER_ENABLED 1
#endif

#ifndef AP_DDS_ARM_CHECK_SERVER_ENABLED
#define AP_DDS_ARM_CHECK_SERVER_ENABLED 1
#endif

// Whether to include Twist support
#define AP_DDS_NEEDS_TWIST AP_DDS_VEL_CTRL_ENABLED || AP_DDS_LOCAL_VEL_PUB_ENABLED

// Whether to include Transform support
#define AP_DDS_NEEDS_TRANSFORMS AP_DDS_DYNAMIC_TF_SUB_ENABLED || AP_DDS_STATIC_TF_PUB_ENABLED

// Whether DDS needs GPS
#define AP_DDS_NEEDS_GPS AP_DDS_NAVSATFIX_PUB_ENABLED || AP_DDS_STATIC_TF_PUB_ENABLED

#ifndef AP_DDS_DEFAULT_UDP_IP_ADDR
#if CONFIG_HAL_BOARD == HAL_BOARD_CHIBIOS
#define AP_DDS_DEFAULT_UDP_IP_ADDR "192.168.144.2"
#else
#define AP_DDS_DEFAULT_UDP_IP_ADDR "127.0.0.1"
#endif
#endif

// Max DDS topic/service string
#ifndef AP_DDS_MAX_NAME_LEN
#define AP_DDS_MAX_NAME_LEN 128
#endif
