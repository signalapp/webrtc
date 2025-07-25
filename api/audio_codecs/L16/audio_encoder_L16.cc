/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/audio_codecs/L16/audio_encoder_L16.h"

#include <stddef.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "absl/strings/match.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_format.h"
#include "api/field_trials_view.h"
#include "modules/audio_coding/codecs/pcm16b/audio_encoder_pcm16b.h"
#include "modules/audio_coding/codecs/pcm16b/pcm16b_common.h"
#include "rtc_base/checks.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/numerics/safe_minmax.h"
#include "rtc_base/string_to_number.h"

namespace webrtc {

std::optional<AudioEncoderL16::Config> AudioEncoderL16::SdpToConfig(
    const SdpAudioFormat& format) {
  if (!IsValueInRangeForNumericType<int>(format.num_channels)) {
    RTC_DCHECK_NOTREACHED();
    return std::nullopt;
  }
  Config config;
  config.sample_rate_hz = format.clockrate_hz;
  config.num_channels = dchecked_cast<int>(format.num_channels);
  auto ptime_iter = format.parameters.find("ptime");
  if (ptime_iter != format.parameters.end()) {
    const auto ptime = StringToNumber<int>(ptime_iter->second);
    if (ptime && *ptime > 0) {
      config.frame_size_ms = SafeClamp(10 * (*ptime / 10), 10, 60);
    }
  }
  if (absl::EqualsIgnoreCase(format.name, "L16") && config.IsOk()) {
    return config;
  }
  return std::nullopt;
}

void AudioEncoderL16::AppendSupportedEncoders(
    std::vector<AudioCodecSpec>* specs) {
  // RingRTC change to disable unused audio codecs
  // Pcm16BAppendSupportedCodecSpecs(specs);
}

AudioCodecInfo AudioEncoderL16::QueryAudioEncoder(
    const AudioEncoderL16::Config& config) {
  RTC_DCHECK(config.IsOk());
  return {config.sample_rate_hz, dchecked_cast<size_t>(config.num_channels),
          config.sample_rate_hz * config.num_channels * 16};
}

std::unique_ptr<AudioEncoder> AudioEncoderL16::MakeAudioEncoder(
    const AudioEncoderL16::Config& config,
    int payload_type,
    std::optional<AudioCodecPairId> /*codec_pair_id*/,
    const FieldTrialsView* /* field_trials */) {
  AudioEncoderPcm16B::Config c;
  c.sample_rate_hz = config.sample_rate_hz;
  c.num_channels = config.num_channels;
  c.frame_size_ms = config.frame_size_ms;
  c.payload_type = payload_type;
  if (!config.IsOk()) {
    RTC_DCHECK_NOTREACHED();
    return nullptr;
  }
  return std::make_unique<AudioEncoderPcm16B>(c);
}

}  // namespace webrtc
