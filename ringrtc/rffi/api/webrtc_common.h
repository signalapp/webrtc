/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef API_WEBRTC_COMMON_H__
#define API_WEBRTC_COMMON_H__

#include <cstdint>

// Types used in the ringrtc <-> rffi and rffi <-> webrtc interfaces.
// They are "common" in the sense that they are included both by webrtc and rffi

// Most of these are moved from types defined upstream; in this case, there is
// a reference to the original file, and the struct has been deleted from the
// original file so that upstream changes cause merge conflicts.

// Why do we want this, rather than leaving the upstream WebRTC code as-is?
//
// Since the code here is shared between RingRTC and WebRTC, RingRTC needs to
// understand what the enum values, struct fields, and struct layouts are.
//
// If a WebRTC merge changed the order of struct fields (for example), and
// the definition were duplicated between WebRTC and RingRTC, there would be no
// compile error, and structs created by WebRTC would be interpreted incorrectly
// by RingRTC (and vice-versa).
//
// So, we need a single definition. The motivation for having this **here**,
// as opposed to having RingRTC include the WebRTC header files, is to provide
// rust-bindgen with a small, self-contained set of header files so that RingRTC
// builds do not need to have a full copy of the WebRTC source tree, and
// to keep compile times down.
//
// Having these types here (and deleting them from the upstream locations) will
// cause a merge conflict if the upstream definitions change. In that case, we
// would want to update the shared definition here.

namespace webrtc {

// Moved from rtc_base/network_constants.h
enum AdapterType {
  // This enum resembles the one in Chromium net::ConnectionType.
  ADAPTER_TYPE_UNKNOWN = 0,
  ADAPTER_TYPE_ETHERNET = 1 << 0,
  ADAPTER_TYPE_WIFI = 1 << 1,
  ADAPTER_TYPE_CELLULAR = 1 << 2,  // This is CELLULAR of unknown type.
  ADAPTER_TYPE_VPN = 1 << 3,
  ADAPTER_TYPE_LOOPBACK = 1 << 4,
  // ADAPTER_TYPE_ANY is used for a network, which only contains a single "any
  // address" IP address (INADDR_ANY for IPv4 or in6addr_any for IPv6), and can
  // use any/all network interfaces. Whereas ADAPTER_TYPE_UNKNOWN is used
  // when the network uses a specific interface/IP, but its interface type can
  // not be determined or not fit in this enum.
  ADAPTER_TYPE_ANY = 1 << 5,
  ADAPTER_TYPE_CELLULAR_2G = 1 << 6,
  ADAPTER_TYPE_CELLULAR_3G = 1 << 7,
  ADAPTER_TYPE_CELLULAR_4G = 1 << 8,
  ADAPTER_TYPE_CELLULAR_5G = 1 << 9
};

// Moved from rtc_base/logging.h
//////////////////////////////////////////////////////////////////////
// The meanings of the levels are:
//  LS_VERBOSE: This level is for data which we do not want to appear in the
//   normal debug log, but should appear in diagnostic logs.
//  LS_INFO: Chatty level used in debugging for all sorts of things, the default
//   in debug builds.
//  LS_WARNING: Something that may warrant investigation.
//  LS_ERROR: Something that should not have occurred.
//  LS_NONE: Don't log.
enum LoggingSeverity {
  LS_VERBOSE,
  LS_INFO,
  LS_WARNING,
  LS_ERROR,
  LS_NONE,
};

// Moved from api/video/video_rotation.h
// enum for clockwise rotation.
enum VideoRotation {
  kVideoRotation_0 = 0,
  kVideoRotation_90 = 90,
  kVideoRotation_180 = 180,
  kVideoRotation_270 = 270
};

// Namespace to avoid conflicting with IceConnectionState in
// p2p/base/ice_transport_internal.h
namespace rffi {
// Moved from api/peer_connection_interface.h
// See https://w3c.github.io/webrtc-pc/#dom-rtciceconnectionstate
enum IceConnectionState {
  kIceConnectionNew,
  kIceConnectionChecking,
  kIceConnectionConnected,
  kIceConnectionCompleted,
  kIceConnectionFailed,
  kIceConnectionDisconnected,
  kIceConnectionClosed,
  kIceConnectionMax,
};
}  // namespace rffi

// Very OPUS-specific
struct AudioEncoderConfig {
  // AKA ptime or frame size
  // One of 10, 20, 40, 60, 80, 100, 120
  int32_t initial_packet_size_ms = 60;
  int32_t min_packet_size_ms = 60;
  int32_t max_packet_size_ms = 60;

  // 500 to 192000
  // Start at initial_bitrate_bps, and let the BWE and bitrate allocator
  // move up to max_bitrate_bps or down to min_bitrate_bps.
  int32_t initial_bitrate_bps = 32000;
  int32_t min_bitrate_bps = 32000;
  int32_t max_bitrate_bps = 32000;

  // 1101 = OPUS_BANDWIDTH_NARROWBAND
  // 1102 = OPUS_BADWIDTH_MEDIUMBAND
  // 1103 = OPUS_BANDWIDTH_WIDEBAND
  // 1104 = OPUS_BANDWIDTH_SUPERWIDEBAND
  // 1105 = OPUS_BANDWIDTH_FULLBAND
  int32_t bandwidth = -1000;  // OPUS_AUTO

  // 0 (least complex) to 9 (most complex)
  int32_t complexity = 9;

  // Adaptation method to use, 0 to disable
  int32_t adaptation = 0;

  // CBR is used by default
  bool enable_cbr = true;

  // DTX is enabled by default
  bool enable_dtx = true;

  // In-band FEC is enabled by default
  bool enable_fec = true;

  // DRED duration (1 to 100), 0 to disable
  int32_t dred_duration = 0;

  // Minimum packet loss percentage (0-100)
  int32_t min_packet_loss_percent = 0;

  // DNN weights blob
  const void* dnn_weights_data = nullptr;
  int32_t dnn_weights_length = 0;
};

// Very OPUS-specific
struct AudioDecoderConfig {
  // DNN weights blob
  const void* dnn_weights_data = nullptr;
  int32_t dnn_weights_length = 0;
  // Decoder complexity:
  // -1: Use NetEq PLC (default)
  //  0: Use Opus PLC
  //  4: Use Opus BWE (not supported)
  //  5: Use Opus Deep PLC
  //  6: Use Opus Deep PLC and LACE (not supported)
  //  7: Use Opus Deep PLC and NoLACE (not supported)
  int32_t complexity = -1;
};

typedef struct {
  uint32_t ssrc;
  uint16_t level;
} ReceivedAudioLevel;

}  // namespace webrtc

#endif  // API_WEBRTC_COMMON_H__
