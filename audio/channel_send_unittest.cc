/*
 *  Copyright 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/channel_send.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "api/array_view.h"
#include "api/audio/audio_frame.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/call/bitrate_allocation.h"
#include "api/call/transport.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/test/mock_frame_transformer.h"
#include "api/test/mock_transformable_audio_frame.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/bitrate_settings.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/rtp_transport_config.h"
#include "call/rtp_transport_controller_send.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/scoped_key_value_config.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {
namespace voe {
namespace {

using ::testing::Eq;
using ::testing::Invoke;
using ::testing::IsTrue;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

constexpr int kRtcpIntervalMs = 1000;
constexpr int kSsrc = 333;
constexpr int kPayloadType = 1;
constexpr int kSampleRateHz = 48000;
constexpr int kRtpRateHz = 48000;

BitrateConstraints GetBitrateConfig() {
  BitrateConstraints bitrate_config;
  bitrate_config.min_bitrate_bps = 10000;
  bitrate_config.start_bitrate_bps = 100000;
  bitrate_config.max_bitrate_bps = 1000000;
  return bitrate_config;
}

class ChannelSendTest : public ::testing::Test {
 protected:
  ChannelSendTest()
      : time_controller_(Timestamp::Seconds(1)),
        env_(CreateEnvironment(&field_trials_,
                               time_controller_.GetClock(),
                               time_controller_.CreateTaskQueueFactory())),
        transport_controller_(
            RtpTransportConfig{.env = env_,
                               .bitrate_config = GetBitrateConfig()}) {
    channel_ = voe::CreateChannelSend(env_, &transport_, nullptr, nullptr,
                                      crypto_options_, false, kRtcpIntervalMs,
                                      kSsrc, nullptr, &transport_controller_);
    encoder_factory_ = CreateBuiltinAudioEncoderFactory();
    SdpAudioFormat opus = SdpAudioFormat("opus", kRtpRateHz, 2);
    std::unique_ptr<AudioEncoder> encoder =
        encoder_factory_->Create(env_, opus, {.payload_type = kPayloadType});
    channel_->SetEncoder(kPayloadType, opus, std::move(encoder));
    transport_controller_.EnsureStarted();
    channel_->RegisterSenderCongestionControlObjects(&transport_controller_);
    ON_CALL(transport_, SendRtcp).WillByDefault(Return(true));
    ON_CALL(transport_, SendRtp).WillByDefault(Return(true));
  }

  std::unique_ptr<AudioFrame> CreateAudioFrame(uint8_t data_init_value = 0) {
    auto frame = std::make_unique<AudioFrame>();
    frame->sample_rate_hz_ = kSampleRateHz;
    frame->samples_per_channel_ = kSampleRateHz / 100;
    frame->num_channels_ = 1;
    frame->set_absolute_capture_timestamp_ms(
        time_controller_.GetClock()->TimeInMilliseconds());
    int16_t* dest = frame->mutable_data();
    for (size_t i = 0; i < frame->samples_per_channel_ * frame->num_channels_;
         i++, dest++) {
      *dest = data_init_value;
    }
    return frame;
  }

  void ProcessNextFrame(std::unique_ptr<AudioFrame> audio_frame) {
    channel_->ProcessAndEncodeAudio(std::move(audio_frame));
    // Advance time to process the task queue.
    time_controller_.AdvanceTime(TimeDelta::Millis(10));
  }

  void ProcessNextFrame() { ProcessNextFrame(CreateAudioFrame()); }

  GlobalSimulatedTimeController time_controller_;
  test::ScopedKeyValueConfig field_trials_;
  Environment env_;
  NiceMock<MockTransport> transport_;
  CryptoOptions crypto_options_;
  RtpTransportControllerSend transport_controller_;
  std::unique_ptr<ChannelSendInterface> channel_;
  scoped_refptr<AudioEncoderFactory> encoder_factory_;
};

TEST_F(ChannelSendTest, StopSendShouldResetEncoder) {
  channel_->StartSend();
  // Insert two frames which should trigger a new packet.
  EXPECT_CALL(transport_, SendRtp).Times(1);
  ProcessNextFrame();
  ProcessNextFrame();

  EXPECT_CALL(transport_, SendRtp).Times(0);
  ProcessNextFrame();
  // StopSend should clear the previous audio frame stored in the encoder.
  channel_->StopSend();

  channel_->StartSend();
  // The following frame should not trigger a new packet since the encoder
  // needs 20 ms audio.
  EXPECT_CALL(transport_, SendRtp).Times(0);
  ProcessNextFrame();
}

TEST_F(ChannelSendTest, IncreaseRtpTimestampByPauseDuration) {
  channel_->StartSend();
  uint32_t timestamp;
  int sent_packets = 0;
  auto send_rtp = [&](ArrayView<const uint8_t> data,
                      const PacketOptions& /* options */) {
    ++sent_packets;
    RtpPacketReceived packet;
    packet.Parse(data);
    timestamp = packet.Timestamp();
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(Invoke(send_rtp));
  ProcessNextFrame();
  ProcessNextFrame();
  EXPECT_EQ(sent_packets, 1);
  uint32_t first_timestamp = timestamp;
  channel_->StopSend();
  time_controller_.AdvanceTime(TimeDelta::Seconds(10));
  channel_->StartSend();

  ProcessNextFrame();
  ProcessNextFrame();
  EXPECT_EQ(sent_packets, 2);
  int64_t timestamp_gap_ms =
      static_cast<int64_t>(timestamp - first_timestamp) * 1000 / kRtpRateHz;
  EXPECT_EQ(timestamp_gap_ms, 10020);
}

