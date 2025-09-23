/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef RTC_BASE_SYNCHRONIZATION_SEQUENCE_CHECKER_INTERNAL_H_
#define RTC_BASE_SYNCHRONIZATION_SEQUENCE_CHECKER_INTERNAL_H_

#include <string>
#include <type_traits>

#include "api/task_queue/task_queue_base.h"
#include "rtc_base/checks.h"
#include "rtc_base/platform_thread_types.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/system/rtc_export.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {
namespace webrtc_sequence_checker_internal {

// Real implementation of SequenceChecker, for use in debug mode, or
// for temporary use in release mode (e.g. to RTC_CHECK on a threading issue
// seen only in the wild).
//
// Note: You should almost always use the SequenceChecker class to get the
// right version for your build configuration.
class RTC_EXPORT SequenceCheckerImpl {
 public:
  explicit SequenceCheckerImpl(bool attach_to_current_thread);
  explicit SequenceCheckerImpl(TaskQueueBase* attached_queue);
  ~SequenceCheckerImpl() = default;

  bool IsCurrent() const;
  // Changes the task queue or thread that is checked for in IsCurrent. This can
  // be useful when an object may be created on one task queue / thread and then
  // used exclusively on another thread.
  void Detach();

  // Makes the task queue or thread that is checked for in `this`.IsCurrent()
  // be the same as in `o`.IsCurrent().
  void AssignStateFrom(const SequenceCheckerImpl& o);

  // Returns a string that is formatted to match with the error string printed
  // by RTC_CHECK() when a condition is not met.
  // This is used in conjunction with the RTC_DCHECK_RUN_ON() macro.
  std::string ExpectationToString() const;

  // Returns whether or not the checker is attached.
  // Exists only in the SequenceChecker that is used when RTC_DCHECK_IS_ON
  // is set, so tests using it must check that flag.
  bool IsAttachedForTesting() const;

  // Returns true if the two sequence checkers are either both detached
  // or attached to the same thread.
  bool HasSameAttachmentForTesting(const SequenceCheckerImpl& o) const;

 private:
  mutable Mutex lock_;
  // These are mutable so that IsCurrent can set them.
  mutable bool attached_ RTC_GUARDED_BY(lock_);
  mutable PlatformThreadRef valid_thread_ RTC_GUARDED_BY(lock_);
  mutable const TaskQueueBase* valid_queue_ RTC_GUARDED_BY(lock_);
};

// Do nothing implementation, for use in release mode.
//
// Note: You should almost always use the SequenceChecker class to get the
// right version for your build configuration.
class SequenceCheckerDoNothing {
 public:
  explicit SequenceCheckerDoNothing(bool /* attach_to_current_thread */) {}
  explicit SequenceCheckerDoNothing(TaskQueueBase* /* attached_queue */) {}
  bool IsCurrent() const { return true; }
  void Detach() {}
};

template <typename ThreadLikeObject>
std::enable_if_t<std::is_base_of_v<SequenceCheckerImpl, ThreadLikeObject>,
                 std::string>
ExpectationToString([[maybe_unused]] const ThreadLikeObject* checker) {
#if RTC_DCHECK_IS_ON
  return checker->ExpectationToString();
#else
  return std::string();
#endif
}

// Catch-all implementation for types other than explicitly supported above.
template <typename ThreadLikeObject>
std::enable_if_t<!std::is_base_of_v<SequenceCheckerImpl, ThreadLikeObject>,
                 std::string>
ExpectationToString(const ThreadLikeObject*) {
  return std::string();
}

class AutoDetachingSequenceCheckerImpl : public SequenceCheckerImpl {
 public:
  enum InitialState : bool { kDetached = false, kAttached = true };

  AutoDetachingSequenceCheckerImpl() : SequenceCheckerImpl(kDetached) {}

  AutoDetachingSequenceCheckerImpl(const AutoDetachingSequenceCheckerImpl& o)
      : SequenceCheckerImpl(kDetached) {
    AssignStateFrom(o);
  }

  AutoDetachingSequenceCheckerImpl& operator=(
      const AutoDetachingSequenceCheckerImpl& o) {
    AssignStateFrom(o);
    return *this;
  }

  AutoDetachingSequenceCheckerImpl(AutoDetachingSequenceCheckerImpl&& o)
      : SequenceCheckerImpl(kDetached) {
    o.Detach();
  }

  AutoDetachingSequenceCheckerImpl& operator=(
      AutoDetachingSequenceCheckerImpl&& o) {
    Detach();
    o.Detach();
    return *this;
  }
};

class AutoDetachingSequenceCheckerDoNothing {
 public:
  AutoDetachingSequenceCheckerDoNothing() {}
  AutoDetachingSequenceCheckerDoNothing(
      const AutoDetachingSequenceCheckerDoNothing& o) {}
  AutoDetachingSequenceCheckerDoNothing& operator=(
      const AutoDetachingSequenceCheckerDoNothing& o) {
    return *this;
  }
  AutoDetachingSequenceCheckerDoNothing(
      AutoDetachingSequenceCheckerDoNothing&& o) {}
  AutoDetachingSequenceCheckerDoNothing& operator=(
      AutoDetachingSequenceCheckerDoNothing&& o) {
    return *this;
  }
  bool IsCurrent() const { return true; }
  void Detach() {}
};

}  // namespace webrtc_sequence_checker_internal
}  // namespace webrtc

#endif  // RTC_BASE_SYNCHRONIZATION_SEQUENCE_CHECKER_INTERNAL_H_
