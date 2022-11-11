/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/internal_encoder_factory.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/match.h"
<<<<<<< HEAD
#include "api/video_codecs/sdp_video_format.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "modules/video_coding/codecs/av1/libaom_av1_encoder_supported.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "rtc_base/logging.h"
=======
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#if defined(RTC_USE_LIBAOM_AV1_ENCODER)
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"  // nogncheck
#endif
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#if defined(WEBRTC_USE_H264)
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"  // nogncheck
#endif
>>>>>>> m108

namespace webrtc {
namespace {

using Factory =
    VideoEncoderFactoryTemplate<webrtc::LibvpxVp8EncoderTemplateAdapter,
#if defined(WEBRTC_USE_H264)
                                webrtc::OpenH264EncoderTemplateAdapter,
#endif
#if defined(RTC_USE_LIBAOM_AV1_ENCODER)
                                webrtc::LibaomAv1EncoderTemplateAdapter,
#endif
                                webrtc::LibvpxVp9EncoderTemplateAdapter>;

absl::optional<SdpVideoFormat> MatchOriginalFormat(
    const SdpVideoFormat& format) {
  const auto supported_formats = Factory().GetSupportedFormats();

  absl::optional<SdpVideoFormat> res;
  int best_parameter_match = 0;
  for (const auto& supported_format : supported_formats) {
    if (absl::EqualsIgnoreCase(supported_format.name, format.name)) {
      int matching_parameters = 0;
      for (const auto& kv : supported_format.parameters) {
        auto it = format.parameters.find(kv.first);
        if (it != format.parameters.end() && it->second == kv.second) {
          matching_parameters += 1;
        }
      }

      if (!res || matching_parameters > best_parameter_match) {
        res = supported_format;
        best_parameter_match = matching_parameters;
      }
    }
  }

  return res;
}
}  // namespace

std::vector<SdpVideoFormat> InternalEncoderFactory::GetSupportedFormats()
    const {
  return Factory().GetSupportedFormats();
}

std::unique_ptr<VideoEncoder> InternalEncoderFactory::CreateVideoEncoder(
    const SdpVideoFormat& format) {
<<<<<<< HEAD
  if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName))
    return VP8Encoder::Create();
  if (absl::EqualsIgnoreCase(format.name, cricket::kVp9CodecName))
    return VP9Encoder::Create(cricket::VideoCodec(format));
  if (absl::EqualsIgnoreCase(format.name, cricket::kH264CodecName))
    return H264Encoder::Create(cricket::VideoCodec(format));
  if (kIsLibaomAv1EncoderSupported &&
      absl::EqualsIgnoreCase(format.name, cricket::kAv1CodecName))
    return CreateLibaomAv1EncoderIfSupported();
  RTC_LOG(LS_ERROR) << "Trying to created encoder of unsupported format "
                    << format.name;
  return nullptr;
=======
  auto original_format = MatchOriginalFormat(format);
  return original_format ? Factory().CreateVideoEncoder(*original_format)
                         : nullptr;
}

VideoEncoderFactory::CodecSupport InternalEncoderFactory::QueryCodecSupport(
    const SdpVideoFormat& format,
    absl::optional<std::string> scalability_mode) const {
  auto original_format = MatchOriginalFormat(format);
  return original_format
             ? Factory().QueryCodecSupport(*original_format, scalability_mode)
             : VideoEncoderFactory::CodecSupport{.is_supported = false};
>>>>>>> m108
}

VideoEncoderFactory::CodecSupport InternalEncoderFactory::QueryCodecSupport(
    const SdpVideoFormat& format,
    absl::optional<std::string> scalability_mode) const {
  // Query for supported formats and check if the specified format is supported.
  // Begin with filtering out unsupported scalability modes.
  if (scalability_mode) {
    bool scalability_mode_supported = false;
    if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName)) {
      scalability_mode_supported =
          VP8Encoder::SupportsScalabilityMode(*scalability_mode);
    } else if (absl::EqualsIgnoreCase(format.name, cricket::kVp9CodecName)) {
      scalability_mode_supported =
          VP9Encoder::SupportsScalabilityMode(*scalability_mode);
    } else if (absl::EqualsIgnoreCase(format.name, cricket::kH264CodecName)) {
      scalability_mode_supported =
          H264Encoder::SupportsScalabilityMode(*scalability_mode);
    } else if (kIsLibaomAv1EncoderSupported &&
               absl::EqualsIgnoreCase(format.name, cricket::kAv1CodecName)) {
      scalability_mode_supported =
          LibaomAv1EncoderSupportsScalabilityMode(*scalability_mode);
    }

    static constexpr VideoEncoderFactory::CodecSupport kUnsupported = {
        /*is_supported=*/false, /*is_power_efficient=*/false};
    if (!scalability_mode_supported) {
      return kUnsupported;
    }
  }

  CodecSupport codec_support;
  codec_support.is_supported = format.IsCodecInList(GetSupportedFormats());
  return codec_support;
}

}  // namespace webrtc
