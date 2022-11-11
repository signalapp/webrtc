/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_VIDEO_CODECS_VIDEO_ENCODER_FACTORY_TEMPLATE_LIBAOM_AV1_ADAPTER_H_
#define API_VIDEO_CODECS_VIDEO_ENCODER_FACTORY_TEMPLATE_LIBAOM_AV1_ADAPTER_H_

#include <memory>
#include <vector>

<<<<<<< HEAD
#include "modules/video_coding/codecs/av1/libaom_av1_encoder.h"
#include "modules/video_coding/svc/create_scalability_structure.h"
=======
#include "absl/container/inlined_vector.h"
#include "api/video_codecs/sdp_video_format.h"
#include "modules/video_coding/codecs/av1/av1_svc_config.h"
#include "modules/video_coding/codecs/av1/libaom_av1_encoder.h"
>>>>>>> m108

namespace webrtc {
struct LibaomAv1EncoderTemplateAdapter {
  static std::vector<SdpVideoFormat> SupportedFormats() {
<<<<<<< HEAD
    return {SdpVideoFormat("AV1")};
=======
    absl::InlinedVector<ScalabilityMode, kScalabilityModeCount>
        scalability_modes = LibaomAv1EncoderSupportedScalabilityModes();
    return {
        SdpVideoFormat("AV1", SdpVideoFormat::Parameters(), scalability_modes)};
>>>>>>> m108
  }

  static std::unique_ptr<VideoEncoder> CreateEncoder(
      const SdpVideoFormat& format) {
    return CreateLibaomAv1Encoder();
  }

<<<<<<< HEAD
  static bool IsScalabilityModeSupported(absl::string_view scalability_mode) {
    // For libaom AV1, the scalability mode is supported if we can create the
    // scalability structure.
    return ScalabilityStructureConfig(scalability_mode) != absl::nullopt;
=======
  static bool IsScalabilityModeSupported(ScalabilityMode scalability_mode) {
    return LibaomAv1EncoderSupportsScalabilityMode(scalability_mode);
>>>>>>> m108
  }
};

}  // namespace webrtc

#endif  // API_VIDEO_CODECS_VIDEO_ENCODER_FACTORY_TEMPLATE_LIBAOM_AV1_ADAPTER_H_
