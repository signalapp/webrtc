/*
 *  Copyright 2026 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/congestion_controller/scream/loss_estimator.h"

#include <vector>

#include "api/environment/environment.h"
#include "api/transport/network_types.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/scream/scream_v2_parameters.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

TransportPacketsFeedback CreateFeedbackWithLoss(Timestamp feedback_time,
                                                bool with_loss) {
  TransportPacketsFeedback feedback;
  feedback.feedback_time = feedback_time;

  PacketResult received_packet;
  received_packet.sent_packet.size = DataSize::Bytes(1000);
  received_packet.receive_time = feedback_time;
  feedback.packet_feedbacks.push_back(received_packet);

  if (with_loss) {
    PacketResult lost_packet;
    lost_packet.sent_packet.size = DataSize::Bytes(1000);
    lost_packet.receive_time = Timestamp::PlusInfinity();
    lost_packet.reported_lost_for_the_first_time = true;
    feedback.packet_feedbacks.push_back(lost_packet);
  }
  return feedback;
}

TEST(LossEstimatorTest, EstimatesCongestionLevel) {
  SimulatedClock clock(Timestamp::Seconds(1000));
  Environment env = CreateTestEnvironment({.time = &clock});
  ScreamV2Parameters params(env.field_trials());
  LossEstimator estimator(params);

  EXPECT_EQ(estimator.congestion_level(), 0.0);
  EXPECT_FALSE(estimator.congested());

  // RTT 1: Loss occurs. Since last_rtt_update_time_ starts at MinusInfinity,
  // this report executes the RTT boundary update immediately.
  TransportPacketsFeedback feedback1 =
      CreateFeedbackWithLoss(clock.CurrentTime(), /*with_loss=*/true);
  EXPECT_TRUE(estimator.Update(feedback1, TimeDelta::Millis(25)));
  EXPECT_NEAR(estimator.congestion_level(), 1.0 / 3.0, 0.001);
  EXPECT_FALSE(estimator.congested());

  // RTT 2: Loss occurs.
  clock.AdvanceTime(TimeDelta::Millis(25));
  TransportPacketsFeedback feedback2 =
      CreateFeedbackWithLoss(clock.CurrentTime(), /*with_loss=*/true);
  EXPECT_TRUE(estimator.Update(feedback2, TimeDelta::Millis(25)));
  EXPECT_NEAR(estimator.congestion_level(), 2.0 / 3.0, 0.001);
  EXPECT_FALSE(estimator.congested());

  // RTT 3: Loss occurs.
  clock.AdvanceTime(TimeDelta::Millis(25));
  TransportPacketsFeedback feedback3 =
      CreateFeedbackWithLoss(clock.CurrentTime(), /*with_loss=*/true);
  EXPECT_TRUE(estimator.Update(feedback3, TimeDelta::Millis(25)));
  EXPECT_NEAR(estimator.congestion_level(), 1.0, 0.001);
  EXPECT_TRUE(estimator.congested());

  // RTT 4: No Loss (Step Down of 0.5).
  clock.AdvanceTime(TimeDelta::Millis(25));
  TransportPacketsFeedback feedback4 =
      CreateFeedbackWithLoss(clock.CurrentTime(), /*with_loss=*/false);
  EXPECT_FALSE(estimator.Update(feedback4, TimeDelta::Millis(25)));
  EXPECT_NEAR(estimator.congestion_level(), 0.5, 0.001);
  EXPECT_FALSE(estimator.congested());

  // RTT 5: Loss occurs.
  clock.AdvanceTime(TimeDelta::Millis(25));
  TransportPacketsFeedback feedback5 =
      CreateFeedbackWithLoss(clock.CurrentTime(), /*with_loss=*/true);
  EXPECT_TRUE(estimator.Update(feedback5, TimeDelta::Millis(25)));
  EXPECT_NEAR(estimator.congestion_level(), 0.5 + 1.0 / 3.0, 0.001);
  EXPECT_FALSE(estimator.congested());
}

TEST(LossEstimatorTest, ClearsCongestionLevelUponFullRecovery) {
  SimulatedClock clock(Timestamp::Seconds(1000));
  Environment env = CreateTestEnvironment({.time = &clock});
  ScreamV2Parameters params(env.field_trials());
  LossEstimator estimator(params);

  // Report loss.
  TransportPacketsFeedback feedback1 =
      CreateFeedbackWithLoss(clock.CurrentTime(), /*with_loss=*/true);
  EXPECT_TRUE(estimator.Update(feedback1, TimeDelta::Millis(25)));

  EXPECT_GT(estimator.congestion_level(), 0.0);

  // Report recovery.
  TransportPacketsFeedback recovery_feedback;
  recovery_feedback.feedback_time = clock.CurrentTime();
  PacketResult recovery_res;
  recovery_res.sent_packet.size = DataSize::Bytes(1000);
  recovery_res.receive_time = clock.CurrentTime();
  recovery_res.reported_recovered_for_the_first_time = true;
  recovery_feedback.packet_feedbacks.push_back(recovery_res);

  EXPECT_FALSE(estimator.Update(recovery_feedback, TimeDelta::Millis(25)));

  EXPECT_EQ(estimator.congestion_level(), 0.0);
}

}  // namespace
}  // namespace webrtc
