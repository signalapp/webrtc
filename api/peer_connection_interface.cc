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

#include "api/dtls_transport_interface.h"
#include "api/sctp_transport_interface.h"
#include "rtc_base/logging.h"

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

RTCError PeerConnectionInterface::RemoveTrackNew(
    rtc::scoped_refptr<RtpSenderInterface> sender) {
  return RTCError(RemoveTrack(sender) ? RTCErrorType::NONE
                                      : RTCErrorType::INTERNAL_ERROR);
}

RTCError PeerConnectionInterface::SetConfiguration(
    const PeerConnectionInterface::RTCConfiguration& config) {
  return RTCError();
}

// RingRTC change to add ICE forking
rtc::scoped_refptr<webrtc::IceGathererInterface>
PeerConnectionInterface::CreateSharedIceGatherer() {
  RTC_LOG(LS_ERROR) << "No shared ICE gatherer in dummy implementation";
  return nullptr;
}

// RingRTC change to add ICE forking
bool PeerConnectionInterface::UseSharedIceGatherer(
    rtc::scoped_refptr<webrtc::IceGathererInterface> shared_ice_gatherer) {
  RTC_LOG(LS_ERROR) << "No shared ICE gatherer in dummy implementation";
  return false;
}

// RingRTC change to RTP from being processed before the call is accepted
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
bool PeerConnectionInterface::ReceiveRtp(uint8_t pt) {
  RTC_LOG(LS_ERROR) << "No SendRtp in dummy implementation";
  return false;
}

PeerConnectionDependencies::PeerConnectionDependencies(
    PeerConnectionObserver* observer_in)
    : observer(observer_in) {}

PeerConnectionDependencies::PeerConnectionDependencies(
    PeerConnectionDependencies&&) = default;

PeerConnectionDependencies::~PeerConnectionDependencies() = default;

PeerConnectionFactoryDependencies::PeerConnectionFactoryDependencies() =
    default;

PeerConnectionFactoryDependencies::PeerConnectionFactoryDependencies(
    PeerConnectionFactoryDependencies&&) = default;

PeerConnectionFactoryDependencies::~PeerConnectionFactoryDependencies() =
    default;

rtc::scoped_refptr<PeerConnectionInterface>
PeerConnectionFactoryInterface::CreatePeerConnection(
    const PeerConnectionInterface::RTCConfiguration& configuration,
    std::unique_ptr<cricket::PortAllocator> allocator,
    std::unique_ptr<rtc::RTCCertificateGeneratorInterface> cert_generator,
    PeerConnectionObserver* observer) {
  return nullptr;
}

rtc::scoped_refptr<PeerConnectionInterface>
PeerConnectionFactoryInterface::CreatePeerConnection(
    const PeerConnectionInterface::RTCConfiguration& configuration,
    PeerConnectionDependencies dependencies) {
  return nullptr;
}

RTCErrorOr<rtc::scoped_refptr<PeerConnectionInterface>>
PeerConnectionFactoryInterface::CreatePeerConnectionOrError(
    const PeerConnectionInterface::RTCConfiguration& configuration,
    PeerConnectionDependencies dependencies) {
  return RTCError(RTCErrorType::INTERNAL_ERROR);
}

RtpCapabilities PeerConnectionFactoryInterface::GetRtpSenderCapabilities(
    cricket::MediaType kind) const {
  return {};
}

RtpCapabilities PeerConnectionFactoryInterface::GetRtpReceiverCapabilities(
    cricket::MediaType kind) const {
  return {};
}

}  // namespace webrtc
