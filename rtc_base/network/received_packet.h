/*
 *  Copyright 2023 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef RTC_BASE_NETWORK_RECEIVED_PACKET_H_
#define RTC_BASE_NETWORK_RECEIVED_PACKET_H_

#include <cstdint>
#include <optional>

#include "api/array_view.h"
#include "api/units/timestamp.h"
#include "rtc_base/network/ecn_marking.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/system/rtc_export.h"

namespace rtc {

// ReceivedPacket repressent a received IP packet.
// It contains a payload and metadata.
// ReceivedPacket itself does not put constraints on what payload contains. For
// example it may contains STUN, SCTP, SRTP, RTP, RTCP.... etc.
class RTC_EXPORT ReceivedPacket {
 public:
  enum DecryptionInfo {
    kNotDecrypted,   // Payload has not yet been decrypted or encryption is not
                     // used.
    kDtlsDecrypted,  // Payload has been Dtls decrypted
    kSrtpEncrypted   // Payload is SRTP encrypted.
  };

  // Caller must keep memory pointed to by payload and address valid for the
  // lifetime of this ReceivedPacket.
  ReceivedPacket(rtc::ArrayView<const uint8_t> payload,
                 const webrtc::SocketAddress& source_address,
                 std::optional<webrtc::Timestamp> arrival_time = std::nullopt,
                 EcnMarking ecn = EcnMarking::kNotEct,
                 DecryptionInfo decryption = kNotDecrypted);

  ReceivedPacket CopyAndSet(DecryptionInfo decryption_info) const;

  // Address/port of the packet sender.
  const webrtc::SocketAddress& source_address() const {
    return source_address_;
  }
  rtc::ArrayView<const uint8_t> payload() const { return payload_; }

  // Timestamp when this packet was received. Not available on all socket
  // implementations.
  std::optional<webrtc::Timestamp> arrival_time() const {
    return arrival_time_;
  }

  // L4S Explicit Congestion Notification.
  EcnMarking ecn() const { return ecn_; }

  const DecryptionInfo& decryption_info() const { return decryption_info_; }

  static ReceivedPacket CreateFromLegacy(
      const char* data,
      size_t size,
      int64_t packet_time_us,
      const webrtc::SocketAddress& addr = webrtc::SocketAddress()) {
    return CreateFromLegacy(reinterpret_cast<const uint8_t*>(data), size,
                            packet_time_us, addr);
  }

  static ReceivedPacket CreateFromLegacy(
      const uint8_t* data,
      size_t size,
      int64_t packet_time_us,
      const webrtc::SocketAddress& = webrtc::SocketAddress());

 private:
  rtc::ArrayView<const uint8_t> payload_;
  std::optional<webrtc::Timestamp> arrival_time_;
  const webrtc::SocketAddress& source_address_;
  EcnMarking ecn_;
  DecryptionInfo decryption_info_;
};

}  // namespace rtc
#endif  // RTC_BASE_NETWORK_RECEIVED_PACKET_H_
