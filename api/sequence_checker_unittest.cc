/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/sequence_checker.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "api/function_view.h"
#include "api/units/time_delta.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/platform_thread.h"
#include "rtc_base/synchronization/sequence_checker_internal.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread_annotations.h"
#include "test/gmock.h"
#include "test/gtest.h"

using testing::Eq;
using testing::HasSubstr;

namespace webrtc {
namespace {

// This class is dead code, but its purpose is to make sure that
// SequenceChecker is compatible with the RTC_GUARDED_BY and RTC_RUN_ON
// attributes that are checked at compile-time.
class CompileTimeTestForGuardedBy {
 public:
  int CalledOnSequence() RTC_RUN_ON(sequence_checker_) { return guarded_; }

  void CallMeFromSequence() {
    RTC_DCHECK_RUN_ON(&sequence_checker_);
    guarded_ = 41;
  }

 private:
  int guarded_ RTC_GUARDED_BY(sequence_checker_);
  SequenceChecker sequence_checker_;
};

void RunOnDifferentThread(FunctionView<void()> run) {
  Event thread_has_run_event;
  PlatformThread::SpawnJoinable(
      [&] {
        run();
        thread_has_run_event.Set();
      },
      "thread");
  EXPECT_TRUE(thread_has_run_event.Wait(TimeDelta::Seconds(1)));
}

}  // namespace

TEST(SequenceCheckerTest, CallsAllowedOnSameThread) {
  SequenceChecker sequence_checker;
  EXPECT_TRUE(sequence_checker.IsCurrent());
}

TEST(SequenceCheckerTest, DestructorAllowedOnDifferentThread) {
  auto sequence_checker = std::make_unique<SequenceChecker>();
  RunOnDifferentThread([&] {
    // Verify that the destructor doesn't assert when called on a different
    // thread.
    sequence_checker.reset();
  });
}

TEST(SequenceCheckerTest, Detach) {
  SequenceChecker sequence_checker;
  sequence_checker.Detach();
  RunOnDifferentThread([&] { EXPECT_TRUE(sequence_checker.IsCurrent()); });
}

TEST(SequenceCheckerTest, DetachFromThreadAndUseOnTaskQueue) {
  SequenceChecker sequence_checker;
  sequence_checker.Detach();
  TaskQueueForTest queue;
  queue.SendTask([&] { EXPECT_TRUE(sequence_checker.IsCurrent()); });
}

TEST(SequenceCheckerTest, InitializeForDifferentTaskQueue) {
  TaskQueueForTest queue;
  SequenceChecker sequence_checker(queue.Get());
  EXPECT_EQ(sequence_checker.IsCurrent(), !RTC_DCHECK_IS_ON);
  queue.SendTask([&] { EXPECT_TRUE(sequence_checker.IsCurrent()); });
}

TEST(SequenceCheckerTest, DetachFromTaskQueueAndUseOnThread) {
  TaskQueueForTest queue;
  queue.SendTask([] {
    SequenceChecker sequence_checker;
    sequence_checker.Detach();
    RunOnDifferentThread([&] { EXPECT_TRUE(sequence_checker.IsCurrent()); });
  });
}

TEST(SequenceCheckerTest, MethodNotAllowedOnDifferentThreadInDebug) {
  SequenceChecker sequence_checker;
  RunOnDifferentThread(
      [&] { EXPECT_EQ(sequence_checker.IsCurrent(), !RTC_DCHECK_IS_ON); });
}

#if RTC_DCHECK_IS_ON
TEST(SequenceCheckerTest, OnlyCurrentOnOneThread) {
  SequenceChecker sequence_checker(SequenceChecker::kDetached);
  RunOnDifferentThread([&] {
    EXPECT_TRUE(sequence_checker.IsCurrent());
    // Spawn a new thread from within the first one to guarantee that we have
    // two concurrently active threads (and that there's no chance of the
    // thread ref being reused).
    RunOnDifferentThread([&] { EXPECT_FALSE(sequence_checker.IsCurrent()); });
  });
}
#endif

TEST(SequenceCheckerTest, MethodNotAllowedOnDifferentTaskQueueInDebug) {
  SequenceChecker sequence_checker;
  TaskQueueForTest queue;
  queue.SendTask(
      [&] { EXPECT_EQ(sequence_checker.IsCurrent(), !RTC_DCHECK_IS_ON); });
}

TEST(SequenceCheckerTest, DetachFromTaskQueueInDebug) {
  SequenceChecker sequence_checker;
  sequence_checker.Detach();

  TaskQueueForTest queue1;
  queue1.SendTask([&] { EXPECT_TRUE(sequence_checker.IsCurrent()); });

  // IsCurrent should return false in debug builds after moving to
  // another task queue.
  TaskQueueForTest queue2;
  queue2.SendTask(
      [&] { EXPECT_EQ(sequence_checker.IsCurrent(), !RTC_DCHECK_IS_ON); });
}

TEST(SequenceCheckerTest, ExpectationToString) {
  TaskQueueForTest queue1;

  SequenceChecker sequence_checker(SequenceChecker::kDetached);

  Event blocker;
  queue1.PostTask([&blocker, &sequence_checker]() {
    (void)sequence_checker.IsCurrent();
    blocker.Set();
  });

  blocker.Wait(Event::kForever);

#if RTC_DCHECK_IS_ON

  EXPECT_THAT(ExpectationToString(&sequence_checker),
              HasSubstr("# Expected: TQ:"));

  // Test for the base class
  webrtc_sequence_checker_internal::SequenceCheckerImpl* sequence_checker_base =
      &sequence_checker;
  EXPECT_THAT(ExpectationToString(sequence_checker_base),
              HasSubstr("# Expected: TQ:"));

#else
  GTEST_ASSERT_EQ(ExpectationToString(&sequence_checker), "");
#endif
}

TEST(SequenceCheckerTest, InitiallyDetached) {
  TaskQueueForTest queue1;

  SequenceChecker sequence_checker(SequenceChecker::kDetached);

  Event blocker;
  queue1.PostTask([&blocker, &sequence_checker]() {
    EXPECT_TRUE(sequence_checker.IsCurrent());
    blocker.Set();
  });

  blocker.Wait(Event::kForever);

#if RTC_DCHECK_IS_ON
  EXPECT_FALSE(sequence_checker.IsCurrent());
#endif
}

class TestAnnotations {
 public:
  TestAnnotations() : test_var_(false) {}

