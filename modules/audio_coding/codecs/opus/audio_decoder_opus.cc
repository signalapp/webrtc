/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/codecs/opus/audio_decoder_opus.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "api/array_view.h"
#include "api/audio_codecs/audio_decoder.h"
#include "api/field_trials_view.h"
#include "modules/audio_coding/codecs/opus/audio_coder_opus_common.h"
#include "modules/audio_coding/codecs/opus/opus_interface.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
#include "rtc_base/copy_on_write_buffer.h"
#endif
// RingRTC Change to configure opus
#include "rtc_base/logging.h"

namespace webrtc {

AudioDecoderOpusImpl::AudioDecoderOpusImpl(const FieldTrialsView& field_trials,
                                           size_t num_channels,
                                           int sample_rate_hz)
    : channels_(num_channels),
      sample_rate_hz_(sample_rate_hz),
      generate_plc_(field_trials.IsEnabled("WebRTC-Audio-OpusGeneratePlc")) {
  RTC_DCHECK(num_channels == 1 || num_channels == 2);
  RTC_DCHECK(sample_rate_hz == 16000 || sample_rate_hz == 48000);
  const int error =
      WebRtcOpus_DecoderCreate(&dec_state_, channels_, sample_rate_hz_);
  RTC_DCHECK(error == 0);
  WebRtcOpus_DecoderInit(dec_state_);
}

AudioDecoderOpusImpl::~AudioDecoderOpusImpl() {
  WebRtcOpus_DecoderFree(dec_state_);
}

std::vector<AudioDecoder::ParseResult> AudioDecoderOpusImpl::ParsePayload(
    Buffer&& payload,
    uint32_t timestamp) {
  std::vector<ParseResult> results;

  if (PacketHasFec(payload.data(), payload.size())) {
    const int duration =
        PacketDurationRedundant(payload.data(), payload.size());
    RTC_DCHECK_GE(duration, 0);
    Buffer payload_copy(payload.data(), payload.size());
    std::unique_ptr<EncodedAudioFrame> fec_frame(
        new OpusFrame(this, std::move(payload_copy), false));
    results.emplace_back(timestamp - duration, 1, std::move(fec_frame));
  }
  std::unique_ptr<EncodedAudioFrame> frame(
      new OpusFrame(this, std::move(payload), true));
  results.emplace_back(timestamp, 0, std::move(frame));
  return results;
}

// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
std::vector<AudioDecoder::ParseResult>
AudioDecoderOpusImpl::ParsePayloadRedundancy(
    Buffer&& payload,
    uint32_t timestamp,
    uint32_t recovery_timestamp_offset) {
  std::vector<ParseResult> results;
  uint32_t begin_timestamp = timestamp;

  // Check for FEC.
  if (PacketHasFec(payload.data(), payload.size())) {
    const int duration =
        PacketDurationRedundant(payload.data(), payload.size());
    RTC_DCHECK_GE(duration, 0);
    Buffer payload_copy(payload.data(), payload.size());
    results.emplace_back(
        timestamp - duration,
        1, // FEC packets have a priority of 1.
        std::make_unique<OpusFrame>(this, std::move(payload_copy), false)
    );
    begin_timestamp -= duration;
  }

  // Check for DRED.
  if (recovery_timestamp_offset > 0) {
    int32_t dred_end = 0;
    CopyOnWriteBuffer dred_data(opus_dred_get_size());
    int samples = WebRtcOpus_DredParse(dec_state_, dred_data.MutableData(),
                                       payload.data(), payload.size(),
                                       recovery_timestamp_offset, &dred_end);
    if (samples > 0 && dred_end < samples) {
      samples -= dred_end;
      // Find the desired number of 10ms chunks.
      int dred_count = 100 * samples / sample_rate_hz_;
      int desired_dred_count =
          100 * recovery_timestamp_offset / sample_rate_hz_;
      if (dred_count > 0 && desired_dred_count > 0) {
        if (dred_count > desired_dred_count)
          dred_count = desired_dred_count;
        uint32_t recovery_timestamp =
            timestamp - dred_count * sample_rate_hz_ / 100;
        for (int i = 0; i < dred_count; ++i) {
          // Make sure recovery_timestamp < begin_timestamp (if there was a FEC frame).
          if ((begin_timestamp == recovery_timestamp) ||
              (begin_timestamp - recovery_timestamp) >= 0xFFFFFFFF / 2)
            break;
          results.emplace_back(
              recovery_timestamp,
              2, // Deep REDundancy packets have a priority of 2.
              std::make_unique<OpusFrame>(this, dred_data, dred_count - i, timestamp)
          );
          recovery_timestamp += sample_rate_hz_ / 100;
        }
      }
    }
  }

  // Handle the primary packet.
  results.emplace_back(
      timestamp,
      0, // Primary packets have a priority of 0 (highest).
      std::make_unique<OpusFrame>(this, std::move(payload), true)
  );

  return results;
}
#endif

int AudioDecoderOpusImpl::DecodeInternal(const uint8_t* encoded,
                                         size_t encoded_len,
                                         int sample_rate_hz,
                                         int16_t* decoded,
                                         SpeechType* speech_type) {
  RTC_DCHECK_EQ(sample_rate_hz, sample_rate_hz_);
  int16_t temp_type = 1;  // Default is speech.
  int ret =
      WebRtcOpus_Decode(dec_state_, encoded, encoded_len, decoded, &temp_type);
  if (ret > 0)
    ret *= static_cast<int>(channels_);  // Return total number of samples.
  *speech_type = ConvertSpeechType(temp_type);
  return ret;
}

int AudioDecoderOpusImpl::DecodeRedundantInternal(const uint8_t* encoded,
                                                  size_t encoded_len,
                                                  int sample_rate_hz,
                                                  int16_t* decoded,
                                                  SpeechType* speech_type) {
  if (!PacketHasFec(encoded, encoded_len)) {
    // This packet is a RED packet.
    return DecodeInternal(encoded, encoded_len, sample_rate_hz, decoded,
                          speech_type);
  }

  RTC_DCHECK_EQ(sample_rate_hz, sample_rate_hz_);
  int16_t temp_type = 1;  // Default is speech.
  int ret = WebRtcOpus_DecodeFec(dec_state_, encoded, encoded_len, decoded,
                                 &temp_type);
  if (ret > 0)
    ret *= static_cast<int>(channels_);  // Return total number of samples.
  *speech_type = ConvertSpeechType(temp_type);
  return ret;
}

// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
int AudioDecoderOpusImpl::DecodeDred(const uint8_t* encoded,
                                             size_t encoded_len,
                                             uint32_t primary_timestamp,
                                             int16_t* decoded,
                                             int index) {
  int ret = WebRtcOpus_DecodeDred(dec_state_, encoded, decoded, index);
  if (ret > 0)
    ret *= static_cast<int>(channels_);  // Return total number of samples.
  return ret;
}
#endif

void AudioDecoderOpusImpl::Reset() {
  WebRtcOpus_DecoderInit(dec_state_);
}

int AudioDecoderOpusImpl::PacketDuration(const uint8_t* encoded,
                                         size_t encoded_len) const {
  return WebRtcOpus_DurationEst(dec_state_, encoded, encoded_len);
}

int AudioDecoderOpusImpl::PacketDurationRedundant(const uint8_t* encoded,
                                                  size_t encoded_len) const {
  if (!PacketHasFec(encoded, encoded_len)) {
    // This packet is a RED packet.
    return PacketDuration(encoded, encoded_len);
  }

  return WebRtcOpus_FecDurationEst(encoded, encoded_len, sample_rate_hz_);
}

bool AudioDecoderOpusImpl::PacketHasFec(const uint8_t* encoded,
                                        size_t encoded_len) const {
  int fec;
  fec = WebRtcOpus_PacketHasFec(encoded, encoded_len);
  return (fec == 1);
}

int AudioDecoderOpusImpl::SampleRateHz() const {
  return sample_rate_hz_;
}

size_t AudioDecoderOpusImpl::Channels() const {
  return channels_;
}

void AudioDecoderOpusImpl::GeneratePlc(
    size_t /* requested_samples_per_channel */,
    BufferT<int16_t>* concealment_audio) {
  if (!generate_plc_) {
    return;
  }
  int plc_size = WebRtcOpus_PlcDuration(dec_state_) * channels_;
  concealment_audio->AppendData(plc_size, [&](ArrayView<int16_t> decoded) {
    int16_t temp_type = 1;
    int ret =
        WebRtcOpus_Decode(dec_state_, nullptr, 0, decoded.data(), &temp_type);
    if (ret < 0) {
      return 0;
    }
    return ret;
  });
}

// RingRTC change to configure opus
bool AudioDecoderOpusImpl::Configure(const AudioDecoder::Config& config) {
  if (config.complexity >= 0) {
    // Make sure the decoder will generate PLC when requested by NetEq.
    generate_plc_ = true;
    WebRtcOpus_DecoderSetComplexity(dec_state_, config.complexity);
  }

// RingRTC change to support Opus DNN features
#if WEBRTC_OPUS_SUPPORT_DEEP_PLC || WEBRTC_OPUS_SUPPORT_DRED
  if (config.dnn_weights_data && config.dnn_weights_length > 0) {
    if (WebRtcOpus_DecoderSetDnnBlob(dec_state_, config.dnn_weights_data,
                                     config.dnn_weights_length) == -1) {
      RTC_LOG(LS_ERROR) << "Failed to configure OPUS DNN blob for decoder";
      return false;
    }
    RTC_LOG(LS_INFO) << "Successfully configured OPUS DNN blob for decoder: "
                     << config.dnn_weights_length << " bytes";
// RingRTC change to support Opus DRED
#if WEBRTC_OPUS_SUPPORT_DRED
    if (WebRtcOpus_DredDecoderSetDnnBlob(dec_state_, config.dnn_weights_data,
                                         config.dnn_weights_length) == -1) {
      RTC_LOG(LS_WARNING)
          << "Failed to configure OPUS DNN blob for DRED decoder";
    } else {
      RTC_LOG(LS_INFO)
          << "Successfully configured OPUS DNN blob for DRED decoder: "
          << config.dnn_weights_length << " bytes";
    }
#endif
  }
#endif

  return true;
}

}  // namespace webrtc