TEST_F(ChannelSendTest, FrameTransformerGetsCorrectTimestamp) {
  scoped_refptr<MockFrameTransformer> mock_frame_transformer =
      make_ref_counted<MockFrameTransformer>();
  channel_->SetEncoderToPacketizerFrameTransformer(mock_frame_transformer);
  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*mock_frame_transformer, RegisterTransformedFrameCallback)
      .WillOnce(SaveArg<0>(&callback));
  EXPECT_CALL(*mock_frame_transformer, UnregisterTransformedFrameCallback);

  std::optional<uint32_t> sent_timestamp;
  auto send_rtp = [&](ArrayView<const uint8_t> data,
                      const PacketOptions& /* options */) {
    RtpPacketReceived packet;
    packet.Parse(data);
    if (!sent_timestamp) {
      sent_timestamp = packet.Timestamp();
    }
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(Invoke(send_rtp));

  channel_->StartSend();
  int64_t transformable_frame_timestamp = -1;
  EXPECT_CALL(*mock_frame_transformer, Transform)
      .WillOnce([&](std::unique_ptr<TransformableFrameInterface> frame) {
        transformable_frame_timestamp = frame->GetTimestamp();
        callback->OnTransformedFrame(std::move(frame));
      });
  // Insert two frames which should trigger a new packet.
  ProcessNextFrame();
  ProcessNextFrame();

  // Ensure the RTP timestamp on the frame passed to the transformer
  // includes the RTP offset and matches the actual RTP timestamp on the sent
  // packet.
  EXPECT_THAT(
      WaitUntil([&] { return 0 + channel_->GetRtpRtcp()->StartTimestamp(); },
                Eq(transformable_frame_timestamp),
                // RingRTC change to prevent hang.
                {.clock = static_cast<SimulatedClock*>(time_controller_.GetClock())}),
      IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return sent_timestamp; }, IsTrue(),
                       // RingRTC change to prevent hang.
                       {.clock = static_cast<SimulatedClock*>(time_controller_.GetClock())}),
              IsRtcOk());
  EXPECT_EQ(*sent_timestamp, transformable_frame_timestamp);
}