  void ModifyTestVar() {
    RTC_DCHECK_RUN_ON(&checker_);
    test_var_ = true;
  }

 private:
  bool test_var_ RTC_GUARDED_BY(&checker_);
  SequenceChecker checker_;
};

TEST(SequenceCheckerTest, TestAnnotations) {
  TestAnnotations annotations;
  annotations.ModifyTestVar();
}

#if GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)

void TestAnnotationsOnWrongQueue() {
  TestAnnotations annotations;
  TaskQueueForTest queue;
  queue.SendTask([&] { annotations.ModifyTestVar(); });
}

#if RTC_DCHECK_IS_ON
// Note: Ending the test suite name with 'DeathTest' is important as it causes
// gtest to order this test before any other non-death-tests, to avoid potential
// global process state pollution such as shared worker threads being started
// (e.g. a side effect of calling InitCocoaMultiThreading() on Mac causes one or
// two additional threads to be created).
TEST(SequenceCheckerDeathTest, TestAnnotationsOnWrongQueueDebug) {
  ASSERT_DEATH({ TestAnnotationsOnWrongQueue(); }, "");
}
#else
TEST(SequenceCheckerTest, TestAnnotationsOnWrongQueueRelease) {
  TestAnnotationsOnWrongQueue();
}
#endif
#endif  // GTEST_HAS_DEATH_TEST

// This class is a helper for verifying that AutoDetachingSequenceChecker
// can be made a member of a class with default copy and move operations.
class MovableObject {
 public:
  MovableObject() = default;
  // Copy operators
  MovableObject(const MovableObject& o) = default;
  MovableObject& operator=(const MovableObject& o) = default;
  // Move operators
  MovableObject(MovableObject&& o) = default;
  MovableObject& operator=(MovableObject&& o) = default;

