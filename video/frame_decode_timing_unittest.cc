/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/frame_decode_timing.h"

#include <cstdint>
#include <optional>

#include "api/field_trials.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/video_coding/timing/timing.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "video/video_receive_stream2.h"

namespace webrtc {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Optional;

namespace {

constexpr uint32_t kNextRtp = 90000;
constexpr uint32_t kLastRtp = 180000;

class FrameDecodeTimingTest : public ::testing::Test {
 public:
  FrameDecodeTimingTest()
      : field_trials_(CreateTestFieldTrials()),
        clock_(Timestamp::Millis(1000)),
        timing_(&clock_, field_trials_),
        frame_decode_scheduler_(&clock_, &timing_) {
    timing_.set_render_delay(TimeDelta::Zero());
    timing_.OnCompleteTemporalUnit(kNextRtp, clock_.CurrentTime());
  }

 protected:
  FieldTrials field_trials_;
  SimulatedClock clock_;
  VCMTiming timing_;
  FrameDecodeTiming frame_decode_scheduler_;
};

TEST_F(FrameDecodeTimingTest, ReturnsWaitTimesWhenValid) {
  const TimeDelta kDecodeDelay = TimeDelta::Millis(42);
  timing_.SetMinimumDelay(kDecodeDelay);

  EXPECT_THAT(frame_decode_scheduler_.OnFrameBufferUpdated(
                  kNextRtp, kLastRtp, kMaxWaitForFrame, false),
              Optional(AllOf(
                  Field(&FrameDecodeTiming::FrameSchedule::latest_decode_time,
                        Eq(clock_.CurrentTime() + kDecodeDelay)),
                  Field(&FrameDecodeTiming::FrameSchedule::render_time,
                        Eq(clock_.CurrentTime() + kDecodeDelay)))));
}

TEST_F(FrameDecodeTimingTest, FastForwardsFrameTooFarInThePast) {
  const TimeDelta kDecodeDelay =
      -FrameDecodeTiming::kMaxAllowedFrameDelay - TimeDelta::Millis(1);
  timing_.SetMinimumDelay(kDecodeDelay);
  timing_.set_min_playout_delay(kDecodeDelay);

  EXPECT_THAT(frame_decode_scheduler_.OnFrameBufferUpdated(
                  kNextRtp, kLastRtp, kMaxWaitForFrame, false),
              Eq(std::nullopt));
}

TEST_F(FrameDecodeTimingTest, NoFastForwardIfOnlyFrameToDecode) {
  const TimeDelta kDecodeDelay =
      -FrameDecodeTiming::kMaxAllowedFrameDelay - TimeDelta::Millis(1);
  timing_.SetMinimumDelay(kDecodeDelay);
  timing_.set_min_playout_delay(kDecodeDelay);

  // Negative `kDecodeDelay` means that `latest_decode_time` is now.
  EXPECT_THAT(frame_decode_scheduler_.OnFrameBufferUpdated(
                  kNextRtp, kNextRtp, kMaxWaitForFrame, false),
              Optional(AllOf(
                  Field(&FrameDecodeTiming::FrameSchedule::latest_decode_time,
                        Eq(clock_.CurrentTime())),
                  Field(&FrameDecodeTiming::FrameSchedule::render_time,
                        Eq(clock_.CurrentTime() + kDecodeDelay)))));
}

TEST_F(FrameDecodeTimingTest, MaxWaitCapped) {
  const TimeDelta kDecodeDelay = kMaxWaitForFrame * 2;
  timing_.SetMinimumDelay(kDecodeDelay);

  EXPECT_THAT(frame_decode_scheduler_.OnFrameBufferUpdated(
                  kNextRtp, kLastRtp, kMaxWaitForFrame, false),
              Optional(AllOf(
                  Field(&FrameDecodeTiming::FrameSchedule::latest_decode_time,
                        Eq(clock_.CurrentTime() + kMaxWaitForFrame)),
                  Field(&FrameDecodeTiming::FrameSchedule::render_time,
                        Eq(clock_.CurrentTime() + kDecodeDelay)))));
}

TEST_F(FrameDecodeTimingTest, MaxWaitCappedForKey) {
  const TimeDelta kDecodeDelay = kMaxWaitForKeyFrame * 2;
  timing_.SetMinimumDelay(kDecodeDelay);

  EXPECT_THAT(frame_decode_scheduler_.OnFrameBufferUpdated(
                  kNextRtp, kLastRtp, kMaxWaitForKeyFrame, false),
              Optional(AllOf(
                  Field(&FrameDecodeTiming::FrameSchedule::latest_decode_time,
                        Eq(clock_.CurrentTime() + kMaxWaitForKeyFrame)),
                  Field(&FrameDecodeTiming::FrameSchedule::render_time,
                        Eq(clock_.CurrentTime() + kDecodeDelay)))));
}

}  // namespace
}  // namespace webrtc
