/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/data_channel_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/peer_connection_tracer_interface.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_wrapper.h"
#include "pc/test/enable_fake_media.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using RTCConfiguration = PeerConnectionInterface::RTCConfiguration;

// Records how many times each tracer method has been invoked. Tests retain a
// raw pointer to the installed tracer (the PC owns it) and assert against the
// counts after driving operations.
class CountingTracer : public PeerConnectionTracerInterface {
 public:
  struct Counts {
    int create_offer = 0;
    int create_offer_success = 0;
    int create_offer_failure = 0;
    int create_answer = 0;
    int create_answer_success = 0;
    int create_answer_failure = 0;
    int set_local_description = 0;
    int set_local_description_implicit = 0;
    int set_local_description_success = 0;
    int set_local_description_success_with_sdp = 0;
    int set_local_description_failure = 0;
    int set_remote_description = 0;
    int set_remote_description_success = 0;
    int set_remote_description_failure = 0;
    int set_configuration = 0;
    int close = 0;
    int ice_candidate = 0;
    int add_ice_candidate = 0;
    int ice_candidate_error = 0;
    int data_channel_created_local = 0;
    int data_channel_created_remote = 0;
    int signaling_state_changed = 0;
    int ice_connection_state_changed = 0;
    int connection_state_changed = 0;
    int ice_gathering_state_changed = 0;
    int negotiation_needed_event = 0;
  };

  const Counts& counts() const { return counts_; }

  void OnCreateOffer(
      const PeerConnectionInterface::RTCOfferAnswerOptions&) override {
    counts_.create_offer++;
  }
  void OnCreateOfferSuccess(const SessionDescriptionInterface*) override {
    counts_.create_offer_success++;
  }
  void OnCreateOfferFailure(const RTCError&) override {
    counts_.create_offer_failure++;
  }
  void OnCreateAnswer(
      const PeerConnectionInterface::RTCOfferAnswerOptions&) override {
    counts_.create_answer++;
  }
  void OnCreateAnswerSuccess(const SessionDescriptionInterface*) override {
    counts_.create_answer_success++;
  }
  void OnCreateAnswerFailure(const RTCError&) override {
    counts_.create_answer_failure++;
  }
  void OnSetLocalDescription(
      const SessionDescriptionInterface* description) override {
    counts_.set_local_description++;
    if (description == nullptr) {
      counts_.set_local_description_implicit++;
    }
  }
  void OnSetLocalDescriptionSuccess(
      const SessionDescriptionInterface* description) override {
    counts_.set_local_description_success++;
    if (description != nullptr) {
      counts_.set_local_description_success_with_sdp++;
    }
  }
  void OnSetLocalDescriptionFailure(const RTCError&) override {
    counts_.set_local_description_failure++;
  }
  void OnSetRemoteDescription(const SessionDescriptionInterface*) override {
    counts_.set_remote_description++;
  }
  void OnSetRemoteDescriptionSuccess() override {
    counts_.set_remote_description_success++;
  }
  void OnSetRemoteDescriptionFailure(const RTCError&) override {
    counts_.set_remote_description_failure++;
  }
  void OnSetConfiguration(
      const PeerConnectionInterface::RTCConfiguration&) override {
    counts_.set_configuration++;
  }
  void OnClose() override { counts_.close++; }
  void OnIceCandidate(const IceCandidate&) override { counts_.ice_candidate++; }
  void OnAddIceCandidate(const IceCandidate&, bool) override {
    counts_.add_ice_candidate++;
  }
  void OnIceCandidateError(absl::string_view,
                           int,
                           absl::string_view,
                           int,
                           absl::string_view) override {
    counts_.ice_candidate_error++;
  }
  void OnCreateDataChannel(const DataChannelInterface&) override {
    counts_.data_channel_created_local++;
  }
  void OnDataChannel(const DataChannelInterface&) override {
    counts_.data_channel_created_remote++;
  }
  void OnSignalingStateChanged(
      PeerConnectionInterface::SignalingState) override {
    counts_.signaling_state_changed++;
  }
  void OnIceConnectionStateChanged(
      PeerConnectionInterface::IceConnectionState) override {
    counts_.ice_connection_state_changed++;
  }
  void OnConnectionStateChanged(
      PeerConnectionInterface::PeerConnectionState) override {
    counts_.connection_state_changed++;
  }
  void OnIceGatheringStateChanged(
      PeerConnectionInterface::IceGatheringState) override {
    counts_.ice_gathering_state_changed++;
  }
  void OnNegotiationNeededEvent() override {
    counts_.negotiation_needed_event++;
  }

