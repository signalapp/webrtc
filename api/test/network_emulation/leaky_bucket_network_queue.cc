/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/test/network_emulation/leaky_bucket_network_queue.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "api/sequence_checker.h"
#include "api/test/simulated_network.h"
#include "api/units/timestamp.h"
#include "rtc_base/checks.h"

namespace webrtc {

LeakyBucketNetworkQueue::LeakyBucketNetworkQueue(size_t max_packet_capacity)
    : max_packet_capacity_(
          std::min(max_packet_capacity,
                   LeakyBucketNetworkQueue::kMaxPacketCapacity)) {
  sequence_checker_.Detach();
}

bool LeakyBucketNetworkQueue::EnqueuePacket(
    const PacketInFlightInfo& packet_info) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (max_packet_capacity_ == queue_.size()) {
    return false;
  }
  queue_.push(packet_info);
  return true;
}

std::optional<PacketInFlightInfo> LeakyBucketNetworkQueue::PeekNextPacket()
    const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (queue_.empty()) {
    return std::nullopt;
  }
  return queue_.front();
}

std::optional<PacketInFlightInfo> LeakyBucketNetworkQueue::DequeuePacket(
    Timestamp time_now) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (queue_.empty()) {
    return std::nullopt;
  }
  RTC_DCHECK_LE(queue_.front().send_time(), time_now);
  PacketInFlightInfo packet_info = queue_.front();
  queue_.pop();
  return packet_info;
}

bool LeakyBucketNetworkQueue::empty() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return queue_.empty();
}

void LeakyBucketNetworkQueue::DropOldestPacket() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  dropped_packets_.push_back(queue_.front());
  queue_.pop();
}

std::vector<PacketInFlightInfo>
LeakyBucketNetworkQueue::DequeueDroppedPackets() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  std::vector<PacketInFlightInfo> dropped_packets;
  dropped_packets.swap(dropped_packets_);
  return dropped_packets;
}

}  // namespace webrtc
