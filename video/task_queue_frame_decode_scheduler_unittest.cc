/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/task_queue_frame_decode_scheduler.h"

#include <stddef.h>

#include <memory>
#include <utility>

<<<<<<< HEAD
#include "absl/functional/bind_front.h"
#include "absl/types/optional.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/task_queue.h"
=======
#include "absl/types/optional.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
>>>>>>> m108
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {

<<<<<<< HEAD
using ::testing::Eq;
using ::testing::Optional;

class TaskQueueFrameDecodeSchedulerTest : public ::testing::Test {
 public:
  TaskQueueFrameDecodeSchedulerTest()
      : time_controller_(Timestamp::Millis(2000)),
        task_queue_(time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
            "scheduler",
            TaskQueueFactory::Priority::NORMAL)),
        scheduler_(std::make_unique<TaskQueueFrameDecodeScheduler>(
            time_controller_.GetClock(),
            task_queue_.Get())) {}

  ~TaskQueueFrameDecodeSchedulerTest() override {
    if (scheduler_) {
      OnQueue([&] {
        scheduler_->Stop();
        scheduler_ = nullptr;
      });
    }
  }

  void FrameReadyForDecode(uint32_t timestamp, Timestamp render_time) {
    last_rtp_ = timestamp;
    last_render_time_ = render_time;
  }

 protected:
  template <class Task>
  void OnQueue(Task&& t) {
    task_queue_.PostTask(std::forward<Task>(t));
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  GlobalSimulatedTimeController time_controller_;
  rtc::TaskQueue task_queue_;
  std::unique_ptr<FrameDecodeScheduler> scheduler_;
  absl::optional<uint32_t> last_rtp_;
  absl::optional<Timestamp> last_render_time_;
};

TEST_F(TaskQueueFrameDecodeSchedulerTest, FrameYieldedAfterSpecifiedPeriod) {
  constexpr TimeDelta decode_delay = TimeDelta::Millis(5);
=======
using ::testing::_;
using ::testing::Eq;
using ::testing::MockFunction;
using ::testing::Optional;

TEST(TaskQueueFrameDecodeSchedulerTest, FrameYieldedAfterSpecifiedPeriod) {
  GlobalSimulatedTimeController time_controller_(Timestamp::Seconds(2000));
  TaskQueueFrameDecodeScheduler scheduler(time_controller_.GetClock(),
                                          time_controller_.GetMainThread());
  constexpr TimeDelta decode_delay = TimeDelta::Millis(5);

>>>>>>> m108
  const Timestamp now = time_controller_.GetClock()->CurrentTime();
  const uint32_t rtp = 90000;
  const Timestamp render_time = now + TimeDelta::Millis(15);
  FrameDecodeTiming::FrameSchedule schedule = {
      .latest_decode_time = now + decode_delay, .render_time = render_time};
<<<<<<< HEAD
  OnQueue([&] {
    scheduler_->ScheduleFrame(
        rtp, schedule,
        absl::bind_front(
            &TaskQueueFrameDecodeSchedulerTest::FrameReadyForDecode, this));
    EXPECT_THAT(scheduler_->ScheduledRtpTimestamp(), Optional(rtp));
  });
  EXPECT_THAT(last_rtp_, Eq(absl::nullopt));

  time_controller_.AdvanceTime(decode_delay);
  EXPECT_THAT(last_rtp_, Optional(rtp));
  EXPECT_THAT(last_render_time_, Optional(render_time));
}

TEST_F(TaskQueueFrameDecodeSchedulerTest, NegativeDecodeDelayIsRoundedToZero) {
=======

  MockFunction<void(uint32_t, Timestamp)> ready_cb;
  scheduler.ScheduleFrame(rtp, schedule, ready_cb.AsStdFunction());
  EXPECT_CALL(ready_cb, Call(_, _)).Times(0);
  EXPECT_THAT(scheduler.ScheduledRtpTimestamp(), Optional(rtp));
  time_controller_.AdvanceTime(TimeDelta::Zero());
  // Check that `ready_cb` has not been invoked yet.
  ::testing::Mock::VerifyAndClearExpectations(&ready_cb);

  EXPECT_CALL(ready_cb, Call(rtp, render_time)).Times(1);
  time_controller_.AdvanceTime(decode_delay);

  scheduler.Stop();
}

TEST(TaskQueueFrameDecodeSchedulerTest, NegativeDecodeDelayIsRoundedToZero) {
  GlobalSimulatedTimeController time_controller_(Timestamp::Seconds(2000));
  TaskQueueFrameDecodeScheduler scheduler(time_controller_.GetClock(),
                                          time_controller_.GetMainThread());
>>>>>>> m108
  constexpr TimeDelta decode_delay = TimeDelta::Millis(-5);
  const Timestamp now = time_controller_.GetClock()->CurrentTime();
  const uint32_t rtp = 90000;
  const Timestamp render_time = now + TimeDelta::Millis(15);
  FrameDecodeTiming::FrameSchedule schedule = {
      .latest_decode_time = now + decode_delay, .render_time = render_time};
<<<<<<< HEAD
  OnQueue([&] {
    scheduler_->ScheduleFrame(
        rtp, schedule,
        absl::bind_front(
            &TaskQueueFrameDecodeSchedulerTest::FrameReadyForDecode, this));
    EXPECT_THAT(scheduler_->ScheduledRtpTimestamp(), Optional(rtp));
  });
  EXPECT_THAT(last_rtp_, Optional(rtp));
  EXPECT_THAT(last_render_time_, Optional(render_time));
}

TEST_F(TaskQueueFrameDecodeSchedulerTest, CancelOutstanding) {
=======

  MockFunction<void(uint32_t, Timestamp)> ready_cb;
  EXPECT_CALL(ready_cb, Call(rtp, render_time)).Times(1);
  scheduler.ScheduleFrame(rtp, schedule, ready_cb.AsStdFunction());
  EXPECT_THAT(scheduler.ScheduledRtpTimestamp(), Optional(rtp));
  time_controller_.AdvanceTime(TimeDelta::Zero());

  scheduler.Stop();
}

TEST(TaskQueueFrameDecodeSchedulerTest, CancelOutstanding) {
  GlobalSimulatedTimeController time_controller_(Timestamp::Seconds(2000));
  TaskQueueFrameDecodeScheduler scheduler(time_controller_.GetClock(),
                                          time_controller_.GetMainThread());
>>>>>>> m108
  constexpr TimeDelta decode_delay = TimeDelta::Millis(50);
  const Timestamp now = time_controller_.GetClock()->CurrentTime();
  const uint32_t rtp = 90000;
  FrameDecodeTiming::FrameSchedule schedule = {
      .latest_decode_time = now + decode_delay,
      .render_time = now + TimeDelta::Millis(75)};
<<<<<<< HEAD
  OnQueue([&] {
    scheduler_->ScheduleFrame(
        rtp, schedule,
        absl::bind_front(
            &TaskQueueFrameDecodeSchedulerTest::FrameReadyForDecode, this));
    EXPECT_THAT(scheduler_->ScheduledRtpTimestamp(), Optional(rtp));
  });
  time_controller_.AdvanceTime(decode_delay / 2);
  OnQueue([&] {
    EXPECT_THAT(scheduler_->ScheduledRtpTimestamp(), Optional(rtp));
    scheduler_->CancelOutstanding();
    EXPECT_THAT(scheduler_->ScheduledRtpTimestamp(), Eq(absl::nullopt));
  });
  time_controller_.AdvanceTime(decode_delay / 2);
  EXPECT_THAT(last_rtp_, Eq(absl::nullopt));
  EXPECT_THAT(last_render_time_, Eq(absl::nullopt));
=======

  MockFunction<void(uint32_t, Timestamp)> ready_cb;
  EXPECT_CALL(ready_cb, Call).Times(0);
  scheduler.ScheduleFrame(rtp, schedule, ready_cb.AsStdFunction());
  EXPECT_THAT(scheduler.ScheduledRtpTimestamp(), Optional(rtp));
  time_controller_.AdvanceTime(decode_delay / 2);
  EXPECT_THAT(scheduler.ScheduledRtpTimestamp(), Optional(rtp));
  scheduler.CancelOutstanding();
  EXPECT_THAT(scheduler.ScheduledRtpTimestamp(), Eq(absl::nullopt));
  time_controller_.AdvanceTime(decode_delay / 2);

  scheduler.Stop();
>>>>>>> m108
}

}  // namespace webrtc
