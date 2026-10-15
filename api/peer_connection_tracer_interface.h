/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_PEER_CONNECTION_TRACER_INTERFACE_H_
#define API_PEER_CONNECTION_TRACER_INTERFACE_H_

#include "absl/strings/string_view.h"
#include "api/data_channel_interface.h"
#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

// PeerConnectionTracerInterface is a passive, signaling-thread observer of
// PeerConnection lifecycle and operation events. It is intended for
// diagnostics and trace surfaces (e.g. chrome://webrtc-internals); it must
// not influence PeerConnection behaviour.
//
// Threading: every method is invoked on the PeerConnection's signaling
// thread. Implementations must not block; opening each method with
// RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS() (rtc_base/thread.h) is
// strongly recommended to catch accidental blocking calls in debug builds.
//
// Lifetime: an implementation is supplied via PeerConnectionDependencies at
// construction time and is owned by the PeerConnection for its entire
// lifetime. It cannot be reset or replaced.
//
// All methods are pure virtual: embedders must spell out every event,
// using `{}` for the ones they don't care about. Adding a method here is
// a breaking change for every implementer.
//
// Payloads are passed by const pointer/reference into long-lived objects
// owned by the PeerConnection. The tracer must not retain these pointers
// past the call; it should serialize lazily, only on demand. Passing
// pointers (rather than pre-serialized strings) keeps the cost near zero
// when no tracer is attached or when a particular event is unobserved.
//
// This interface is not the place to observe per-message data-channel
// traffic; use DataChannelEventObserverInterface for that. An embedder
// that wants both can install both at construction time.
class RTC_EXPORT PeerConnectionTracerInterface {
 public:
  virtual ~PeerConnectionTracerInterface() = default;

  // CreateOffer was called by the application; OnCreateOfferSuccess /
  // OnCreateOfferFailure fires when the operation resolves. The SDP type
  // is recoverable via description->GetType() (returns webrtc::SdpType).
  virtual void OnCreateOffer(
      const PeerConnectionInterface::RTCOfferAnswerOptions& options) = 0;
  virtual void OnCreateOfferSuccess(
      const SessionDescriptionInterface* description) = 0;
  virtual void OnCreateOfferFailure(const RTCError& error) = 0;

  // CreateAnswer was called by the application.
  virtual void OnCreateAnswer(
      const PeerConnectionInterface::RTCOfferAnswerOptions& options) = 0;
  virtual void OnCreateAnswerSuccess(
      const SessionDescriptionInterface* description) = 0;
  virtual void OnCreateAnswerFailure(const RTCError& error) = 0;

  // SetLocalDescription was called. `description` is null for the no-arg
  // overload, where the PC produces the SDP internally; success then
  // carries the description that was applied.
  virtual void OnSetLocalDescription(
      const SessionDescriptionInterface* description) = 0;
  virtual void OnSetLocalDescriptionSuccess(
      const SessionDescriptionInterface* description) = 0;
  virtual void OnSetLocalDescriptionFailure(const RTCError& error) = 0;

  // SetRemoteDescription was called. Success carries no payload because
  // the description was supplied by the caller.
  virtual void OnSetRemoteDescription(
      const SessionDescriptionInterface* description) = 0;
  virtual void OnSetRemoteDescriptionSuccess() = 0;
  virtual void OnSetRemoteDescriptionFailure(const RTCError& error) = 0;

  // SetConfiguration was called. Fired only after configuration has been
  // validated and applied successfully.
  virtual void OnSetConfiguration(
      const PeerConnectionInterface::RTCConfiguration& configuration) = 0;

  // PeerConnection::Close was called.
  virtual void OnClose() = 0;

  // The local ICE agent gathered a candidate that will be signaled to
  // the remote peer (mirrors the PeerConnectionObserver::OnIceCandidate
  // callback).
  virtual void OnIceCandidate(const IceCandidate& candidate) = 0;

  // The application called PeerConnection::AddIceCandidate with a
  // candidate received from the remote peer over signaling. `succeeded`
  // indicates whether the candidate was accepted.
  virtual void OnAddIceCandidate(const IceCandidate& candidate,
                                 bool succeeded) = 0;

  // Local ICE candidate gathering produced an error.
  virtual void OnIceCandidateError(absl::string_view address,
                                   int port,
                                   absl::string_view url,
                                   int error_code,
                                   absl::string_view error_text) = 0;

  // A data channel was created locally via PeerConnection::CreateDataChannel.
  virtual void OnCreateDataChannel(const DataChannelInterface& channel) = 0;

  // A peer-initiated data channel was surfaced via the OnDataChannel observer
  // callback.
  virtual void OnDataChannel(const DataChannelInterface& channel) = 0;

  // State-change events. These are fired in addition to (not in place of)
  // the equivalent PeerConnectionObserver callbacks.
  virtual void OnSignalingStateChanged(
      PeerConnectionInterface::SignalingState state) = 0;
  virtual void OnIceConnectionStateChanged(
      PeerConnectionInterface::IceConnectionState state) = 0;
  virtual void OnConnectionStateChanged(
      PeerConnectionInterface::PeerConnectionState state) = 0;
  virtual void OnIceGatheringStateChanged(
      PeerConnectionInterface::IceGatheringState state) = 0;

  // Gated by ShouldFireNegotiationNeededEvent().
  virtual void OnNegotiationNeededEvent() = 0;
};

}  // namespace webrtc

#endif  // API_PEER_CONNECTION_TRACER_INTERFACE_H_
