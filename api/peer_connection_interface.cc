/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/peer_connection_interface.h"

#include "pc/media_factory.h"

namespace webrtc {

PeerConnectionInterface::IceServer::IceServer() = default;
PeerConnectionInterface::IceServer::IceServer(const IceServer& rhs) = default;
PeerConnectionInterface::IceServer::~IceServer() = default;

PeerConnectionInterface::RTCConfiguration::RTCConfiguration() = default;

PeerConnectionInterface::RTCConfiguration::RTCConfiguration(
    const RTCConfiguration& rhs) = default;

PeerConnectionInterface::RTCConfiguration::RTCConfiguration(
    RTCConfigurationType type) {
  if (type == RTCConfigurationType::kAggressive) {
    // These parameters are also defined in Java and IOS configurations,
    // so their values may be overwritten by the Java or IOS configuration.
    bundle_policy = kBundlePolicyMaxBundle;
    rtcp_mux_policy = kRtcpMuxPolicyRequire;
    ice_connection_receiving_timeout = kAggressiveIceConnectionReceivingTimeout;

    // These parameters are not defined in Java or IOS configuration,
    // so their values will not be overwritten.
    enable_ice_renomination = true;
    redetermine_role_on_ice_restart = false;
  }
}

PeerConnectionInterface::RTCConfiguration::~RTCConfiguration() = default;

// RingRTC change to support ICE forking
rtc::scoped_refptr<webrtc::IceGathererInterface>
PeerConnectionInterface::CreateSharedIceGatherer() {
  RTC_LOG(LS_ERROR) << "No shared ICE gatherer in dummy implementation";
  return nullptr;
}

// RingRTC change to support ICE forking
bool PeerConnectionInterface::UseSharedIceGatherer(
    rtc::scoped_refptr<webrtc::IceGathererInterface> shared_ice_gatherer) {
  RTC_LOG(LS_ERROR) << "No shared ICE gatherer in dummy implementation";
  return false;
}

// RingRTC change to explicitly control when incoming packets can be processed
bool PeerConnectionInterface::SetIncomingRtpEnabled(bool enabled) {
  RTC_LOG(LS_ERROR) << "No enabling of incoming RTP in dummy implementation";
  return false;
}

// RingRTC change to send RTP data
bool PeerConnectionInterface::SendRtp(std::unique_ptr<RtpPacket> rtp_packet) {
  RTC_LOG(LS_ERROR) << "No SendRtp in dummy implementation";
  return false;
}

// RingRTC change to receive RTP data
bool PeerConnectionInterface::ReceiveRtp(uint8_t pt, bool enable_incoming) {
  RTC_LOG(LS_ERROR) << "No SendRtp in dummy implementation";
  return false;
}

// RingRTC change to get audio levels
void PeerConnectionInterface::GetAudioLevels(
      uint16_t* captured_out,
      ReceivedAudioLevel* received_out,
      size_t received_out_size,
      size_t* received_size_out) {
  RTC_LOG(LS_ERROR) << "No GetAudioLevels in dummy implementation";
  *received_size_out = 0;
  *captured_out = 0;
}

PeerConnectionDependencies::PeerConnectionDependencies(
    PeerConnectionObserver* observer_in)
    : observer(observer_in) {}

PeerConnectionDependencies::PeerConnectionDependencies(
    PeerConnectionDependencies&&) = default;

PeerConnectionDependencies::~PeerConnectionDependencies() = default;

PeerConnectionFactoryDependencies::PeerConnectionFactoryDependencies() =
    default;

// Allow move constructor to move deprecated members. Pragma can be removed
// when there are no deprecated depedencies at the moment.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
PeerConnectionFactoryDependencies::PeerConnectionFactoryDependencies(
    PeerConnectionFactoryDependencies&&) = default;
#pragma clang diagnostic pop

PeerConnectionFactoryDependencies::~PeerConnectionFactoryDependencies() =
    default;

}  // namespace webrtc
