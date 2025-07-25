/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/encoder_rtcp_feedback.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/rtp_rtcp/source/rtp_sequence_number_map.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/keyframe_interval_settings.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

namespace {
constexpr int kMinKeyframeSendIntervalMs = 300;
}  // namespace

EncoderRtcpFeedback::EncoderRtcpFeedback(
    const Environment& env,
    bool per_layer_keyframes,
    const std::vector<uint32_t>& ssrcs,
    VideoStreamEncoderInterface* encoder,
    std::function<std::vector<RtpSequenceNumberMap::Info>(
        uint32_t ssrc,
        const std::vector<uint16_t>& seq_nums)> get_packet_infos)
    : env_(env),
      ssrcs_(ssrcs),
      per_layer_keyframes_(per_layer_keyframes),
      get_packet_infos_(std::move(get_packet_infos)),
      video_stream_encoder_(encoder),
      // RingRTC change to enable per-layer PLI for screen sharing
      time_last_packet_delivery_queue_(ssrcs.size(),
                                       Timestamp::Zero()),
      min_keyframe_send_interval_(
          TimeDelta::Millis(KeyframeIntervalSettings(env_.field_trials())
                                .MinKeyframeSendIntervalMs()
                                .value_or(kMinKeyframeSendIntervalMs))) {
  RTC_DCHECK(!ssrcs.empty());
  packet_delivery_queue_.Detach();
}

// Called via Call::DeliverRtcp.
void EncoderRtcpFeedback::OnReceivedIntraFrameRequest(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&packet_delivery_queue_);
  RTC_DCHECK(std::find(ssrcs_.begin(), ssrcs_.end(), ssrc) != ssrcs_.end());

  auto it = std::find(ssrcs_.begin(), ssrcs_.end(), ssrc);
  if (it == ssrcs_.end()) {
    RTC_LOG(LS_WARNING) << "SSRC " << ssrc << " not found.";
    return;
  }
  // RingRTC change to enable per-layer PLI for screen sharing
  bool per_layer_keyframes = per_layer_keyframes_.load();
  size_t ssrc_index =
      per_layer_keyframes ? std::distance(ssrcs_.begin(), it) : 0;
  RTC_CHECK_LE(ssrc_index, time_last_packet_delivery_queue_.size());
  const Timestamp now = env_.clock().CurrentTime();
  if (time_last_packet_delivery_queue_[ssrc_index] +
          min_keyframe_send_interval_ >
      now)
    return;

  time_last_packet_delivery_queue_[ssrc_index] = now;

  std::vector<VideoFrameType> layers(ssrcs_.size(),
                                     VideoFrameType::kVideoFrameDelta);
  if (!per_layer_keyframes) {
    // Always produce key frame for all streams.
    video_stream_encoder_->SendKeyFrame();
  } else {
    // Determine on which layer we ask for key frames.
    layers[ssrc_index] = VideoFrameType::kVideoFrameKey;
    video_stream_encoder_->SendKeyFrame(layers);
  }
}

void EncoderRtcpFeedback::OnReceivedLossNotification(
    uint32_t ssrc,
    uint16_t seq_num_of_last_decodable,
    uint16_t seq_num_of_last_received,
    bool decodability_flag) {
  RTC_DCHECK(get_packet_infos_) << "Object initialization incomplete.";

  const std::vector<uint16_t> seq_nums = {seq_num_of_last_decodable,
                                          seq_num_of_last_received};
  const std::vector<RtpSequenceNumberMap::Info> infos =
      get_packet_infos_(ssrc, seq_nums);
  if (infos.empty()) {
    return;
  }
  RTC_DCHECK_EQ(infos.size(), 2u);

  const RtpSequenceNumberMap::Info& last_decodable = infos[0];
  const RtpSequenceNumberMap::Info& last_received = infos[1];

  VideoEncoder::LossNotification loss_notification;
  loss_notification.timestamp_of_last_decodable = last_decodable.timestamp;
  loss_notification.timestamp_of_last_received = last_received.timestamp;

  // Deduce decodability of the last received frame and of its dependencies.
  if (last_received.is_first && last_received.is_last) {
    // The frame consists of a single packet, and that packet has evidently
    // been received in full; the frame is therefore assemblable.
    // In this case, the decodability of the dependencies is communicated by
    // the decodability flag, and the frame itself is decodable if and only
    // if they are decodable.
    loss_notification.dependencies_of_last_received_decodable =
        decodability_flag;
    loss_notification.last_received_decodable = decodability_flag;
  } else if (last_received.is_first && !last_received.is_last) {
    // In this case, the decodability flag communicates the decodability of
    // the dependencies. If any is undecodable, we also know that the frame
    // itself will not be decodable; if all are decodable, the frame's own
    // decodability will remain unknown, as not all of its packets have
    // been received.
    loss_notification.dependencies_of_last_received_decodable =
        decodability_flag;
    loss_notification.last_received_decodable =
        !decodability_flag ? std::make_optional(false) : std::nullopt;
  } else if (!last_received.is_first && last_received.is_last) {
    if (decodability_flag) {
      // The frame has been received in full, and found to be decodable.
      // (Messages of this type are not sent by WebRTC at the moment, but are
      // theoretically possible, for example for serving as acks.)
      loss_notification.dependencies_of_last_received_decodable = true;
      loss_notification.last_received_decodable = true;
    } else {
      // It is impossible to tell whether some dependencies were undecodable,
      // or whether the frame was unassemblable, but in either case, the frame
      // itself was undecodable.
      loss_notification.dependencies_of_last_received_decodable = std::nullopt;
      loss_notification.last_received_decodable = false;
    }
  } else {  // !last_received.is_first && !last_received.is_last
    if (decodability_flag) {
      // The frame has not yet been received in full, but no gaps have
      // been encountered so far, and the dependencies were all decodable.
      // (Messages of this type are not sent by WebRTC at the moment, but are
      // theoretically possible, for example for serving as acks.)
      loss_notification.dependencies_of_last_received_decodable = true;
      loss_notification.last_received_decodable = std::nullopt;
    } else {
      // It is impossible to tell whether some dependencies were undecodable,
      // or whether the frame was unassemblable, but in either case, the frame
      // itself was undecodable.
      loss_notification.dependencies_of_last_received_decodable = std::nullopt;
      loss_notification.last_received_decodable = false;
    }
  }

  video_stream_encoder_->OnLossNotification(loss_notification);
}

}  // namespace webrtc
