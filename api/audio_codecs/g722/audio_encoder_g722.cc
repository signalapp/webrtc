/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/audio_codecs/g722/audio_encoder_g722.h"

#include <stddef.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "absl/strings/match.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/g722/audio_encoder_g722_config.h"
#include "api/field_trials_view.h"
#include "modules/audio_coding/codecs/g722/audio_encoder_g722.h"
#include "rtc_base/checks.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/numerics/safe_minmax.h"
#include "rtc_base/string_to_number.h"

namespace webrtc {

std::optional<AudioEncoderG722Config> AudioEncoderG722::SdpToConfig(
    const SdpAudioFormat& format) {
  if (!absl::EqualsIgnoreCase(format.name, "g722") ||
      format.clockrate_hz != 8000) {
    return std::nullopt;
  }

  AudioEncoderG722Config config;
  config.num_channels = checked_cast<int>(format.num_channels);
  auto ptime_iter = format.parameters.find("ptime");
  if (ptime_iter != format.parameters.end()) {
    auto ptime = StringToNumber<int>(ptime_iter->second);
    if (ptime && *ptime > 0) {
      const int whole_packets = *ptime / 10;
      config.frame_size_ms = SafeClamp<int>(whole_packets * 10, 10, 60);
    }
  }
  if (!config.IsOk()) {
    RTC_DCHECK_NOTREACHED();
    return std::nullopt;
  }
  return config;
}

void AudioEncoderG722::AppendSupportedEncoders(
    std::vector<AudioCodecSpec>* specs) {
  // RingRTC change to disable unused audio codecs
  // const SdpAudioFormat fmt = {"G722", 8000, 1};
  // const AudioCodecInfo info = QueryAudioEncoder(*SdpToConfig(fmt));
  // specs->push_back({fmt, info});
}

AudioCodecInfo AudioEncoderG722::QueryAudioEncoder(
    const AudioEncoderG722Config& config) {
  RTC_DCHECK(config.IsOk());
  return {16000, dchecked_cast<size_t>(config.num_channels),
          64000 * config.num_channels};
}

std::unique_ptr<AudioEncoder> AudioEncoderG722::MakeAudioEncoder(
    const AudioEncoderG722Config& config,
    int payload_type,
    std::optional<AudioCodecPairId> /*codec_pair_id*/,
    const FieldTrialsView* /* field_trials */) {
  if (!config.IsOk()) {
    RTC_DCHECK_NOTREACHED();
    return nullptr;
  }
  return std::make_unique<AudioEncoderG722Impl>(config, payload_type);
}

}  // namespace webrtc
