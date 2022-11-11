/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/peer_connection_message_handler.h"

#include <list>
#include <utility>

#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/stats_types.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "pc/legacy_stats_collector_interface.h"
#include "rtc_base/checks.h"

namespace webrtc {
namespace {

template <typename T>
rtc::scoped_refptr<T> WrapScoped(T* ptr) {
  return rtc::scoped_refptr<T>(ptr);
}

<<<<<<< HEAD
void PeerConnectionMessageHandler::OnMessage(rtc::Message* msg) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  switch (msg->message_id) {
    case MSG_SET_SESSIONDESCRIPTION_SUCCESS: {
      SetSessionDescriptionMsg* param =
          static_cast<SetSessionDescriptionMsg*>(msg->pdata);
      param->observer->OnSuccess();
      delete param;
      break;
    }
    case MSG_SET_SESSIONDESCRIPTION_FAILED: {
      SetSessionDescriptionMsg* param =
          static_cast<SetSessionDescriptionMsg*>(msg->pdata);
      param->observer->OnFailure(std::move(param->error));
      delete param;
      break;
    }
    case MSG_CREATE_SESSIONDESCRIPTION_FAILED: {
      CreateSessionDescriptionMsg* param =
          static_cast<CreateSessionDescriptionMsg*>(msg->pdata);
      param->observer->OnFailure(std::move(param->error));
      delete param;
      break;
    }
    case MSG_GETSTATS: {
      GetStatsMsg* param = static_cast<GetStatsMsg*>(msg->pdata);
      StatsReports reports;
      param->stats->GetStats(param->track, &reports);
      param->observer->OnComplete(reports);
      delete param;
      break;
    }
    case MSG_REPORT_USAGE_PATTERN: {
      RequestUsagePatternMsg* param =
          static_cast<RequestUsagePatternMsg*>(msg->pdata);
      param->function();
      delete param;
      break;
    }
    default:
      RTC_DCHECK_NOTREACHED() << "Not implemented";
      break;
  }
}
=======
}  // namespace
>>>>>>> m108

void PeerConnectionMessageHandler::PostSetSessionDescriptionSuccess(
    SetSessionDescriptionObserver* observer) {
  signaling_thread_->PostTask(
      SafeTask(safety_.flag(),
               [observer = WrapScoped(observer)] { observer->OnSuccess(); }));
}

void PeerConnectionMessageHandler::PostSetSessionDescriptionFailure(
    SetSessionDescriptionObserver* observer,
    RTCError&& error) {
  RTC_DCHECK(!error.ok());
  signaling_thread_->PostTask(SafeTask(
      safety_.flag(),
      [observer = WrapScoped(observer), error = std::move(error)]() mutable {
        observer->OnFailure(std::move(error));
      }));
}

void PeerConnectionMessageHandler::PostCreateSessionDescriptionFailure(
    CreateSessionDescriptionObserver* observer,
    RTCError error) {
  RTC_DCHECK(!error.ok());
  // Do not protect this task with the safety_.flag() to ensure
  // observer is invoked even if the PeerConnection is destroyed early.
  signaling_thread_->PostTask(
      [observer = WrapScoped(observer), error = std::move(error)]() mutable {
        observer->OnFailure(std::move(error));
      });
}

void PeerConnectionMessageHandler::PostGetStats(
    StatsObserver* observer,
    LegacyStatsCollectorInterface* legacy_stats,
    MediaStreamTrackInterface* track) {
  signaling_thread_->PostTask(
      SafeTask(safety_.flag(), [observer = WrapScoped(observer), legacy_stats,
                                track = WrapScoped(track)] {
        StatsReports reports;
        legacy_stats->GetStats(track.get(), &reports);
        observer->OnComplete(reports);
      }));
}

void PeerConnectionMessageHandler::RequestUsagePatternReport(
    std::function<void()> func,
    int delay_ms) {
  signaling_thread_->PostDelayedTask(SafeTask(safety_.flag(), std::move(func)),
                                     TimeDelta::Millis(delay_ms));
}

}  // namespace webrtc
