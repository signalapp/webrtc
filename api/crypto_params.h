/*
 * Copyright 2019-2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// RingRTC change: Struct to carry SRTP crypto parameters to RTP transport.

#ifndef API_CRYPTO_PARAMS_H_
#define API_CRYPTO_PARAMS_H_

#include <algorithm>

#include "rtc_base/buffer.h"
#include "rtc_base/ssl_stream_adapter.h"  // kSrtpInvalidCryptoSuite

namespace webrtc {

// Parameters for propagating SRTP params to RTP transport.
struct CryptoParams {
  CryptoParams() = default;

  // Manually define a copy constructor because ZeroOnFreeBuffer assumes its
  // contents might be quite large, and wants us to be explicit. However, keys
  // won't be extremely large, so allow copies.
  CryptoParams(const CryptoParams& other)
      : crypto_suite(other.crypto_suite),
        key_params(other.key_params.data(), other.key_params.size()) {}

  // Similarly define an assignment constructor.
  CryptoParams& operator=(CryptoParams other) {
    std::swap(crypto_suite, other.crypto_suite);
    // ZeroOnFreeBuffer defines a swap()
    std::swap(key_params, other.key_params);
    return *this;
  }

  int crypto_suite = kSrtpInvalidCryptoSuite;
  // Key and salt.
  ZeroOnFreeBuffer<uint8_t> key_params;
};

}  // namespace webrtc

#endif  // API_CRYPTO_PARAMS_H_
