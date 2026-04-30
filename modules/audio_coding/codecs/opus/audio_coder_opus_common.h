/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_CODING_CODECS_OPUS_AUDIO_CODER_OPUS_COMMON_H_
#define MODULES_AUDIO_CODING_CODECS_OPUS_AUDIO_CODER_OPUS_COMMON_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/array_view.h"
#include "api/audio_codecs/audio_decoder.h"
#include "api/audio_codecs/audio_format.h"
#include "rtc_base/buffer.h"
#include "rtc_base/string_to_number.h"

// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
#include "modules/audio_coding/codecs/opus/audio_decoder_opus.h"
#include "rtc_base/copy_on_write_buffer.h"
#endif

namespace webrtc {

std::optional<std::string> GetFormatParameter(const SdpAudioFormat& format,
                                              absl::string_view param);

template <typename T>
std::optional<T> GetFormatParameter(const SdpAudioFormat& format,
                                    absl::string_view param) {
  return StringToNumber<T>(GetFormatParameter(format, param).value_or(""));
}

template <>
std::optional<std::vector<unsigned char>> GetFormatParameter(
    const SdpAudioFormat& format,
    absl::string_view param);

class OpusFrame : public AudioDecoder::EncodedAudioFrame {
 public:
  OpusFrame(AudioDecoder* decoder, Buffer&& payload, bool is_primary_payload)
      : decoder_(decoder),
        payload_(std::move(payload)),
        is_primary_payload_(is_primary_payload) {}

// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
  OpusFrame(AudioDecoder* decoder,
            const CopyOnWriteBuffer& dred_payload,
            int dred_index,
            uint32_t dred_primary_timestamp)
      : decoder_(decoder),
        is_primary_payload_(false),
        dred_payload_(dred_payload),
        dred_index_(dred_index),
        dred_primary_timestamp_(dred_primary_timestamp) {}
#endif

  size_t Duration() const override {
    int ret;
// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
    if (dred_index_ > 0) {
      // DRED frames are always 10ms.
      return decoder_->SampleRateHz() / 100;
    }
#endif
    if (is_primary_payload_) {
      ret = decoder_->PacketDuration(payload_.data(), payload_.size());
    } else {
      ret = decoder_->PacketDurationRedundant(payload_.data(), payload_.size());
    }
    return (ret < 0) ? 0 : static_cast<size_t>(ret);
  }

  bool IsDtxPacket() const override {
// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
    // Don't treat DRED frames as DTX packets.
    return payload_.size() <= 2 && dred_index_ == 0;
#else
    return payload_.size() <= 2;
#endif
  }

  std::optional<DecodeResult> Decode(
      ArrayView<int16_t> decoded) const override {
    AudioDecoder::SpeechType speech_type = AudioDecoder::kSpeech;
    int ret;
    if (is_primary_payload_) {
      ret = decoder_->Decode(
          payload_.data(), payload_.size(), decoder_->SampleRateHz(),
          decoded.size() * sizeof(int16_t), decoded.data(), &speech_type);
// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
    } else if (dred_index_ > 0 && dred_payload_.size() > 0) {
      ret = static_cast<AudioDecoderOpusImpl*>(decoder_)->DecodeDred(
          dred_payload_.data(), dred_payload_.size(),
          dred_primary_timestamp_, decoded.data(), dred_index_);
#endif
    } else {
      ret = decoder_->DecodeRedundant(
          payload_.data(), payload_.size(), decoder_->SampleRateHz(),
          decoded.size() * sizeof(int16_t), decoded.data(), &speech_type);
    }

    if (ret < 0)
      return std::nullopt;

    return DecodeResult{.num_decoded_samples = static_cast<size_t>(ret),
                        .speech_type = speech_type};
  }

 private:
  AudioDecoder* const decoder_;
  const Buffer payload_;
  const bool is_primary_payload_;
// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
  const CopyOnWriteBuffer dred_payload_;
  const int dred_index_ = 0;
  const uint32_t dred_primary_timestamp_ = 0;
#endif
};

}  // namespace webrtc

#endif  // MODULES_AUDIO_CODING_CODECS_OPUS_AUDIO_CODER_OPUS_COMMON_H_
