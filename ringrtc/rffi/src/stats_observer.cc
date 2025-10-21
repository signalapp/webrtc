/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/src/stats_observer.h"

#include "api/stats/rtcstats_objects.h"
#include "rffi/api/stats_observer_intf.h"
#include "rffi/src/ptr.h"

namespace webrtc {
namespace rffi {

StatsObserverRffi::StatsObserverRffi(
    void* stats_observer,
    const StatsObserverCallbacks* stats_observer_cbs)
    : stats_observer_(stats_observer),
      stats_observer_cbs_(*stats_observer_cbs) {
  RTC_LOG(LS_INFO) << "StatsObserverRffi:ctor(): " << this->stats_observer_;
}

StatsObserverRffi::~StatsObserverRffi() {
  RTC_LOG(LS_INFO) << "StatsObserverRffi:dtor(): " << this->stats_observer_;
}

void StatsObserverRffi::SetCollectRawStatsReport(
    bool collect_raw_stats_report) {
  this->collect_raw_stats_report_.store(collect_raw_stats_report);
}

void StatsObserverRffi::OnStatsDelivered(
    const scoped_refptr<const RTCStatsReport>& report) {
  this->audio_sender_statistics_.clear();
  this->video_sender_statistics_.clear();
  this->audio_receiver_statistics_.clear();
  this->video_receiver_statistics_.clear();
  this->connection_statistics_.clear();

  auto outbound_stream_stats =
      report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  auto inbound_stream_stats =
      report->GetStatsOfType<RTCInboundRtpStreamStats>();
  auto candidate_pair_stats =
      report->GetStatsOfType<RTCIceCandidatePairStats>();

  for (const auto& stat : outbound_stream_stats) {
    if (*stat->kind == "audio" &&
        (*stat->mid == "audio" ||
         absl::StartsWith(*stat->mid, "local-audio"))) {
      AudioSenderStatistics audio_sender = {0};

      audio_sender.ssrc = stat->ssrc.value_or(0);
      audio_sender.packets_sent = stat->packets_sent.value_or(0);
      audio_sender.bytes_sent = stat->bytes_sent.value_or(0);

      if (stat->remote_id.has_value()) {
        auto remote_stat =
            report->GetAs<RTCRemoteInboundRtpStreamStats>(*stat->remote_id);
        if (remote_stat) {
          audio_sender.remote_packets_lost =
              remote_stat->packets_lost.value_or(0);
          audio_sender.remote_jitter = remote_stat->jitter.value_or(0.0);
          audio_sender.remote_round_trip_time =
              remote_stat->round_trip_time.value_or(0.0);
        }
      }

      if (stat->media_source_id.has_value()) {
        auto audio_source_stat =
            report->GetAs<RTCAudioSourceStats>(*stat->media_source_id);
        if (audio_source_stat) {
          audio_sender.total_audio_energy =
              audio_source_stat->total_audio_energy.value_or(0.0);
        }
      }

      this->audio_sender_statistics_.push_back(audio_sender);
    } else if (*stat->kind == "video" &&
               (*stat->mid == "video" ||
                absl::StartsWith(*stat->mid, "local-video"))) {
      VideoSenderStatistics video_sender = {0};

      video_sender.ssrc = stat->ssrc.value_or(0);
      video_sender.packets_sent = stat->packets_sent.value_or(0);
      video_sender.bytes_sent = stat->bytes_sent.value_or(0);
      video_sender.frames_encoded = stat->frames_encoded.value_or(0);
      video_sender.key_frames_encoded = stat->key_frames_encoded.value_or(0);
      video_sender.total_encode_time = stat->total_encode_time.value_or(0.0);
      video_sender.frame_width = stat->frame_width.value_or(0);
      video_sender.frame_height = stat->frame_height.value_or(0);
      video_sender.retransmitted_packets_sent =
          stat->retransmitted_packets_sent.value_or(0);
      video_sender.retransmitted_bytes_sent =
          stat->retransmitted_bytes_sent.value_or(0);
      video_sender.total_packet_send_delay =
          stat->total_packet_send_delay.value_or(0.0);
      video_sender.nack_count = stat->nack_count.value_or(0);
      video_sender.pli_count = stat->pli_count.value_or(0);
      if (stat->quality_limitation_reason.has_value()) {
        // "none" = 0 (the default)
        if (*stat->quality_limitation_reason == "cpu") {
          video_sender.quality_limitation_reason = 1;
        } else if (*stat->quality_limitation_reason == "bandwidth") {
          video_sender.quality_limitation_reason = 2;
        } else {
          video_sender.quality_limitation_reason = 3;
        }
      }
      video_sender.quality_limitation_resolution_changes =
          stat->quality_limitation_resolution_changes.value_or(0);

      if (stat->remote_id.has_value()) {
        auto remote_stat =
            report->GetAs<RTCRemoteInboundRtpStreamStats>(*stat->remote_id);
        if (remote_stat) {
          video_sender.remote_packets_lost =
              remote_stat->packets_lost.value_or(0);
          video_sender.remote_jitter = remote_stat->jitter.value_or(0.0);
          video_sender.remote_round_trip_time =
              remote_stat->round_trip_time.value_or(0.0);
        }
      }

      this->video_sender_statistics_.push_back(video_sender);
    }
  }

  for (const auto& stat : inbound_stream_stats) {
    if (*stat->kind == "audio" &&
        (*stat->mid == "audio" ||
         absl::StartsWith(*stat->mid, "remote-audio"))) {
      AudioReceiverStatistics audio_receiver = {0};

      audio_receiver.ssrc = stat->ssrc.value_or(0);
      audio_receiver.packets_received = stat->packets_received.value_or(0);
      audio_receiver.packets_lost = stat->packets_lost.value_or(0);
      audio_receiver.bytes_received = stat->bytes_received.value_or(0);
      audio_receiver.jitter = stat->jitter.value_or(0.0);
      audio_receiver.total_audio_energy =
          stat->total_audio_energy.value_or(0.0);
      audio_receiver.jitter_buffer_delay =
          stat->jitter_buffer_delay.value_or(0.0);
      audio_receiver.jitter_buffer_emitted_count =
          stat->jitter_buffer_emitted_count.value_or(0);
      audio_receiver.jitter_buffer_flushes =
          stat->jitter_buffer_flushes.value_or(0);
      audio_receiver.estimated_playout_timestamp =
          stat->estimated_playout_timestamp.value_or(0.0);

      this->audio_receiver_statistics_.push_back(audio_receiver);
    } else if (*stat->kind == "video" &&
               (*stat->mid == "video" ||
                absl::StartsWith(*stat->mid, "remote-video"))) {
      VideoReceiverStatistics video_receiver = {0};

      video_receiver.ssrc = stat->ssrc.value_or(0);
      video_receiver.packets_received = stat->packets_received.value_or(0);
      video_receiver.packets_lost = stat->packets_lost.value_or(0);
      video_receiver.bytes_received = stat->bytes_received.value_or(0);
      video_receiver.frames_received = stat->frames_received.value_or(0);
      video_receiver.frames_decoded = stat->frames_decoded.value_or(0);
      video_receiver.key_frames_decoded = stat->key_frames_decoded.value_or(0);
      video_receiver.total_decode_time = stat->total_decode_time.value_or(0.0);
      video_receiver.frame_width = stat->frame_width.value_or(0);
      video_receiver.frame_height = stat->frame_height.value_or(0);
      video_receiver.jitter = stat->jitter.value_or(0.0);
      video_receiver.jitter_buffer_delay =
          stat->jitter_buffer_delay.value_or(0.0);
      video_receiver.jitter_buffer_emitted_count =
          stat->jitter_buffer_emitted_count.value_or(0);
      video_receiver.jitter_buffer_flushes =
          stat->jitter_buffer_flushes.value_or(0);
      video_receiver.freeze_count = stat->freeze_count.value_or(0);
      video_receiver.total_freezes_duration =
          stat->total_freezes_duration.value_or(0.0);
      video_receiver.estimated_playout_timestamp =
          stat->estimated_playout_timestamp.value_or(0.0);

      this->video_receiver_statistics_.push_back(video_receiver);
    }
  }

  ConnectionStatistics nominated_connection_statistics = {0};
  uint64_t highest_priority = 0;
  for (const auto& stat : candidate_pair_stats) {
    ConnectionStatistics connection_stats = {0};
    connection_stats.raw_candidate_pair_id = stat->id().c_str();
    connection_stats.current_round_trip_time =
        stat->current_round_trip_time.value_or(0.0);
    connection_stats.available_outgoing_bitrate =
        stat->available_outgoing_bitrate.value_or(0.0);
    connection_stats.requests_sent = stat->requests_sent.value_or(0);
    connection_stats.responses_received = stat->responses_received.value_or(0);
    connection_stats.requests_received = stat->requests_received.value_or(0);
    connection_stats.responses_sent = stat->responses_sent.value_or(0);

    // We'll only look at the pair that is nominated with the highest priority,
    // usually that has useful values (there does not seem to be a 'in_use' type
    // of flag).
    uint64_t current_priority = stat->priority.value_or(0);
    if (*stat->nominated && stat->priority.value_or(0) > highest_priority) {
      highest_priority = current_priority;
      nominated_connection_statistics = connection_stats;
    }

    this->connection_statistics_.push_back(connection_stats);
  }

  MediaStatistics media_statistics;
  media_statistics.timestamp_us = report->timestamp().us_or(-1);
  media_statistics.audio_sender_statistics_size =
      this->audio_sender_statistics_.size();
  media_statistics.audio_sender_statistics =
      this->audio_sender_statistics_.data();
  media_statistics.video_sender_statistics_size =
      this->video_sender_statistics_.size();
  media_statistics.video_sender_statistics =
      this->video_sender_statistics_.data();
  media_statistics.audio_receiver_statistics_size =
      this->audio_receiver_statistics_.size();
  media_statistics.audio_receiver_statistics =
      this->audio_receiver_statistics_.data();
  media_statistics.video_receiver_statistics_size =
      this->video_receiver_statistics_.size();
  media_statistics.video_receiver_statistics =
      this->video_receiver_statistics_.data();
  media_statistics.nominated_connection_statistics =
      nominated_connection_statistics;
  media_statistics.connection_statistics_size =
      this->connection_statistics_.size();
  media_statistics.connection_statistics = this->connection_statistics_.data();

  std::string report_json =
      this->collect_raw_stats_report_.load() ? report->ToJson() : "";
  // Pass media_statistics up to Rust, which will consume the data before
  // returning.
  this->stats_observer_cbs_.OnStatsComplete(
      this->stats_observer_, &media_statistics, report_json.c_str());
}

// Returns an owned RC.
// Pass-in values must outlive the returned value.
RUSTEXPORT StatsObserverRffi* Rust_createStatsObserver(
    void* stats_observer_borrowed,
    const StatsObserverCallbacks* stats_observer_cbs_borrowed) {
  return take_rc(make_ref_counted<StatsObserverRffi>(
      stats_observer_borrowed, stats_observer_cbs_borrowed));
}

RUSTEXPORT void Rust_setCollectRawStatsReport(
    rffi::StatsObserverRffi* stats_observer_borrowed,
    bool collect_raw_stats_report) {
  stats_observer_borrowed->SetCollectRawStatsReport(collect_raw_stats_report);
}

}  // namespace rffi
}  // namespace webrtc