  int any_member RTC_GUARDED_BY(sequence_checker_) = 4711;
  AutoDetachingSequenceChecker sequence_checker_;
};

TEST(AutoDetachingSequenceCheckerTest, CanDeclareMovableObject) {
  MovableObject foo;
  RTC_DCHECK_RUN_ON(&foo.sequence_checker_);
  EXPECT_THAT(foo.any_member, Eq(4711));
}

TEST(AutoDetachingSequenceCheckerTest, CanCopyMovableObject) {
  MovableObject foo;
  RTC_DCHECK_RUN_ON(&foo.sequence_checker_);
  foo.any_member = 12;
  MovableObject bar = foo;
  RTC_DCHECK_RUN_ON(&bar.sequence_checker_);
  EXPECT_THAT(bar.any_member, Eq(12));
}

#if RTC_DCHECK_IS_ON
// The tests below use the helper functions IsAttachedForTesting and
// HasSameAttachmentForTesting, which are only present on the
// AutoDetachingSequenceChecker when compiled with DCHECK on.
TEST(AutoDetachingSequenceCheckerTest, InitialStateIsDetached) {
  AutoDetachingSequenceChecker foo;
  EXPECT_FALSE(foo.IsAttachedForTesting());
}

TEST(AutoDetachingSequenceCheckerTest, CopyConstructorKeepsAttachment) {
  AutoDetachingSequenceChecker foo;
  EXPECT_FALSE(foo.IsAttachedForTesting());
  AutoDetachingSequenceChecker bar(foo);
  EXPECT_FALSE(bar.IsAttachedForTesting());
  RTC_DCHECK_RUN_ON(&foo);
  EXPECT_TRUE(foo.IsAttachedForTesting());
  AutoDetachingSequenceChecker baz(foo);
  EXPECT_TRUE(baz.IsAttachedForTesting());
  EXPECT_TRUE(baz.HasSameAttachmentForTesting(foo));
}

TEST(AutoDetachingSequenceCheckerTest, MoveDetachesFromCurrentThread) {
  TaskQueueForTest queue;
  AutoDetachingSequenceChecker foo;
  EXPECT_FALSE(foo.IsAttachedForTesting());
  RTC_DCHECK_RUN_ON(&foo);
  EXPECT_TRUE(foo.IsAttachedForTesting());
  AutoDetachingSequenceChecker bar = std::move(foo);
  EXPECT_FALSE(bar.IsAttachedForTesting());
  EXPECT_FALSE(foo.IsAttachedForTesting());
}

TEST(AutoDetachingSequenceCheckerTest, MoveDetachesFromCurrentThreadInCapture) {
  TaskQueueForTest queue;
  AutoDetachingSequenceChecker foo;
  RTC_DCHECK_RUN_ON(&foo);
  queue.SendTask([bar = std::move(foo)]() {
    EXPECT_FALSE(bar.IsAttachedForTesting());
    RTC_DCHECK_RUN_ON(&bar);
    EXPECT_TRUE(bar.IsAttachedForTesting());
  });
  EXPECT_FALSE(foo.IsAttachedForTesting());
}

TEST(AutoDetachingSequenceCheckerTest, CopyOperatorKeepsOldThread) {
  TaskQueueForTest queue;
  AutoDetachingSequenceChecker object1;
  AutoDetachingSequenceChecker object2;
  // Attach object1 to current thread.
  RTC_DCHECK_RUN_ON(&object1);
  queue.SendTask([&]() {
    // Attach object2 to this task queue
    RTC_DCHECK_RUN_ON(&object2);
    // Overwriting will attach object2 to object1's bound thread.
    object2 = object1;
  });
  EXPECT_TRUE(object1.HasSameAttachmentForTesting(object2));
}
#endif  // RTC_DCHECK_IS_ON

#if GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID) && RTC_DCHECK_IS_ON
TEST(AutoDetachingSequenceCheckerDeathTest, NotMovingCrashes) {
  TaskQueueForTest queue;
  AutoDetachingSequenceChecker unmoved_object;
  // Attach `unmoved_object` to the queue, not the main thread
  queue.SendTask([&unmoved_object]() { RTC_DCHECK_RUN_ON(&unmoved_object); });
  ASSERT_DEATH({RTC_DCHECK_RUN_ON(&unmoved_object)}, "IsCurrent");
}

TEST(AutoDetachingSequenceCheckerDeathTest,
     CopyOperatorKeepsOldThreadAndCrashes) {
  TaskQueueForTest queue;
  AutoDetachingSequenceChecker object1;
  AutoDetachingSequenceChecker object2;
  // Attach object2 to current thread.
  RTC_DCHECK_RUN_ON(&object2);
  queue.SendTask([&]() {
    RTC_DCHECK_RUN_ON(&object1);
    object2 = object1;  // This assignment overwrites the attachment.
  });
  // object2 is now attached to the task queue.
  ASSERT_DEATH({RTC_DCHECK_RUN_ON(&object2)}, "IsCurrent");
}

#endif
}  // namespace webrtc
