/*
 *  Copyright 2009 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// RingRTC: Allow out-of-band / "manual" key negotiation.

#include "pc/srtp_key_carrier.h"

#include "rtc_base/logging.h"
#include "rtc_base/ssl_stream_adapter.h"

namespace webrtc {

SrtpKeyCarrier::SrtpKeyCarrier() = default;

SrtpKeyCarrier::~SrtpKeyCarrier() = default;

bool SrtpKeyCarrier::ApplyParams(const CryptoParams& crypto,
                                 webrtc::SdpType type,
                                 ContentSource source) {
  switch (type) {
    case webrtc::SdpType::kOffer:
      offer_params_ = crypto;
      return true;
    case webrtc::SdpType::kPrAnswer:
    case webrtc::SdpType::kAnswer:
      return SetAnswer(crypto, source);
    default:
      return false;
  }
}

bool SrtpKeyCarrier::SetAnswer(const CryptoParams& answer_params,
                               ContentSource source) {
  if (!offer_params_.has_value()) {
    RTC_LOG(LS_WARNING) << "Missing offer parameters when handling SRTP answer";
    return false;
  }

  const CryptoParams& new_send_params =
      (source == CS_REMOTE) ? offer_params_.value() : answer_params;
  const CryptoParams& new_recv_params =
      (source == CS_REMOTE) ? answer_params : offer_params_.value();

  if (new_send_params.crypto_suite == kSrtpInvalidCryptoSuite) {
    RTC_LOG(LS_WARNING) << "Invalid crypto suite(s) received for send";
    return false;
  }
  if (new_recv_params.crypto_suite == kSrtpInvalidCryptoSuite) {
    RTC_LOG(LS_WARNING) << "Invalid crypto suite(s) received for recv";
    return false;
  }

  applied_send_params_ = new_send_params;
  applied_recv_params_ = new_recv_params;

  offer_params_ = std::nullopt;
  return true;
}

}  // namespace webrtc
