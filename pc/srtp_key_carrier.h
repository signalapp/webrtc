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

#ifndef PC_SRTP_KEY_CARRIER_H_
#define PC_SRTP_KEY_CARRIER_H_

#include <optional>

#include "api/crypto_params.h"
#include "api/jsep.h"
#include "pc/session_description.h"

namespace cricket {

// A helper class used to propagate crypto params.
class SrtpKeyCarrier {
 public:
  SrtpKeyCarrier();
  ~SrtpKeyCarrier();

  // Handle the offer/answer propagation of the crypto parameters.
  // If type is kPrAnswer or kAnswer, returns true iff `send_params` and
  // `recv_params` are usable.
  bool ApplyParams(const CryptoParams& crypto,
                   webrtc::SdpType type,
                   ContentSource source);

  const CryptoParams& send_params() { return applied_send_params_; }
  const CryptoParams& recv_params() { return applied_recv_params_; }

 private:
  // Applies params to be visible from `send_params` and `recv_params`.
  bool SetAnswer(const CryptoParams& answer_params, ContentSource source);

  std::optional<CryptoParams> offer_params_;
  CryptoParams applied_send_params_;
  CryptoParams applied_recv_params_;
};

}  // namespace cricket

#endif  // PC_SRTP_KEY_CARRIER_H_