 private:
  Counts counts_;
};

PeerConnectionFactoryDependencies CreateFactoryDependencies() {
  PeerConnectionFactoryDependencies deps;
  deps.env = CreateTestEnvironment();
  deps.network_thread = Thread::Current();
  deps.worker_thread = Thread::Current();
  deps.signaling_thread = Thread::Current();
  EnableFakeMedia(deps);
  return deps;
}

class PeerConnectionTracerTest : public ::testing::Test {
 protected:
  PeerConnectionTracerTest()
      : vss_(std::make_unique<VirtualSocketServer>()), main_(vss_.get()) {
    pc_factory_ =
        CreateModularPeerConnectionFactory(CreateFactoryDependencies());
  }

  // Creates a PC with a freshly attached CountingTracer. The tracer is owned
  // by the PC; tests recover it via Counts() (or by hand through
  // pc->GetInternalPeerConnection()->tracer()).
  std::unique_ptr<PeerConnectionWrapper> CreatePeerConnection() {
    auto observer = std::make_unique<MockPeerConnectionObserver>();
    PeerConnectionDependencies deps(observer.get());
    deps.tracer = std::make_unique<CountingTracer>();
    RTCConfiguration config;
    config.sdp_semantics = SdpSemantics::kUnifiedPlan;
    auto result =
        pc_factory_->CreatePeerConnectionOrError(config, std::move(deps));
    if (!result.ok()) {
      return nullptr;
    }
    observer->SetPeerConnectionInterface(result.value().get());
    return std::make_unique<PeerConnectionWrapper>(
        pc_factory_, result.MoveValue(), std::move(observer));
  }

  // Returns the CountingTracer attached to `pc`, recovered through the
  // tracer() accessor on PeerConnection.
  const CountingTracer::Counts& Counts(const PeerConnectionWrapper& pc) {
    return static_cast<const CountingTracer*>(
               pc.GetInternalPeerConnection()->tracer())
        ->counts();
  }

  std::unique_ptr<VirtualSocketServer> vss_;
  test::RunLoop main_;
  scoped_refptr<PeerConnectionFactoryInterface> pc_factory_;
};

// At construction time the tracer should see exactly one
// OnSetConfiguration (for the initial configuration) and nothing else.
TEST_F(PeerConnectionTracerTest, FiresOnConstruction) {
  auto pc = CreatePeerConnection();
  ASSERT_TRUE(pc);

  EXPECT_EQ(Counts(*pc).set_configuration, 1);
  EXPECT_EQ(Counts(*pc).create_offer, 0);
  EXPECT_EQ(Counts(*pc).close, 0);
}

// CreateOffer should fire OnCreateOffer once and OnCreateOfferSuccess once
// when the operation resolves successfully.
TEST_F(PeerConnectionTracerTest, FiresOnCreateOffer) {
  auto pc = CreatePeerConnection();
  ASSERT_TRUE(pc);
  pc->AddAudioTrack("a");

  auto offer = pc->CreateOffer();
  ASSERT_TRUE(offer);
  EXPECT_EQ(Counts(*pc).create_offer, 1);
  EXPECT_EQ(Counts(*pc).create_offer_success, 1);
  EXPECT_EQ(Counts(*pc).create_offer_failure, 0);
}

// SetLocalDescription with an explicit description should fire
// OnSetLocalDescription with that description and report success.
TEST_F(PeerConnectionTracerTest, FiresOnSetLocalDescription) {
  auto pc = CreatePeerConnection();
  ASSERT_TRUE(pc);
  pc->AddAudioTrack("a");
  auto offer = pc->CreateOffer();
  ASSERT_TRUE(offer);

  ASSERT_TRUE(pc->SetLocalDescription(std::move(offer)));
  EXPECT_EQ(Counts(*pc).set_local_description, 1);
  EXPECT_EQ(Counts(*pc).set_local_description_implicit, 0);
  EXPECT_EQ(Counts(*pc).set_local_description_success, 1);
  EXPECT_EQ(Counts(*pc).set_local_description_success_with_sdp, 1);
  EXPECT_EQ(Counts(*pc).set_local_description_failure, 0);
}

