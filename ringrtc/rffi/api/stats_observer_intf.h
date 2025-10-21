/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_STATS_OBSERVER_INTF_H__
#define RFFI_API_STATS_OBSERVER_INTF_H__

#include "api/peer_connection_interface.h"
#include "rffi/api/rffi_defs.h"

/**
 * Rust friendly wrapper for creating objects that implement the
 * webrtc::StatsCollector interface.
 *
 */

namespace webrtc {
namespace rffi {
class StatsObserverRffi;
}  // namespace rffi
}  // namespace webrtc

typedef struct {
  uint32_t ssrc;
  uint32_t packets_sent;
  uint64_t bytes_sent;
  int32_t remote_packets_lost;
  double remote_jitter;
  double remote_round_trip_time;
  double total_audio_energy;
} AudioSenderStatistics;

typedef struct {
  uint32_t ssrc;
  uint32_t packets_sent;
  uint64_t bytes_sent;
  uint32_t frames_encoded;
  uint32_t key_frames_encoded;
  double total_encode_time;
  uint32_t frame_width;
  uint32_t frame_height;
  uint64_t retransmitted_packets_sent;
  uint64_t retransmitted_bytes_sent;
  double total_packet_send_delay;
  uint32_t nack_count;
  uint32_t pli_count;
  // 0 - kNone, 1 - kCpu, 2 - kBandwidth, 3 - kOther
  uint32_t quality_limitation_reason;
  uint32_t quality_limitation_resolution_changes;
  int32_t remote_packets_lost;
  double remote_jitter;
  double remote_round_trip_time;
} VideoSenderStatistics;

typedef struct {
  uint32_t ssrc;
  uint32_t packets_received;
  int32_t packets_lost;
  uint64_t bytes_received;
  double jitter;
  double total_audio_energy;
  double jitter_buffer_delay;
  uint64_t jitter_buffer_emitted_count;
  uint64_t jitter_buffer_flushes;
  double estimated_playout_timestamp;
} AudioReceiverStatistics;

typedef struct {
  uint32_t ssrc;
  uint32_t packets_received;
  int32_t packets_lost;
  uint64_t bytes_received;
  uint32_t frames_received;
  uint32_t frames_decoded;
  uint32_t key_frames_decoded;
  double total_decode_time;
  uint32_t frame_width;
  uint32_t frame_height;
  uint32_t freeze_count;
  double total_freezes_duration;
  double jitter;
  double jitter_buffer_delay;
  uint64_t jitter_buffer_emitted_count;
  uint64_t jitter_buffer_flushes;
  double estimated_playout_timestamp;
} VideoReceiverStatistics;

typedef struct {
  const char* raw_candidate_pair_id;
  double current_round_trip_time;
  double available_outgoing_bitrate;
  uint64_t requests_sent;
  uint64_t responses_received;
  uint64_t requests_received;
  uint64_t responses_sent;
} ConnectionStatistics;

typedef struct {
  int64_t timestamp_us;
  uint32_t audio_sender_statistics_size;
  const AudioSenderStatistics* audio_sender_statistics;
  uint32_t video_sender_statistics_size;
  const VideoSenderStatistics* video_sender_statistics;
  uint32_t audio_receiver_statistics_size;
  const AudioReceiverStatistics* audio_receiver_statistics;
  uint32_t video_receiver_statistics_size;
  const VideoReceiverStatistics* video_receiver_statistics;
  ConnectionStatistics nominated_connection_statistics;
  const ConnectionStatistics* connection_statistics;
  uint32_t connection_statistics_size;
} MediaStatistics;

/* Stats Observer Callback callback function pointers */
typedef struct {
  void (*OnStatsComplete)(void* stats_observer_borrowed,
                          const MediaStatistics* media_statistics_borrowed,
                          const char* report_json_borrowed);
} StatsObserverCallbacks;

RUSTEXPORT webrtc::rffi::StatsObserverRffi* Rust_createStatsObserver(
    void* stats_observer_borrowed,
    const StatsObserverCallbacks* stats_observer_cbs_borrowed);

RUSTEXPORT void Rust_setCollectRawStatsReport(
    webrtc::rffi::StatsObserverRffi* stats_observer_borrowed,
    bool collect_raw_stats_report);

#endif /* RFFI_API_STATS_OBSERVER_INTF_H__ */
