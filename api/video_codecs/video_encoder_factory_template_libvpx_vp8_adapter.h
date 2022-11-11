/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_VIDEO_CODECS_VIDEO_ENCODER_FACTORY_TEMPLATE_LIBVPX_VP8_ADAPTER_H_
#define API_VIDEO_CODECS_VIDEO_ENCODER_FACTORY_TEMPLATE_LIBVPX_VP8_ADAPTER_H_

#include <memory>
#include <vector>

<<<<<<< HEAD
#include "modules/video_coding/codecs/vp8/include/vp8.h"
=======
#include "absl/container/inlined_vector.h"
#include "api/video_codecs/sdp_video_format.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp8/vp8_scalability.h"
>>>>>>> m108

namespace webrtc {
struct LibvpxVp8EncoderTemplateAdapter {
  static std::vector<SdpVideoFormat> SupportedFormats() {
<<<<<<< HEAD
    return {SdpVideoFormat("VP8")};
=======
    absl::InlinedVector<ScalabilityMode, kScalabilityModeCount>
        scalability_modes;
    for (const auto scalability_mode : kVP8SupportedScalabilityModes) {
      scalability_modes.push_back(scalability_mode);
    }

    return {
        SdpVideoFormat("VP8", SdpVideoFormat::Parameters(), scalability_modes)};
>>>>>>> m108
  }

  static std::unique_ptr<VideoEncoder> CreateEncoder(
      const SdpVideoFormat& format) {
    return VP8Encoder::Create();
  }

<<<<<<< HEAD
  static bool IsScalabilityModeSupported(
      const absl::string_view scalability_mode) {
    return VP8Encoder::SupportsScalabilityMode(scalability_mode);
=======
  static bool IsScalabilityModeSupported(ScalabilityMode scalability_mode) {
    return VP8SupportsScalabilityMode(scalability_mode);
>>>>>>> m108
  }
};
}  // namespace webrtc

#endif  // API_VIDEO_CODECS_VIDEO_ENCODER_FACTORY_TEMPLATE_LIBVPX_VP8_ADAPTER_H_