// CreateDataChannel should fire OnCreateDataChannel.
TEST_F(PeerConnectionTracerTest, FiresOnCreateDataChannel) {
  auto pc = CreatePeerConnection();
  ASSERT_TRUE(pc);

  auto channel = pc->CreateDataChannel("dc");
  ASSERT_TRUE(channel);
  EXPECT_EQ(Counts(*pc).data_channel_created_local, 1);
  EXPECT_EQ(Counts(*pc).data_channel_created_remote, 0);
}

// SetConfiguration should fire OnSetConfiguration in addition to the one
// fired at construction.
TEST_F(PeerConnectionTracerTest, FiresOnSetConfiguration) {
  auto pc = CreatePeerConnection();
  ASSERT_TRUE(pc);
  ASSERT_EQ(Counts(*pc).set_configuration, 1);

  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  ASSERT_TRUE(pc->pc()->SetConfiguration(config).ok());
  EXPECT_EQ(Counts(*pc).set_configuration, 2);
}

// Close should fire OnClose once and at least one signaling-state transition
// (to "closed").
TEST_F(PeerConnectionTracerTest, FiresOnClose) {
  auto pc = CreatePeerConnection();
  ASSERT_TRUE(pc);

  pc->pc()->Close();
  EXPECT_EQ(Counts(*pc).close, 1);
  EXPECT_GE(Counts(*pc).signaling_state_changed, 1);

  // Calling Close again should be a no-op for the tracer.
  pc->pc()->Close();
  EXPECT_EQ(Counts(*pc).close, 1);
}

// The implicit SetLocalDescription overload (no SDP arg) should fire
// OnSetLocalDescription with a null description; the SDP the PeerConnection
// generated is reported on success instead.
TEST_F(PeerConnectionTracerTest, FiresOnSetLocalDescriptionImplicit) {
  auto pc = CreatePeerConnection();
  ASSERT_TRUE(pc);
  pc->AddAudioTrack("a");

  auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  pc->pc()->SetLocalDescription(observer.get());
  EXPECT_EQ(Counts(*pc).set_local_description, 1);
  EXPECT_EQ(Counts(*pc).set_local_description_implicit, 1);

  EXPECT_TRUE(WaitUntil([&] { return observer->called(); }));
  EXPECT_EQ(Counts(*pc).set_local_description_success, 1);
  EXPECT_EQ(Counts(*pc).set_local_description_success_with_sdp, 1);
}

// OnSetRemoteDescription should fire when the application calls
// SetRemoteDescription, not when the operation reaches the front of the chain.
TEST_F(PeerConnectionTracerTest, FiresOnSetRemoteDescription) {
  auto caller = CreatePeerConnection();
  ASSERT_TRUE(caller);
  auto callee = CreatePeerConnection();
  ASSERT_TRUE(callee);
  caller->AddAudioTrack("a");
  auto offer = caller->CreateOffer();
  ASSERT_TRUE(offer);

  // Keep the callee's operations chain busy.
  auto create_observer =
      make_ref_counted<MockCreateSessionDescriptionObserver>();
  callee->pc()->CreateOffer(create_observer.get(),
                            PeerConnectionInterface::RTCOfferAnswerOptions());
  ASSERT_FALSE(create_observer->called());

  auto set_observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  callee->pc()->SetRemoteDescription(set_observer.get(), offer.release());
  EXPECT_EQ(Counts(*callee).set_remote_description, 1);
  EXPECT_EQ(Counts(*callee).set_remote_description_success, 0);

  EXPECT_TRUE(WaitUntil([&] { return set_observer->called(); }));
  EXPECT_EQ(Counts(*callee).set_remote_description_success, 1);
  EXPECT_EQ(Counts(*callee).set_remote_description_failure, 0);
}

}  // namespace
}  // namespace webrtc