// Ensure that AudioLevel calculations are performed correctly per-packet even
// if there's an async Encoded Frame Transform happening.
TEST_F(ChannelSendTest, AudioLevelsAttachedToCorrectTransformedFrame) {
  channel_->SetSendAudioLevelIndicationStatus(true, /*id=*/1);
  RtpPacketReceived::ExtensionManager extension_manager;
  extension_manager.RegisterByType(1, kRtpExtensionAudioLevel);

  scoped_refptr<MockFrameTransformer> mock_frame_transformer =
      make_ref_counted<MockFrameTransformer>();
  channel_->SetEncoderToPacketizerFrameTransformer(mock_frame_transformer);
  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*mock_frame_transformer, RegisterTransformedFrameCallback)
      .WillOnce(SaveArg<0>(&callback));
  EXPECT_CALL(*mock_frame_transformer, UnregisterTransformedFrameCallback);

  std::vector<uint8_t> sent_audio_levels;
  auto send_rtp = [&](ArrayView<const uint8_t> data,
                      const PacketOptions& /* options */) {
    RtpPacketReceived packet(&extension_manager);
    packet.Parse(data);
    RTPHeader header;
    packet.GetHeader(&header);
    sent_audio_levels.push_back(header.extension.audio_level()->level());
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(Invoke(send_rtp));

  channel_->StartSend();
  std::vector<std::unique_ptr<TransformableFrameInterface>> frames;
  EXPECT_CALL(*mock_frame_transformer, Transform)
      .Times(2)
      .WillRepeatedly([&](std::unique_ptr<TransformableFrameInterface> frame) {
        frames.push_back(std::move(frame));
      });

  // Insert two frames of 7s which should trigger a new packet.
  ProcessNextFrame(CreateAudioFrame(/*data_init_value=*/7));
  ProcessNextFrame(CreateAudioFrame(/*data_init_value=*/7));

  // Insert two more frames of 3s, meaning a second packet is
  // prepared and sent to the transform before the first packet has
  // been sent.
  ProcessNextFrame(CreateAudioFrame(/*data_init_value=*/3));
  ProcessNextFrame(CreateAudioFrame(/*data_init_value=*/3));

  // Wait for both packets to be encoded and sent to the transform.
  // RingRTC change to prevent hang and crash.
  ASSERT_THAT(WaitUntil([&] { return frames.size(); }, Eq(2ul), {.clock = static_cast<SimulatedClock*>(time_controller_.GetClock())}), IsRtcOk());
  // Complete the transforms on both frames at the same time
  callback->OnTransformedFrame(std::move(frames[0]));
  callback->OnTransformedFrame(std::move(frames[1]));

  // Allow things posted back to the encoder queue to run.
  time_controller_.AdvanceTime(TimeDelta::Millis(10));

  // Ensure the audio levels on both sent packets is present and
  // matches their contents.
  EXPECT_THAT(WaitUntil([&] { return sent_audio_levels.size(); }, Eq(2ul)),
              IsRtcOk());
  // rms dbov of the packet with raw audio of 7s is 73.
  EXPECT_EQ(sent_audio_levels[0], 73);
  // rms dbov of the second packet with raw audio of 3s is 81.
  EXPECT_EQ(sent_audio_levels[1], 81);
}

// Ensure that AudioLevels are attached to frames injected into the
// Encoded Frame transform.
TEST_F(ChannelSendTest, AudioLevelsAttachedToInsertedTransformedFrame) {
  channel_->SetSendAudioLevelIndicationStatus(true, /*id=*/1);
  RtpPacketReceived::ExtensionManager extension_manager;
  extension_manager.RegisterByType(1, kRtpExtensionAudioLevel);

  scoped_refptr<MockFrameTransformer> mock_frame_transformer =
      make_ref_counted<MockFrameTransformer>();
  channel_->SetEncoderToPacketizerFrameTransformer(mock_frame_transformer);
  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*mock_frame_transformer, RegisterTransformedFrameCallback)
      .WillOnce(SaveArg<0>(&callback));
  EXPECT_CALL(*mock_frame_transformer, UnregisterTransformedFrameCallback);

  std::optional<uint8_t> sent_audio_level;
  auto send_rtp = [&](ArrayView<const uint8_t> data,
                      const PacketOptions& /* options */) {
    RtpPacketReceived packet(&extension_manager);
    packet.Parse(data);
    RTPHeader header;
    packet.GetHeader(&header);
    sent_audio_level = header.extension.audio_level()->level();
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(Invoke(send_rtp));

  channel_->StartSend();

  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  // Inject a frame encoded elsewhere.
  auto mock_frame = std::make_unique<NiceMock<MockTransformableAudioFrame>>();
  uint8_t audio_level = 67;
  ON_CALL(*mock_frame, AudioLevel()).WillByDefault(Return(audio_level));
  uint8_t payload[10];
  ON_CALL(*mock_frame, GetData())
      .WillByDefault(Return(ArrayView<uint8_t>(&payload[0], 10)));
  EXPECT_THAT(WaitUntil([&] { return callback; }, IsTrue()), IsRtcOk());
  callback->OnTransformedFrame(std::move(mock_frame));

  // Allow things posted back to the encoder queue to run.
  time_controller_.AdvanceTime(TimeDelta::Millis(10));

  // Ensure the audio levels is set on the sent packet.
  EXPECT_THAT(WaitUntil([&] { return sent_audio_level; }, IsTrue()), IsRtcOk());
  EXPECT_EQ(*sent_audio_level, audio_level);
}

// Ensure that GetUsedRate returns null if no frames are coded.
TEST_F(ChannelSendTest, NoUsedRateInitially) {
  channel_->StartSend();
  auto used_rate = channel_->GetUsedRate();
  EXPECT_EQ(used_rate, std::nullopt);
}

// Ensure that GetUsedRate returns value with one coded frame.
TEST_F(ChannelSendTest, ValidUsedRateWithOneCodedFrame) {
  channel_->StartSend();
  EXPECT_CALL(transport_, SendRtp).Times(1);
  ProcessNextFrame();
  ProcessNextFrame();
  auto used_rate = channel_->GetUsedRate();
  EXPECT_GT(used_rate.value().bps(), 0);
}

// Ensure that GetUsedRate returns value with one coded frame.
TEST_F(ChannelSendTest, UsedRateIsLargerofLastTwoFrames) {
  channel_->StartSend();
  channel_->CallEncoder(
      [&](AudioEncoder* encoder) { encoder->OnReceivedOverhead(72); });
  DataRate lowrate = DataRate::BitsPerSec(40000);
  DataRate highrate = DataRate::BitsPerSec(80000);
  BitrateAllocationUpdate update;
  update.bwe_period = TimeDelta::Millis(100);

  update.target_bitrate = lowrate;
  channel_->OnBitrateAllocation(update);
  EXPECT_CALL(transport_, SendRtp).Times(1);
  ProcessNextFrame();
  ProcessNextFrame();
  // Last two frames have rates [32kbps, -], yielding 32kbps.
  auto used_rate_1 = channel_->GetUsedRate();

  update.target_bitrate = highrate;
  channel_->OnBitrateAllocation(update);
  EXPECT_CALL(transport_, SendRtp).Times(1);
  ProcessNextFrame();
  ProcessNextFrame();
  // Last two frames have rates [54kbps, 32kbps], yielding 54kbps
  auto used_rate_2 = channel_->GetUsedRate();

  update.target_bitrate = lowrate;
  channel_->OnBitrateAllocation(update);
  EXPECT_CALL(transport_, SendRtp).Times(1);
  ProcessNextFrame();
  ProcessNextFrame();
  // Last two frames have rates [32kbps 54kbps], yielding 54kbps
  auto used_rate_3 = channel_->GetUsedRate();

  EXPECT_GT(used_rate_2, used_rate_1);
  EXPECT_EQ(used_rate_3, used_rate_2);
}

// Test that we gracefully handle packets while the congestion control objects
// are not configured. This can happen during calls
// AudioSendStream::ConfigureStream
TEST_F(ChannelSendTest, EnqueuePacketsGracefullyHandlesNonInitializedPacer) {
  EXPECT_CALL(transport_, SendRtp).Times(1);
  channel_->StartSend();
  channel_->ResetSenderCongestionControlObjects();
  // This should trigger a packet, but congestion control is not configured
  // so it should be dropped
  ProcessNextFrame();
  ProcessNextFrame();

  channel_->RegisterSenderCongestionControlObjects(&transport_controller_);
  // Now that we reconfigured the congestion control objects the new frame
  // should be processed
  ProcessNextFrame();
  ProcessNextFrame();
}

}  // namespace
}  // namespace voe
}  // namespace webrtc
